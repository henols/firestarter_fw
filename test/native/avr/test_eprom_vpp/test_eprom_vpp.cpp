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

extern "C" {
#include "memory.h"
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

void setUp(void) {
    ArduinoFakeReset();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
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
    return UNITY_END();
}
