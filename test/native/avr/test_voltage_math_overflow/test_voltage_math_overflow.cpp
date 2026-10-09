/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Overflow behaviour of src/rurp_voltage_math.c at the edges of the allowed
 * input range: the largest bandgap with the largest divider sum, an ADC at
 * full scale, and a meter reading large enough to wrap a uint32 product.
 */

#include <unity.h>

#include <stdint.h>

#include "rurp_voltage_math.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_max_bandgap_with_a_huge_divider_sum_is_refused(void) {
    // 1200 mV * 3.9 M = 4.68e9, which wraps a uint32. It must be refused,
    // not evaluated on a wrapped product.
    const uint32_t r2 = 44000;
    const uint32_t r1 = RURP_DIVIDER_SUM_MAX - r2;
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(r1, r2, RURP_BANDGAP_MAX_MV));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_scale_voltage_mv(512, 225, r1, r2, RURP_BANDGAP_MAX_MV));
}

static void test_max_bandgap_boundary_of_the_sum(void) {
    // The largest sum whose product with the largest bandgap fits in uint32.
    const uint32_t r2 = 44000;
    const uint32_t sum_ok = 0xFFFFFFFFUL / RURP_BANDGAP_MAX_MV;
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(sum_ok - r2, r2, RURP_BANDGAP_MAX_MV));
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(sum_ok + 1 - r2, r2, RURP_BANDGAP_MAX_MV));
}

static void test_nominal_bandgap_still_accepts_the_sum_bound(void) {
    // The refusal above must not tighten the nominal-bandgap behaviour.
    const uint32_t r2 = 44000;
    TEST_ASSERT_EQUAL_UINT8(1, rurp_calibration_is_plausible(RURP_DIVIDER_SUM_MAX - r2, r2,
                                                              RURP_BANDGAP_NOMINAL_MV));
}

static void test_sum_wraparound_is_refused(void) {
    TEST_ASSERT_EQUAL_UINT8(0, rurp_calibration_is_plausible(0xFFFFFFFFUL, 44000, RURP_BANDGAP_NOMINAL_MV));
}

static void test_scale_factor_bound_follows_the_adc_width(void) {
    // adc at full scale times k at the bound must fit in uint32.
    uint64_t product = (uint64_t)(RURP_ADC_FULL_SCALE - 1) * (uint64_t)RURP_SCALE_K_MAX;
    TEST_ASSERT_TRUE(product <= 0xFFFFFFFFULL);
}

static void test_scale_voltage_saturates_instead_of_wrapping(void) {
    // A tiny bandgap count makes the quotient exceed 16 bits. It must read as
    // a very high rail, never wrap to a small value.
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, rurp_scale_voltage_mv(1023, 1, 270000, 44000, RURP_BANDGAP_NOMINAL_MV));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, rurp_scale_vcc_mv(1, RURP_BANDGAP_NOMINAL_MV));
}

static void test_back_solve_refuses_a_product_that_wraps_uint32(void) {
    // 4294967 mV * 1023 counts is over 2^32.
    TEST_ASSERT_EQUAL_UINT16(0, rurp_bandgap_from_measured_vcc(0xFFFFFFFFUL, 1023, 1024));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_bandgap_from_measured_vcc(4200000UL, 1023, 1024));
    TEST_ASSERT_EQUAL_UINT16(0, rurp_bandgap_from_measured_vcc(5000, 0xFFFFFFFFUL, 1024));
}

static void test_back_solve_still_accepts_a_normal_reading(void) {
    TEST_ASSERT_EQUAL_UINT16(1024, rurp_bandgap_from_measured_vcc(5090, 206, 1024));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_max_bandgap_with_a_huge_divider_sum_is_refused);
    RUN_TEST(test_max_bandgap_boundary_of_the_sum);
    RUN_TEST(test_nominal_bandgap_still_accepts_the_sum_bound);
    RUN_TEST(test_sum_wraparound_is_refused);
    RUN_TEST(test_scale_factor_bound_follows_the_adc_width);
    RUN_TEST(test_scale_voltage_saturates_instead_of_wrapping);
    RUN_TEST(test_back_solve_refuses_a_product_that_wraps_uint32);
    RUN_TEST(test_back_solve_still_accepts_a_normal_reading);
    return UNITY_END();
}
