/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The ADC multiplexer arithmetic behind the bandgap reading.
 *
 * The register values used to be hand-assembled from AVR bit names inside
 * rurp_common.cpp -- a translation unit gated on the three ARDUINO_AVR_*
 * macros, which no native environment compiles. So the one thing that decides
 * WHICH voltage the firmware calls its reference had no test at all. Moving
 * the arithmetic into a pure header fixed that; these cases pin it.
 *
 * The expected ADMUX bytes below are the exact values the pre-refactor code
 * wrote, transcribed from the bit expressions it used, so a wrong channel
 * number turns this red rather than silently reading a different pin.
 */

#include <unity.h>

#include <stdint.h>

extern "C" {
#include "rurp_adc_mux.h"
}

void setUp(void) {}
void tearDown(void) {}

// REFS0 is bit 6; MUX4..MUX1 are bits 4..1.
static const uint8_t kRefs0 = 1u << 6;
static const uint8_t kMux1 = 1u << 1;
static const uint8_t kMux2 = 1u << 2;
static const uint8_t kMux3 = 1u << 3;
static const uint8_t kMux4 = 1u << 4;

static void test_admux_matches_the_pre_refactor_writes(void) {
    // ATmega328P/PB: ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1)
    TEST_ASSERT_EQUAL_HEX8(kRefs0 | kMux3 | kMux2 | kMux1,
                           rurp_admux_value(RURP_ADC_REF_AVCC, RURP_ADC_CHANNEL_BANDGAP_328P));
    // ATmega32U4: ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1)
    TEST_ASSERT_EQUAL_HEX8(kRefs0 | kMux4 | kMux3 | kMux2 | kMux1,
                           rurp_admux_value(RURP_ADC_REF_AVCC, RURP_ADC_CHANNEL_BANDGAP_32U4));
}

static void test_the_two_parts_differ_only_in_the_channel(void) {
    // Both parts' bandgap is 1.1 V nominal. The channel number moves; the
    // reference selection does not. If these two were ever made equal, one of
    // the boards would be reading a pin instead of its reference.
    TEST_ASSERT_NOT_EQUAL(RURP_ADC_CHANNEL_BANDGAP_328P, RURP_ADC_CHANNEL_BANDGAP_32U4);
    TEST_ASSERT_EQUAL_UINT8(14, RURP_ADC_CHANNEL_BANDGAP_328P);
    TEST_ASSERT_EQUAL_UINT8(30, RURP_ADC_CHANNEL_BANDGAP_32U4);
    // Same reference on both: AVCC, REFS[1:0] = 01.
    TEST_ASSERT_EQUAL_UINT8(1, RURP_ADC_REF_AVCC);
    TEST_ASSERT_EQUAL_HEX8(
        rurp_admux_value(RURP_ADC_REF_AVCC, 0) & 0xC0,
        rurp_admux_value(RURP_ADC_REF_AVCC, RURP_ADC_CHANNEL_BANDGAP_32U4) & 0xC0);
}

static void test_reference_select_is_not_the_2v56_option(void) {
    // REFS[1:0] = 11 is 1.1 V on the 328P but 2.56 V on the 32U4. Selecting it
    // would make the Leonardo behave unlike the Unos. Assert we are nowhere
    // near it.
    TEST_ASSERT_NOT_EQUAL(3, RURP_ADC_REF_AVCC);
    TEST_ASSERT_EQUAL_HEX8(0x40,
                           rurp_admux_value(RURP_ADC_REF_AVCC, RURP_ADC_CHANNEL_BANDGAP_32U4) & 0xC0);
}

static void test_bandgap_channels_need_mux5_clear(void) {
    TEST_ASSERT_EQUAL_UINT8(0, rurp_mux5_value(RURP_ADC_CHANNEL_BANDGAP_328P));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_mux5_value(RURP_ADC_CHANNEL_BANDGAP_32U4));
}

static void test_mux5_is_not_inert(void) {
    // Non-vacuity. The two assertions above would pass against a helper
    // hardwired to return 0, which is exactly the bug this guards -- the old
    // code never wrote MUX5 at all, which is indistinguishable from always
    // writing 0 until something sets it.
    //
    // Leonardo A6..A11 are ADC8..ADC13. They fit in five bits, so they do NOT
    // set MUX5 -- the real MUX5 channels start at 32 (the differential and
    // extended set). Both facts are asserted, because getting either wrong
    // changes which pin a later read leaves selected.
    TEST_ASSERT_EQUAL_UINT8(0, rurp_mux5_value(13));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_mux5_value(31));  // last channel without it
    TEST_ASSERT_EQUAL_UINT8(1, rurp_mux5_value(32));  // first channel with it
    TEST_ASSERT_EQUAL_UINT8(1, rurp_mux5_value(40));
}

static void test_admux_never_leaks_the_sixth_channel_bit(void) {
    // A channel above 31 must not corrupt the reference bits by overflowing
    // into them. This is the failure the mask exists to prevent.
    TEST_ASSERT_EQUAL_HEX8(0x40 | 8, rurp_admux_value(RURP_ADC_REF_AVCC, 40));
    TEST_ASSERT_EQUAL_HEX8(0x40, rurp_admux_value(RURP_ADC_REF_AVCC, 32) & 0xC0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_admux_matches_the_pre_refactor_writes);
    RUN_TEST(test_the_two_parts_differ_only_in_the_channel);
    RUN_TEST(test_reference_select_is_not_the_2v56_option);
    RUN_TEST(test_bandgap_channels_need_mux5_clear);
    RUN_TEST(test_mux5_is_not_inert);
    RUN_TEST(test_admux_never_leaks_the_sixth_channel_bit);
    return UNITY_END();
}
