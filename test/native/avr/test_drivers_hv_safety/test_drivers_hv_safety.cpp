/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * High-voltage ordering and refusal behaviour of the PROM drivers:
 *   - Intel flash write and erase measure the regulator alone, and add the
 *     pin 1 route only after an in-window reading.
 *   - The AT28C chip-ID read measures the regulator before it raises A9.
 *   - A NOR sector erase above the 16-bit command address range is refused.
 *   - An EPROM pulse wider than EPROM_PULSE_MAX_US is refused.
 *   - The 5 V page-write poll compares DQ7 only.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "eprom.h"
#include "firestarter.h"
#include "flash_utils.h"
#include "rurp_pinout.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"

using namespace fakeit;

extern "C" void set_mock_vpp_mv(uint16_t mv);
extern "C" void set_mock_hw_rev(uint8_t rev);

/* Control-register recorder. */
#define MAX_CTRL_WRITES 64
static rurp_register_t s_ctrl_bits[MAX_CTRL_WRITES];
static bool s_ctrl_state[MAX_CTRL_WRITES];
static unsigned s_ctrl_count = 0;

static void mock_set_ctrl_reg(struct firestarter_handle*, rurp_register_t reg, bool state) {
    if (s_ctrl_count < MAX_CTRL_WRITES) {
        s_ctrl_bits[s_ctrl_count] = reg;
        s_ctrl_state[s_ctrl_count] = state;
    }
    s_ctrl_count++;
}
static bool mock_get_ctrl_reg(struct firestarter_handle*, rurp_register_t) { return 0; }
static void mock_set_data(struct firestarter_handle*, uint32_t, uint8_t) {}

/* get_data returns s_read_value for every address. */
static uint8_t s_read_value = 0xFF;
static uint8_t mock_get_data(struct firestarter_handle*, uint32_t) { return s_read_value; }

/* Index of the first write that asserts any bit in `bits`, or -1. */
static int first_assert_of(rurp_register_t bits) {
    for (unsigned i = 0; i < s_ctrl_count && i < MAX_CTRL_WRITES; i++) {
        if (s_ctrl_state[i] && (s_ctrl_bits[i] & bits)) {
            return (int)i;
        }
    }
    return -1;
}

static bool last_write_clears(rurp_register_t bits) {
    if (s_ctrl_count == 0 || s_ctrl_count > MAX_CTRL_WRITES) {
        return false;
    }
    unsigned i = s_ctrl_count - 1;
    return !s_ctrl_state[i] && (s_ctrl_bits[i] & bits) == bits;
}

void setUp(void) {
    ArduinoFakeReset();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    set_mock_vpp_mv(0);
    set_mock_hw_rev(1);
    s_ctrl_count = 0;
    s_read_value = 0xFF;
    rurp_get_config()->r1 = VALUE_R1;
    rurp_get_config()->r2 = VALUE_R2;
    rurp_get_config()->bandgap_mv = 1019;  // calibrated
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd, uint16_t vpp_mv, uint32_t flags) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = vpp_mv;
    h.ctrl_flags = flags;
    h.mem_size = 0x8000;
    return h;
}

static void wire_mocks(firestarter_handle_t* h) {
    h->firestarter_set_control_register = mock_set_ctrl_reg;
    h->firestarter_get_control_register = mock_get_ctrl_reg;
    h->firestarter_set_data = mock_set_data;
    h->firestarter_get_data = mock_get_data;
}

/* ---- Intel flash (0x10) ---- */

void test_intel_write_measures_regulator_before_pin1(void) {
    set_mock_vpp_mv(12000);
    firestarter_handle_t h = make_handle(0x10, CMD_WRITE, 12000, FLAG_SKIP_ERASE);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    int reg = first_assert_of(CTRL_VPP_REGULATOR_ENABLE);
    int p1 = first_assert_of(CTRL_VPP_P1_ENABLE);
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(p1 > reg);
    TEST_ASSERT_EQUAL_UINT(0, s_ctrl_bits[reg] & CTRL_VPP_P1_ENABLE);
}

void test_intel_write_high_vpp_never_asserts_pin1(void) {
    set_mock_vpp_mv(12700);
    firestarter_handle_t h = make_handle(0x10, CMD_WRITE, 12000, FLAG_SKIP_ERASE);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(CTRL_VPP_P1_ENABLE));
    TEST_ASSERT_TRUE(last_write_clears(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE));
}

void test_intel_erase_has_init(void) {
    firestarter_handle_t h = make_handle(0x10, CMD_ERASE, 12000, 0);
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NOT_NULL(h.firestarter_operation_main);
}

void test_intel_erase_refuses_bad_calibration_before_hv(void) {
    set_mock_vpp_mv(12000);
    rurp_get_config()->r2 = 0;
    firestarter_handle_t h = make_handle(0x10, CMD_ERASE, 12000, 0);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE));
}

void test_intel_erase_high_vpp_refuses_and_clears(void) {
    set_mock_vpp_mv(12700);
    firestarter_handle_t h = make_handle(0x10, CMD_ERASE, 12000, 0);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(CTRL_VPP_P1_ENABLE));
    TEST_ASSERT_TRUE(last_write_clears(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE));
}

void test_intel_erase_nominal_asserts_pin1(void) {
    set_mock_vpp_mv(12000);
    firestarter_handle_t h = make_handle(0x10, CMD_ERASE, 12000, 0);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_TRUE(first_assert_of(CTRL_VPP_P1_ENABLE) > first_assert_of(CTRL_VPP_REGULATOR_ENABLE));
}

void test_intel_rev0_skips_calibration_refusal(void) {
    set_mock_hw_rev(0);
    rurp_get_config()->r2 = 0;
    firestarter_handle_t h = make_handle(0x10, CMD_WRITE, 12000, FLAG_SKIP_ERASE);
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
#ifdef HARDWARE_REVISION
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, h.response_code);
#endif
}

/* ---- AT28C chip-ID (0x0D) ---- */

static firestarter_handle_t run_28c_chip_id(uint16_t vpp_target, uint16_t measured) {
    set_mock_vpp_mv(measured);
    s_read_value = 0x1E;  /* reads 0x1E1E */
    firestarter_handle_t h = make_handle(0x0D, CMD_WRITE, vpp_target, FLAG_SKIP_SDP_UNLOCK);
    h.chip_id = 0x1E1E;
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_init(&h);
    return h;
}

void test_28c_chip_id_high_vpp_never_raises_a9(void) {
    firestarter_handle_t h = run_28c_chip_id(12000, 12700);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(CTRL_VPP_A9_ENABLE));
    TEST_ASSERT_TRUE(last_write_clears(CTRL_VPP_REGULATOR_ENABLE));
}

void test_28c_chip_id_bad_calibration_refused_before_hv(void) {
    rurp_get_config()->r2 = 0;
    firestarter_handle_t h = run_28c_chip_id(12000, 12000);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_A9_ENABLE));
}

void test_28c_chip_id_nominal_reads_id(void) {
    firestarter_handle_t h = run_28c_chip_id(12000, 12000);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_TRUE(first_assert_of(CTRL_VPP_A9_ENABLE) > first_assert_of(CTRL_VPP_REGULATOR_ENABLE));
}

void test_28c_chip_id_without_target_skips_measurement(void) {
    firestarter_handle_t h = run_28c_chip_id(0, 20000);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_TRUE(first_assert_of(CTRL_VPP_A9_ENABLE) >= 0);
}

/* ---- NOR sector erase (0x06) ---- */

void test_nor_sector_erase_above_16_bits_refused(void) {
    firestarter_handle_t h = make_handle(0x06, CMD_ERASE, 0, 0);
    h.address = 0x10000;
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT(0, s_ctrl_count);
}

void test_nor_sector_erase_within_16_bits_runs(void) {
    firestarter_handle_t h = make_handle(0x06, CMD_ERASE, 0, 0);
    h.address = 0xFFFF;
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_GREATER_THAN_UINT(0, s_ctrl_count);
}

/* ---- EPROM pulse bound ---- */

void test_eprom_pulse_above_max_refused(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE, 12500, 0);
    h.pulse_delay = EPROM_PULSE_MAX_US + 1;
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_eprom_pulse_at_max_accepted(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE, 12500, 0);
    h.pulse_delay = EPROM_PULSE_MAX_US;
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
}

/* ---- 5 V page write poll (0x05) ---- */

static firestarter_handle_t run_5v_page_write(uint8_t expected, uint8_t read_back) {
    firestarter_handle_t h = make_handle(0x05, CMD_WRITE, 0, 0);
    h.page_size = 64;
    h.data_size = 64;
    memset(h.data_buffer, expected, 64);
    s_read_value = read_back;
    configure_memory(&h);
    wire_mocks(&h);
    h.firestarter_operation_main(&h);
    return h;
}

void test_5v_page_poll_completes_on_dq7_match(void) {
    // Same DQ7, different low bits: the cycle is complete.
    firestarter_handle_t h = run_5v_page_write(0x7F, 0x00);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
}

void test_5v_page_poll_times_out_on_dq7_complement(void) {
    firestarter_handle_t h = run_5v_page_write(0x7F, 0x80);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_intel_write_measures_regulator_before_pin1);
    RUN_TEST(test_intel_write_high_vpp_never_asserts_pin1);
    RUN_TEST(test_intel_erase_has_init);
    RUN_TEST(test_intel_erase_refuses_bad_calibration_before_hv);
    RUN_TEST(test_intel_erase_high_vpp_refuses_and_clears);
    RUN_TEST(test_intel_erase_nominal_asserts_pin1);
    RUN_TEST(test_intel_rev0_skips_calibration_refusal);
    RUN_TEST(test_28c_chip_id_high_vpp_never_raises_a9);
    RUN_TEST(test_28c_chip_id_bad_calibration_refused_before_hv);
    RUN_TEST(test_28c_chip_id_nominal_reads_id);
    RUN_TEST(test_28c_chip_id_without_target_skips_measurement);
    RUN_TEST(test_nor_sector_erase_above_16_bits_refused);
    RUN_TEST(test_nor_sector_erase_within_16_bits_runs);
    RUN_TEST(test_eprom_pulse_above_max_refused);
    RUN_TEST(test_eprom_pulse_at_max_accepted);
    RUN_TEST(test_5v_page_poll_completes_on_dq7_match);
    RUN_TEST(test_5v_page_poll_times_out_on_dq7_complement);
    return UNITY_END();
}
