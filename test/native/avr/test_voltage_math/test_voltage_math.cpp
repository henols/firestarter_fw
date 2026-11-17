/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Native coverage for the ADC-count-to-millivolt arithmetic
 * (src/rurp_voltage_math.c).
 *
 * The properties are asserted against the shipped C itself, compiled and
 * executed. The arithmetic has no board dependency.
 *
 * The 64-bit reference form below is the exact-arithmetic form. It is a
 * reference implementation for an equivalence claim, not a second production
 * path.
 */

#include <unity.h>

#include <stdint.h>

#include "rurp_voltage_math.h"

#include "rurp_shield.h"

static const uint32_t kR1 = VALUE_R1;
static const uint32_t kR2 = VALUE_R2;

// The 64-bit form that rurp_scale_voltage_mv matches:
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

// The reference narrowed the way rurp_scale_voltage_mv narrows: a result above
// 65535 saturates to 65535.
static uint16_t reference_v64_saturated(uint32_t adc, uint32_t bandgap_adc, uint32_t r1, uint32_t r2) {
    uint32_t v = reference_v64(adc, bandgap_adc, r1, r2);
    return v > 0xFFFFUL ? 0xFFFF : (uint16_t)v;
}

void setUp(void) {}
void tearDown(void) {}

// --- The named reading ------------------------------------------------

static void test_named_single_reading(void) {
    // adc 1023 at bandgap count 225 gives 35691 mV.
    TEST_ASSERT_EQUAL_UINT16(35691, rurp_scale_voltage_mv(1023, 225, kR1, kR2, RURP_BANDGAP_NOMINAL_MV));
}

// --- Equivalence ------------------------------------------------------

static void test_bit_identity_over_the_full_bandgap_range(void) {
    // 0 mismatches over bandgap 1..1023 crossed with adc 0..1023 at the
    // shipped calibration. Both forms saturate at 65535, so the comparison
    // covers results above 65535 too.
    uint32_t mismatches = 0;
    for (uint32_t bg = 1; bg < 1024; bg++) {
        for (uint32_t adc = 0; adc < 1024; adc++) {
            uint16_t expected = reference_v64_saturated(adc, bg, kR1, kR2);
            if (rurp_scale_voltage_mv(adc, bg, kR1, kR2, RURP_BANDGAP_NOMINAL_MV) != expected) {
                mismatches++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, mismatches);
}

static void test_a_result_above_sixteen_bits_saturates(void) {
    // adc 1023 at bandgap count 1 gives 8,030,550 mV in the reference form.
    // The reading is meaningless; it saturates to 65535 so it still reads as
    // a high rail and never wraps to a small, plausible value.
    TEST_ASSERT_TRUE(reference_v64(1023, 1, kR1, kR2) > 0xFFFFUL);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, rurp_scale_voltage_mv(1023, 1, kR1, kR2, RURP_BANDGAP_NOMINAL_MV));
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
                uint32_t v32 = rurp_scale_voltage_mv(adc, bg, kR1, r2, RURP_BANDGAP_NOMINAL_MV);
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
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(r1_at, r2, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_NOT_EQUAL(0, rurp_scale_voltage_mv(512, 225, r1_at, r2, RURP_BANDGAP_NOMINAL_MV));

    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(r1_at + 1, r2, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, r1_at + 1, r2, RURP_BANDGAP_NOMINAL_MV));
}

static void test_scale_factor_guard_boundary(void) {
    // k exactly at the bound evaluates; one over refuses. Both pairs pass the
    // divider-sum guard first, so this isolates the second guard -- which is
    // only reachable at all when r2 is under about 938, since otherwise
    // RURP_DIVIDER_SUM_MAX caps the sum before k can grow.
    const uint32_t r2 = 900;
    const uint32_t r1_at = 3430803;   // r1 + r2 = 3431703 -> k == 4194303
    const uint32_t r1_over = 3430804; // r1 + r2 = 3431704 -> k == 4194304

    TEST_ASSERT_TRUE(r1_at + r2 <= RURP_DIVIDER_SUM_MAX);
    TEST_ASSERT_TRUE(r1_over + r2 <= RURP_DIVIDER_SUM_MAX);

    TEST_ASSERT_EQUAL_UINT32(RURP_SCALE_K_MAX,
                             (RURP_BANDGAP_NOMINAL_MV * (r1_at + r2)) / r2);
    TEST_ASSERT_EQUAL_UINT32(RURP_SCALE_K_MAX + 1,
                             (RURP_BANDGAP_NOMINAL_MV * (r1_over + r2)) / r2);

    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(r1_at, r2, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(r1_over, r2, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, r1_over, r2, RURP_BANDGAP_NOMINAL_MV));
}

// --- Zero sentinels ---------------------------------------------------

static void test_zero_sentinels(void) {
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, kR1, 0, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 0, kR1, kR2, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(kR1, 0, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_vcc_mv(0, RURP_BANDGAP_NOMINAL_MV));
}

// --- VCC --------------------------------------------------------------

static void test_vcc_conversion(void) {
    // 1100 * 1024 / count, rounded to nearest.
    TEST_ASSERT_EQUAL_UINT16(5006, rurp_scale_vcc_mv(225, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(1100, rurp_scale_vcc_mv(1024, RURP_BANDGAP_NOMINAL_MV));
}

static void test_vcc_readout_measures_the_bandgap_discrepancy(void) {
    // The reported supply is supply * 1100 / bandgap_true, so a board that
    // cannot physically run at the reported figure is reporting a bandgap
    // error, not a supply fault. A true 5000 mV supply on a part whose real
    // bandgap is 1000 mV reads 5500 mV: the count is 1000/5000*1024 = 204.8,
    // and at 205 counts the conversion returns the impossible figure.
    TEST_ASSERT_EQUAL_UINT16(5495, rurp_scale_vcc_mv(205, RURP_BANDGAP_NOMINAL_MV));
    // The same part over-reads the divider by the same ratio. At a true
    // 12000 mV rail the divider sees 12000 / 7.1364 = 1681.5 mV, which
    // against a real 1000 mV bandgap is 1681.5 / 5000 * 1024 = 344 counts.
    // The firmware reads that as 13173 mV -- 9.8 % high, which is above the
    // +500 mV hard error threshold for a 12000 mV target and is why a
    // correctly set rail can refuse to program.
    TEST_ASSERT_EQUAL_UINT16(13173, rurp_scale_voltage_mv(344, 205, kR1, kR2, RURP_BANDGAP_NOMINAL_MV));
}


// --- Calibration -------------------------------------------------------

static void test_nominal_bandgap_is_the_identity(void) {
    // A board that has never been calibrated stores the nominal, and must
    // produce byte-identical numbers to the pre-calibration firmware.
    TEST_ASSERT_EQUAL_UINT16(35691, rurp_scale_voltage_mv(1023, 225, kR1, kR2, 1100));
    TEST_ASSERT_EQUAL_UINT16(5006, rurp_scale_vcc_mv(225, 1100));
}

static void test_an_out_of_band_bandgap_is_never_used(void) {
    // The same band gates the conversion itself, so a calibration that
    // somehow reached storage still cannot produce a reading.
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(kR1, kR2, 999));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(kR1, kR2, 1201));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(344, 205, kR1, kR2, 999));
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(kR1, kR2, 1000));
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(kR1, kR2, 1200));
}

static void test_calibration_corrects_the_measured_leonardo(void) {
    // The case this whole milestone exists for. Uncalibrated, the leonardo
    // reads a true 12.0 V rail as 13173 mV -- above the +500 mV hard error
    // threshold, so it refuses to write. Calibrated to its measured 1024 mV
    // bandgap, the same ADC counts read within the window.
    uint16_t uncal = rurp_scale_voltage_mv(344, 205, kR1, kR2, 1100);
    uint16_t cal = rurp_scale_voltage_mv(344, 205, kR1, kR2, 1024);
    TEST_ASSERT_EQUAL_UINT16(13173, uncal);
    TEST_ASSERT_TRUE_MESSAGE(uncal > 12000 + 500, "precondition: uncalibrated trips the HIGH guard");
    TEST_ASSERT_TRUE_MESSAGE(cal <= 12000 + 500, "calibrated must clear the HIGH guard");
    TEST_ASSERT_TRUE_MESSAGE(cal >= 12000 * 95 / 100, "calibrated must clear the LOW guard");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_named_single_reading);
    RUN_TEST(test_bit_identity_over_the_full_bandgap_range);
    RUN_TEST(test_a_result_above_sixteen_bits_saturates);
    RUN_TEST(test_worst_deviation_is_five_and_never_over_reads);
    RUN_TEST(test_divider_sum_guard_boundary);
    RUN_TEST(test_scale_factor_guard_boundary);
    RUN_TEST(test_zero_sentinels);
    RUN_TEST(test_vcc_conversion);
    RUN_TEST(test_vcc_readout_measures_the_bandgap_discrepancy);
    RUN_TEST(test_nominal_bandgap_is_the_identity);
    RUN_TEST(test_an_out_of_band_bandgap_is_never_used);
    RUN_TEST(test_calibration_corrects_the_measured_leonardo);
    return UNITY_END();
}
