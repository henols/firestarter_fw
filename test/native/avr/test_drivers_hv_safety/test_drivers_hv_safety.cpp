/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * High-voltage ordering and refusal behaviour of the PROM drivers:
 *   - An EPROM pulse wider than EPROM_PULSE_MAX_US is refused on every row.
 *   - The EPROM write body refuses a protocol with no parameter row before
 *     it asserts any route.
 *   - The AT28C chip-ID read measures the regulator before it raises A9.
 *   - configure_memory refuses a protocol with no handler before it writes
 *     to any register.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "eprom.h"
#include "firestarter.h"
#include "rurp_pinout.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"

using namespace fakeit;

extern "C" void set_mock_vpp_mv(uint16_t mv);
extern "C" void set_mock_hw_rev(uint8_t rev);
#include "../_shared/bus_recording.h"

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

/* ---- EPROM pulse bound ---- */

/* 0x07 and 0x08 have no energy cap, so before EPROM_PULSE_MAX_US a wider
 * pulse reached the write loop on those rows. */
void test_eprom_pulse_above_max_refused_on_every_row(void) {
    const uint32_t protocols[] = {0x07, 0x08, 0x0B};
    for (unsigned i = 0; i < sizeof(protocols) / sizeof(protocols[0]); i++) {
        firestarter_handle_t h = make_handle(protocols[i], CMD_WRITE, 12500, 0);
        h.pulse_delay = EPROM_PULSE_MAX_US + 1;
        configure_memory(&h);
        TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    }
}

void test_eprom_pulse_at_max_accepted_on_uncapped_rows(void) {
    const uint32_t protocols[] = {0x07, 0x08};
    for (unsigned i = 0; i < sizeof(protocols) / sizeof(protocols[0]); i++) {
        firestarter_handle_t h = make_handle(protocols[i], CMD_WRITE, 12500, 0);
        h.pulse_delay = EPROM_PULSE_MAX_US;
        configure_memory(&h);
        TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    }
}

/* The write body looks up its parameter row before it asserts a route. */
void test_eprom_write_without_a_row_asserts_no_route(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE, 12500, 0);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    wire_mocks(&h);
    h.protocol = 0x99;
    h.data_size = 16;
    memset(h.data_buffer, 0x00, 16);
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_INT(-1, first_assert_of(EPROM_HV_ALL_OFF_MASK));
    TEST_ASSERT_TRUE(last_write_clears(EPROM_HV_ALL_OFF_MASK));
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

#ifdef HARDWARE_REVISION
/* Rev 0 cannot measure the rail, so the read goes ahead unmeasured. */
void test_28c_chip_id_rev0_skips_measurement(void) {
    set_mock_hw_rev(0);
    firestarter_handle_t h = run_28c_chip_id(12000, 20000);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_TRUE(first_assert_of(CTRL_VPP_A9_ENABLE) >= 0);
}
#endif

/* ---- configure_memory dispatch ---- */

void test_unknown_protocol_is_refused_before_the_bus(void) {
    const uint32_t protocols[] = {0x00, 0x11, 0x2A, 0x2B, 0x2C, 0x34, 0x99};
    for (unsigned i = 0; i < sizeof(protocols) / sizeof(protocols[0]); i++) {
        clear_bus_recording();
        firestarter_handle_t h = make_handle(protocols[i], CMD_WRITE, 12500, 0);
        configure_memory(&h);
        TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
        TEST_ASSERT_EQUAL_INT(0, bus_recording_count());
    }
}

void test_known_protocol_sets_the_address_bus(void) {
    clear_bus_recording();
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE, 12500, 0);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_GREATER_THAN_INT(0, bus_recording_count());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_eprom_pulse_above_max_refused_on_every_row);
    RUN_TEST(test_eprom_pulse_at_max_accepted_on_uncapped_rows);
    RUN_TEST(test_eprom_write_without_a_row_asserts_no_route);
    RUN_TEST(test_28c_chip_id_high_vpp_never_raises_a9);
    RUN_TEST(test_28c_chip_id_bad_calibration_refused_before_hv);
    RUN_TEST(test_28c_chip_id_nominal_reads_id);
    RUN_TEST(test_28c_chip_id_without_target_skips_measurement);
#ifdef HARDWARE_REVISION
    RUN_TEST(test_28c_chip_id_rev0_skips_measurement);
#endif
    RUN_TEST(test_unknown_protocol_is_refused_before_the_bus);
    RUN_TEST(test_known_protocol_sets_the_address_bus);
    return UNITY_END();
}
