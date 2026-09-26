/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Native coverage for the ADC-count-to-millivolt arithmetic
 * (src/rurp_voltage_math.c).
 *
 * This suite REPLACES tests/test_voltage_reformulation_oracle.py, which
 * proved the same properties against a Python reimplementation bound to the
 * shipped C by a regex source scan. That module's own docstring named why a
 * native test was unavailable: the arithmetic lived in a translation unit
 * gated on the three ARDUINO_AVR_* board macros whose body writes ADMUX and
 * ADCSRA. Extracting the arithmetic removed that blocker, so the properties
 * are now asserted against the shipped C itself, compiled and executed.
 *
 * The 64-bit reference form below is the pre-reformulation shipped code. It
 * is a reference implementation for an equivalence claim, not a second
 * production path.
 */

#include <unity.h>

#include <stdint.h>

#include "rurp_voltage_math.h"

// The shipped defaults, transcribed so a change to rurp_shield.h does not
// silently redefine what this suite is asserting. Kept in lockstep by
// test_shipped_defaults_match_the_header below.
#include "rurp_shield.h"

static const uint32_t kR1 = VALUE_R1;
static const uint32_t kR2 = VALUE_R2;

// The 64-bit form rurp_scale_voltage_mv replaced:
//   numerator   = adc * 1100 * (r1 + r2)
//   denominator = bandgap_adc * r2
//   result      = (numerator + denominator / 2) / denominator
static uint32_t reference_v64(uint32_t adc, uint32_t bandgap_adc, uint32_t r1, uint32_t r2) {
    if (r2 == 0 || bandgap_adc == 0) {
        return 0;
    }
    uint64_t numerator = (uint64_t)adc * 1100ULL * (uint64_t)(r1 + r2);
    uint64_t denominator = (uint64_t)bandgap_adc * (uint64_t)r2;
    return (uint32_t)((numerator + denominator / 2ULL) / denominator);
}

void setUp(void) {}
void tearDown(void) {}

// --- The scale factor -------------------------------------------------

static void test_scale_factor_is_exact_at_the_shipped_calibration(void) {
    // k = 1100 * (r1 + r2) / r2 divides exactly at the shipped calibration,
    // so the folded form loses nothing there.
    TEST_ASSERT_EQUAL_UINT32(0, (RURP_BANDGAP_NOMINAL_MV * (kR1 + kR2)) % kR2);
    TEST_ASSERT_EQUAL_UINT32(7850, (RURP_BANDGAP_NOMINAL_MV * (kR1 + kR2)) / kR2);
}

static void test_shipped_defaults_match_the_header(void) {
    // Non-vacuity anchor: if VALUE_R1/VALUE_R2 move, every figure below is
    // about a different divider and this suite must be re-derived.
    TEST_ASSERT_EQUAL_UINT32(270000, kR1);
    TEST_ASSERT_EQUAL_UINT32(44000, kR2);
}

// --- The named reading ------------------------------------------------

static void test_named_single_reading(void) {
    // adc 1023 at bandgap count 225 gives 35691 mV in both forms.
    TEST_ASSERT_EQUAL_UINT16(35691, rurp_scale_voltage_mv(1023, 225, kR1, kR2));
    TEST_ASSERT_EQUAL_UINT32(35691, reference_v64(1023, 225, kR1, kR2));
}

// --- Equivalence ------------------------------------------------------

static void test_bit_identity_over_the_full_bandgap_range(void) {
    // 0 mismatches over bandgap 1..1023 crossed with adc 0..1023 at the
    // shipped calibration.
    for (uint32_t bg = 1; bg < 1024; bg++) {
        for (uint32_t adc = 0; adc < 1024; adc++) {
            uint32_t expected = reference_v64(adc, bg, kR1, kR2);
            if (expected > 0xFFFFUL) {
                continue;  // both forms narrow identically; compare below the cast
            }
            TEST_ASSERT_EQUAL_UINT16((uint16_t)expected,
                                     rurp_scale_voltage_mv(adc, bg, kR1, kR2));
        }
    }
}

static void test_worst_deviation_is_five_and_never_over_reads(void) {
    // Over r2 39000..47000 step 1000, bandgap 200..250, adc 0..1023: the
    // folded form never exceeds the 64-bit form, and the worst shortfall is
    // exactly 5 mV. One-directional under-reading cannot suppress a
    // high-side VPP error, which fires on an over-read.
    uint32_t worst = 0;
    for (uint32_t r2 = 39000; r2 <= 47000; r2 += 1000) {
        for (uint32_t bg = 200; bg <= 250; bg++) {
            for (uint32_t adc = 0; adc < 1024; adc++) {
                uint32_t v64 = reference_v64(adc, bg, kR1, r2);
                uint32_t v32 = rurp_scale_voltage_mv(adc, bg, kR1, r2);
                if (v64 > 0xFFFFUL) {
                    continue;
                }
                TEST_ASSERT_FALSE_MESSAGE(v32 > v64, "folded form over-read");
                if (v64 - v32 > worst) {
                    worst = v64 - v32;
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(5, worst);
}

// --- The uint32 guards, both boundaries -------------------------------

static void test_divider_sum_guard_boundary(void) {
    // r1 + r2 exactly at the bound evaluates; one over refuses.
    uint32_t r2 = 44000;
    uint32_t r1_at = RURP_DIVIDER_SUM_MAX - r2;
    TEST_ASSERT_EQUAL_UINT32(RURP_DIVIDER_SUM_MAX, r1_at + r2);
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(r1_at, r2));
    TEST_ASSERT_NOT_EQUAL(0, rurp_scale_voltage_mv(512, 225, r1_at, r2));

    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(r1_at + 1, r2));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, r1_at + 1, r2));
}

static void test_scale_factor_guard_boundary(void) {
    // k exactly at the bound evaluates; one over refuses. Both pairs pass the
    // divider-sum guard first, so this isolates the second guard -- which is
    // only reachable at all when r2 is under about 1023, since otherwise
    // RURP_DIVIDER_SUM_MAX caps the sum before k can grow.
    const uint32_t r2 = 1000;
    const uint32_t r1_at = 3812003;   // r1 + r2 = 3813003 -> k == 4194303
    const uint32_t r1_over = 3812004; // r1 + r2 = 3813004 -> k == 4194304

    TEST_ASSERT_TRUE(r1_at + r2 <= RURP_DIVIDER_SUM_MAX);
    TEST_ASSERT_TRUE(r1_over + r2 <= RURP_DIVIDER_SUM_MAX);

    TEST_ASSERT_EQUAL_UINT32(RURP_SCALE_K_MAX,
                             (RURP_BANDGAP_NOMINAL_MV * (r1_at + r2)) / r2);
    TEST_ASSERT_EQUAL_UINT32(RURP_SCALE_K_MAX + 1,
                             (RURP_BANDGAP_NOMINAL_MV * (r1_over + r2)) / r2);

    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(r1_at, r2));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(r1_over, r2));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, r1_over, r2));
}

static void test_scale_guard_keeps_the_product_inside_thirty_two_bits(void) {
    // adc at full scale times k at the bound must not wrap uint32.
    uint64_t product = (uint64_t)1023 * (uint64_t)RURP_SCALE_K_MAX;
    TEST_ASSERT_EQUAL_UINT64(4290771969ULL, product);
    TEST_ASSERT_TRUE(product < 4294967295ULL);
}

// --- Zero sentinels ---------------------------------------------------

static void test_zero_sentinels(void) {
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, kR1, 0));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 0, kR1, kR2));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(kR1, 0));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_vcc_mv(0));
}

// --- VCC --------------------------------------------------------------

static void test_vcc_conversion(void) {
    // 1100 * 1024 = 1126400, rounded to nearest.
    TEST_ASSERT_EQUAL_UINT32(1126400UL, RURP_BANDGAP_NOMINAL_MV * RURP_ADC_FULL_SCALE);
    TEST_ASSERT_EQUAL_UINT16(5006, rurp_scale_vcc_mv(225));
    TEST_ASSERT_EQUAL_UINT16(1100, rurp_scale_vcc_mv(1024));
}

static void test_vcc_readout_measures_the_bandgap_discrepancy(void) {
    // The reported supply is supply * 1100 / bandgap_true, so a board that
    // cannot physically run at the reported figure is reporting a bandgap
    // error, not a supply fault. A true 5000 mV supply on a part whose real
    // bandgap is 1000 mV reads 5500 mV: the count is 1000/5000*1024 = 204.8,
    // and at 205 counts the conversion returns the impossible figure.
    TEST_ASSERT_EQUAL_UINT16(5495, rurp_scale_vcc_mv(205));
    // The same part over-reads the divider by the same ratio. At a true
    // 12000 mV rail the divider sees 12000 / 7.1364 = 1681.5 mV, which
    // against a real 1000 mV bandgap is 1681.5 / 5000 * 1024 = 344 counts.
    // The firmware reads that as 13173 mV -- 9.8 % high, which is above the
    // +500 mV hard error threshold for a 12000 mV target and is why a
    // correctly set rail can refuse to program.
    TEST_ASSERT_EQUAL_UINT16(13173, rurp_scale_voltage_mv(344, 205, kR1, kR2));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_shipped_defaults_match_the_header);
    RUN_TEST(test_scale_factor_is_exact_at_the_shipped_calibration);
    RUN_TEST(test_named_single_reading);
    RUN_TEST(test_bit_identity_over_the_full_bandgap_range);
    RUN_TEST(test_worst_deviation_is_five_and_never_over_reads);
    RUN_TEST(test_divider_sum_guard_boundary);
    RUN_TEST(test_scale_factor_guard_boundary);
    RUN_TEST(test_scale_guard_keeps_the_product_inside_thirty_two_bits);
    RUN_TEST(test_zero_sentinels);
    RUN_TEST(test_vcc_conversion);
    RUN_TEST(test_vcc_readout_measures_the_bandgap_discrepancy);
    return UNITY_END();
}
