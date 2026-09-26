/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Every case builds a FRESH, zero-initialised handle: json_parse() does NOT
 * reset pulse_delay, protocol, mem_size, vpp_mv or pins (src/json_parser.c:
 * 81-89), so a stale global handle would leak state between cases (Pitfall
 * 4 / S8).
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "eprom_params.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();

    /* delay()/delayMicroseconds() are free functions DEFINED by ArduinoFake,
     * not stubbed anywhere in the shared .inc -- every suite that could reach
     * them mocks them in its own setUp(). configure_memory/configure_eprom's
     * pulse_delay fallback never actually calls either, but this is cheap
     * insurance and matches house convention (e.g. test_cmd_admission). */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
}

void tearDown(void) {}

/* Fresh handle per case (Pitfall 4) -- json_parse never resets pulse_delay,
 * protocol, mem_size, vpp_mv or pins, so a stale global handle would leak
 * state between cases. FLAG_SKIP_ERASE keeps a CMD_WRITE dispatch from
 * wanting an erase path this suite never drives (configure_eprom only sets
 * up function pointers and the pulse_delay fallback for CMD_WRITE; neither
 * pointer is ever invoked here; write-init itself performs no blank check
 * at all any more, regardless of ctrl_flags). */
static firestarter_handle_t make_handle(uint32_t protocol, uint32_t pulse_delay_us) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.mem_size = 2048;
    h.ctrl_flags = FLAG_SKIP_ERASE;
    h.pulse_delay = pulse_delay_us;
    return h;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 1-3: the pulse_delay == 0 fallback, EXERCISED by a running test --
 * never merely asserted in prose (must_haves truth #1).
 * ───────────────────────────────────────────────────────────────────────── */

void test_0x07_zero_pulse_delay_takes_the_1000us_fallback(void) {
    firestarter_handle_t h = make_handle(0x07, 0);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "0x07 fallback: response_code");
    TEST_ASSERT_EQUAL_MESSAGE(1000, h.pulse_delay, "0x07 fallback: pulse_delay must become 1000 us");
}

void test_0x08_zero_pulse_delay_takes_the_100us_fallback(void) {
    firestarter_handle_t h = make_handle(0x08, 0);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "0x08 fallback: response_code");
    TEST_ASSERT_EQUAL_MESSAGE(100, h.pulse_delay, "0x08 fallback: pulse_delay must become 100 us");
}

void test_0x0B_zero_pulse_delay_takes_the_500us_fallback(void) {
    firestarter_handle_t h = make_handle(0x0B, 0);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "0x0B fallback: response_code");
    TEST_ASSERT_EQUAL_MESSAGE(500, h.pulse_delay, "0x0B fallback: pulse_delay must become 500 us");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 4-6: negative controls, 1:1 paired with 1-3 -- the non-vacuity proof
 * (T-140-13 / must_haves truth #3). Without these, cases 1-3 could pass on a
 * handle the fallback switch never actually touched.
 * ───────────────────────────────────────────────────────────────────────── */

void test_0x07_nonzero_pulse_delay_is_left_alone(void) {
    firestarter_handle_t h = make_handle(0x07, 777);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_MESSAGE(777, h.pulse_delay, "0x07 negative control: a nonzero pulse_delay must survive configure_memory untouched");
}

void test_0x08_nonzero_pulse_delay_is_left_alone(void) {
    firestarter_handle_t h = make_handle(0x08, 777);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_MESSAGE(777, h.pulse_delay, "0x08 negative control: a nonzero pulse_delay must survive configure_memory untouched");
}

void test_0x0B_nonzero_pulse_delay_is_left_alone(void) {
    firestarter_handle_t h = make_handle(0x0B, 777);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_MESSAGE(777, h.pulse_delay, "0x0B negative control: a nonzero pulse_delay must survive configure_memory untouched");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 7: row resolution -- three protocols resolve to three distinct rows
 * (must_haves truth #4, first half).
 * ───────────────────────────────────────────────────────────────────────── */

void test_each_protocol_resolves_to_its_own_distinct_row(void) {
    const eprom_params_t* row_07 = eprom_params_for(0x07);
    const eprom_params_t* row_08 = eprom_params_for(0x08);
    const eprom_params_t* row_0B = eprom_params_for(0x0B);

    TEST_ASSERT_NOT_NULL_MESSAGE(row_07, "0x07 must resolve to a row");
    TEST_ASSERT_NOT_NULL_MESSAGE(row_08, "0x08 must resolve to a row");
    TEST_ASSERT_NOT_NULL_MESSAGE(row_0B, "0x0B must resolve to a row");

    TEST_ASSERT_TRUE_MESSAGE(row_07 != row_08, "0x07 and 0x08 must resolve to distinct rows");
    TEST_ASSERT_TRUE_MESSAGE(row_07 != row_0B, "0x07 and 0x0B must resolve to distinct rows");
    TEST_ASSERT_TRUE_MESSAGE(row_08 != row_0B, "0x08 and 0x0B must resolve to distinct rows");
}

void test_unknown_protocol_returns_null(void) {
    TEST_ASSERT_NULL_MESSAGE(eprom_params_for(0x0C), "0x0C is unrecognised and must return NULL, never a default row");
    TEST_ASSERT_NULL_MESSAGE(eprom_params_for(0), "protocol 0 is unrecognised and must return NULL, never a default row");
}

static void assert_row_matches(uint32_t protocol, uint32_t expected_overprogram_cap_us,
                                uint32_t expected_energy_cap_us, uint8_t expected_max_pulses,
                                uint8_t expected_overprogram_factor, uint8_t expected_verify_mode,
                                uint8_t expected_vpp_path, const char* ctx) {
    const eprom_params_t* row = eprom_params_for(protocol);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, ctx);

    uint32_t overprogram_cap_us = pgm_read_dword(&row->overprogram_cap_us);
    uint32_t energy_cap_us = pgm_read_dword(&row->energy_cap_us);
    uint8_t max_pulses = pgm_read_byte(&row->max_pulses);
    uint8_t overprogram_factor = pgm_read_byte(&row->overprogram_factor);
    uint8_t verify_mode = pgm_read_byte(&row->verify_mode);
    uint8_t vpp_path = pgm_read_byte(&row->vpp_path);

    TEST_ASSERT_EQUAL_MESSAGE(expected_overprogram_cap_us, overprogram_cap_us, ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_energy_cap_us, energy_cap_us, ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_max_pulses, max_pulses, ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_overprogram_factor, overprogram_factor, ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_verify_mode, verify_mode, ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_vpp_path, vpp_path, ctx);
}

void test_row_values_match_the_frozen_table(void) {
    assert_row_matches(0x07, 75000UL, 0UL, 25, 0, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR,
                        "0x07 row (plan 140-01 locked values)");
    assert_row_matches(0x08, 75000UL, 0UL, 25, 0, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR,
                        "0x08 row (plan 140-01 locked values)");
    assert_row_matches(0x0B, 75000UL, 50000UL, 255, 0, VERIFY_PER_PULSE, VPP_PATH_DIRECT_VPE,
                        "0x0B row (plan 140-01 locked values)");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_0x07_zero_pulse_delay_takes_the_1000us_fallback);
    RUN_TEST(test_0x08_zero_pulse_delay_takes_the_100us_fallback);
    RUN_TEST(test_0x0B_zero_pulse_delay_takes_the_500us_fallback);

    RUN_TEST(test_0x07_nonzero_pulse_delay_is_left_alone);
    RUN_TEST(test_0x08_nonzero_pulse_delay_is_left_alone);
    RUN_TEST(test_0x0B_nonzero_pulse_delay_is_left_alone);

    RUN_TEST(test_each_protocol_resolves_to_its_own_distinct_row);
    RUN_TEST(test_unknown_protocol_returns_null);
    RUN_TEST(test_row_values_match_the_frozen_table);

    return UNITY_END();
}
