/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * rurp_config_migrate: the per-field migration that replaced the
 * version-gated wipe.
 *
 * Two defects motivate every case below. The old policy reset
 * hardware_revision on any version change, destroying an operator's shield
 * override. And it only ever ran on a version MISMATCH, so a board carrying
 * the pre-Phase-44 r1 of 1000 under a matching version string kept that value
 * forever -- the stranding defect in
 * .planning/todos/pending/config-version-not-bumped-strands-stale-eeprom-calibration.md
 */

#include <unity.h>

#include <string.h>

extern "C" {
#include "rurp_config_migrate.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"
}

void setUp(void) {}
void tearDown(void) {}

// A configuration that needs no migration: current version, everything in band.
static rurp_configuration_t good(void) {
    rurp_configuration_t c;
    memset(&c, 0, sizeof(c));
    strcpy(c.version, CONFIG_VERSION);
    c.r1 = VALUE_R1;
    c.r2 = VALUE_R2;
    c.hardware_revision = 4;  // an operator override, not the 0xFF sentinel
    c.bandgap_mv = (uint16_t)RURP_BANDGAP_NOMINAL_MV;
    return c;
}

static void test_a_current_config_is_left_alone(void) {
    rurp_configuration_t c = good();
    TEST_ASSERT_FALSE_MESSAGE(rurp_config_migrate(&c), "nothing to change, so no write");
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
    TEST_ASSERT_EQUAL_UINT8(4, c.hardware_revision);
}

static void test_the_stranded_r1_is_corrected(void) {
    // The stranding defect, reproduced: version string ALREADY current, so the
    // old version gate could never have reached this value.
    rurp_configuration_t c = good();
    c.r1 = 1000;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
}

static void test_an_operator_override_survives_a_version_bump(void) {
    // The regression the old wipe caused. An old version string must migrate
    // the version WITHOUT touching hardware_revision.
    rurp_configuration_t c = good();
    strcpy(c.version, "VER06");
    c.hardware_revision = 4;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_STRING(CONFIG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, c.hardware_revision,
                                    "a version bump must not wipe the shield override");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(VALUE_R1, c.r1, "an in-band r1 is kept, not reset");
}

static void test_the_no_override_sentinel_also_survives(void) {
    rurp_configuration_t c = good();
    c.hardware_revision = 0xFF;
    strcpy(c.version, "VER06");
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.hardware_revision);
}

static void test_garbage_bandgap_from_an_old_layout_resets_to_nominal(void) {
    // An old record is two bytes short, so bandgap_mv reads whatever followed
    // it in EEPROM. Any out-of-band value must become the nominal, which is
    // the identity and changes no reading.
    rurp_configuration_t c = good();
    c.bandgap_mv = 0xFFFF;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_UINT16(RURP_BANDGAP_NOMINAL_MV, c.bandgap_mv);

    c = good();
    c.bandgap_mv = 0;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_UINT16(RURP_BANDGAP_NOMINAL_MV, c.bandgap_mv);
}

static void test_a_real_calibration_is_never_discarded(void) {
    // Non-vacuity for the case above: the band must keep a genuine
    // calibration, including at both edges. The leonardo's measured 1024 mV
    // is the case this milestone exists to store.
    const uint16_t in_band[] = {(uint16_t)RURP_BANDGAP_MIN_MV, 1024, 1050,
                                (uint16_t)RURP_BANDGAP_MAX_MV};
    for (unsigned i = 0; i < sizeof(in_band) / sizeof(in_band[0]); i++) {
        const uint16_t bg = in_band[i];
        rurp_configuration_t c = good();
        c.bandgap_mv = bg;
        TEST_ASSERT_FALSE_MESSAGE(rurp_config_migrate(&c), "an in-band calibration needs no write");
        TEST_ASSERT_EQUAL_UINT16(bg, c.bandgap_mv);
    }
}

static void test_the_r_bands_keep_plausible_values_and_reject_others(void) {
    struct { long v; bool kept; } r1_cases[] = {
        {VALUE_R1_MIN, true}, {VALUE_R1_MAX, true},
        {VALUE_R1_MIN - 1, false}, {VALUE_R1_MAX + 1, false},
    };
    for (unsigned i = 0; i < 4; i++) {
        const auto& c1 = r1_cases[i];
        rurp_configuration_t c = good();
        c.r1 = c1.v;
        rurp_config_migrate(&c);
        if (c1.kept) {
            TEST_ASSERT_EQUAL_INT32(c1.v, c.r1);
        } else {
            TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
        }
    }
    struct { long v; bool kept; } r2_cases[] = {
        {VALUE_R2_MIN, true}, {VALUE_R2_MAX, true},
        {VALUE_R2_MIN - 1, false}, {VALUE_R2_MAX + 1, false},
    };
    for (unsigned i = 0; i < 4; i++) {
        const auto& c2 = r2_cases[i];
        rurp_configuration_t c = good();
        c.r2 = c2.v;
        rurp_config_migrate(&c);
        if (c2.kept) {
            TEST_ASSERT_EQUAL_INT32(c2.v, c.r2);
        } else {
            TEST_ASSERT_EQUAL_INT32(VALUE_R2, c.r2);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_current_config_is_left_alone);
    RUN_TEST(test_the_stranded_r1_is_corrected);
    RUN_TEST(test_an_operator_override_survives_a_version_bump);
    RUN_TEST(test_the_no_override_sentinel_also_survives);
    RUN_TEST(test_garbage_bandgap_from_an_old_layout_resets_to_nominal);
    RUN_TEST(test_a_real_calibration_is_never_discarded);
    RUN_TEST(test_the_r_bands_keep_plausible_values_and_reject_others);
    return UNITY_END();
}
