/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * eprom_check_vpp and eprom_generic_init, driven through configure_memory()
 * with a mocked rurp_read_voltage_mv. The suite records every register write
 * at the full register width and asserts:
 *   - a reading above the window is an ERROR and the last control-register
 *     write clears every bit in EPROM_HV_ALL_OFF_MASK;
 *   - an unusable divider calibration is refused before any high-voltage bit
 *     is written;
 *   - the window edges, as value pairs: the value that passes and the
 *     adjacent value that does not.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <vector>

extern "C" {
#include "memory.h"
#include "messages.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"

using namespace fakeit;

extern "C" void set_mock_vpp_mv(uint16_t mv);
extern "C" void set_mock_hw_rev(uint8_t rev);

extern "C" void clear_bus_recording();
extern "C" int bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" rurp_register_t recorded_data(int i);

/* Every high-voltage control bit at the full register width. */
static const rurp_register_t kHvBits = (rurp_register_t)(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE |
                                                         CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE |
                                                         CTRL_VPP_VPE_DROP_ENABLE);

static std::vector<uint8_t> captured;

/* The params of the first frame with this id, or an empty vector. Frame
 * layout: 4 preamble bytes, u16 length, id, params, CRC. */
static std::vector<uint8_t> frame_params(uint8_t id) {
    size_t offset = 0;
    while (offset + 7 <= captured.size()) {
        uint16_t len = (uint16_t)(((uint16_t)captured[offset + 4] << 8) | captured[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len + 1;
        if (offset + frame_size > captured.size()) {
            break;
        }
        if (captured[offset + 6] == id) {
            return std::vector<uint8_t>(captured.begin() + offset + 7, captured.begin() + offset + 6 + len - 1);
        }
        offset += frame_size;
    }
    return std::vector<uint8_t>();
}

void setUp(void) {
    ArduinoFakeReset();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    captured.clear();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    clear_bus_recording();
    set_mock_vpp_mv(0);
    set_mock_hw_rev(1); /* not Rev 0 */
    rurp_get_config()->r1 = VALUE_R1;
    rurp_get_config()->r2 = VALUE_R2;
    rurp_get_config()->bandgap_mv = 1019; /* calibrated, so no uncalibrated warning */
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint16_t vpp_setpoint, uint32_t flags) {
    firestarter_handle_t h = {};
    h.protocol = 0x07;
    h.cmd = CMD_READ;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = vpp_setpoint;
    h.chip_id = 0; /* skip the chip-ID branch */
    h.mem_size = 65536;
    h.ctrl_flags = flags;
    return h;
}

/* Dispatch, run the init and return the response code. */
static uint8_t run_init(firestarter_handle_t* h, uint16_t measured_mv) {
    set_mock_vpp_mv(measured_mv);
    configure_memory(h);
    TEST_ASSERT_NOT_NULL(h->firestarter_operation_init);
    h->firestarter_operation_init(h);
    return h->response_code;
}

static uint8_t run_at(uint16_t target, uint16_t measured, uint32_t flags) {
    firestarter_handle_t h = make_handle(target, flags);
    return run_init(&h, measured);
}

static bool any_control_write_with(rurp_register_t bits) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER && (recorded_data(i) & bits)) {
            return true;
        }
    }
    return false;
}

static int last_control_write_index(void) {
    int last = -1;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            last = i;
        }
    }
    return last;
}

void test_high_reading_is_an_error_and_clears_every_hv_bit(void) {
    firestarter_handle_t h = make_handle(13000, 0);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_init(&h, 14000));
    TEST_ASSERT_TRUE_MESSAGE(any_control_write_with(CTRL_VPP_REGULATOR_ENABLE),
                             "the check must route the regulator before it reads");
    int last = last_control_write_index();
    TEST_ASSERT_TRUE(last >= 0);
    TEST_ASSERT_BITS_LOW_MESSAGE((rurp_register_t)EPROM_HV_ALL_OFF_MASK, recorded_data(last),
                                 "the last control write must clear every HV bit");
}

void test_in_band_reading_also_ends_with_every_hv_bit_clear(void) {
    firestarter_handle_t h = make_handle(13000, 0);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_init(&h, 13000));
    int last = last_control_write_index();
    TEST_ASSERT_TRUE(last >= 0);
    TEST_ASSERT_BITS_LOW((rurp_register_t)EPROM_HV_ALL_OFF_MASK, recorded_data(last));
}

void test_force_downgrades_a_high_reading_to_a_warning(void) {
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(13000, 14000, FLAG_FORCE));
}

void test_a_zero_r2_is_refused_before_any_hv_write(void) {
    rurp_get_config()->r2 = 0;
    firestarter_handle_t h = make_handle(13000, 0);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_init(&h, 13000));
    TEST_ASSERT_FALSE_MESSAGE(any_control_write_with(kHvBits),
                              "an unusable calibration must be refused before any HV bit is written");
}

void test_force_does_not_override_a_bad_calibration(void) {
    rurp_get_config()->r2 = 0;
    firestarter_handle_t h = make_handle(13000, FLAG_FORCE);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_init(&h, 13000));
    TEST_ASSERT_FALSE(any_control_write_with(kHvBits));
}

void test_an_out_of_band_bandgap_is_refused_before_any_hv_write(void) {
    rurp_get_config()->bandgap_mv = 999;
    firestarter_handle_t h = make_handle(13000, 0);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_init(&h, 13000));
    TEST_ASSERT_FALSE(any_control_write_with(kHvBits));
}

void test_high_edge_below_the_crossover_uses_the_floor(void) {
    /* 12000 target: 3 % is 360, below the 500 floor, so the ceiling is 12500. */
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(12000, 12500, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(12000, 12501, 0));
}

void test_high_edge_above_the_crossover_uses_the_percentage(void) {
    /* 25000 target: 3 % is 750, above the floor, so the ceiling is 25750. */
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(25000, 25750, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(25000, 25751, 0));
}

void test_low_edge_is_five_percent(void) {
    /* 12000 target: the floor is 11400. Below it is a warning, never an error. */
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, run_at(12000, 11400, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(12000, 11399, 0));
}

/* The mismatch reports carry [measured_mv u16 BE][expected_mv u16 BE]; the
 * host formats them. */
static void assert_mv_report(uint8_t id, uint16_t measured, uint16_t expected) {
    std::vector<uint8_t> p = frame_params(id);
    TEST_ASSERT_EQUAL_MESSAGE(4, p.size(), "the report carries two u16 values");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(measured, (uint16_t)((p[0] << 8) | p[1]), "measured mV");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected, (uint16_t)((p[2] << 8) | p[3]), "expected mV");
}

void test_a_high_reading_reports_both_values_in_millivolts(void) {
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(13000, 14449, 0));
    assert_mv_report(MSG_ERR_VPP_HIGH_MV, 14449, 13000);
}

void test_a_forced_high_reading_reports_a_warning_in_millivolts(void) {
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(13000, 14449, FLAG_FORCE));
    assert_mv_report(MSG_WARN_VPP_HIGH_MV, 14449, 13000);
}

void test_a_low_reading_reports_both_values_in_millivolts(void) {
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(13000, 12000, 0));
    assert_mv_report(MSG_WARN_VPP_LOW_MV, 12000, 13000);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_high_reading_is_an_error_and_clears_every_hv_bit);
    RUN_TEST(test_in_band_reading_also_ends_with_every_hv_bit_clear);
    RUN_TEST(test_force_downgrades_a_high_reading_to_a_warning);
    RUN_TEST(test_a_zero_r2_is_refused_before_any_hv_write);
    RUN_TEST(test_force_does_not_override_a_bad_calibration);
    RUN_TEST(test_an_out_of_band_bandgap_is_refused_before_any_hv_write);
    RUN_TEST(test_high_edge_below_the_crossover_uses_the_floor);
    RUN_TEST(test_high_edge_above_the_crossover_uses_the_percentage);
    RUN_TEST(test_low_edge_is_five_percent);
    RUN_TEST(test_a_high_reading_reports_both_values_in_millivolts);
    RUN_TEST(test_a_forced_high_reading_reports_a_warning_in_millivolts);
    RUN_TEST(test_a_low_reading_reports_both_values_in_millivolts);
    return UNITY_END();
}
