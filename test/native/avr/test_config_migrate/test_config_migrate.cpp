/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * rurp_config_migrate: the per-field configuration migration.
 *
 * The migration runs on every load, not only on a version mismatch. It keeps
 * the operator's hardware_revision override across a version change and
 * replaces each out-of-band field with its default. The return value is true
 * when the record changed and must be written back.
 */

#include <unity.h>

#include <string.h>

extern "C" {
#include "rurp_config_migrate.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"
}
#include "jsmn.h"
#include "json_parser.h"

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
    // The version string is already current, and the migration still corrects
    // an out-of-band r1.
    rurp_configuration_t c = good();
    c.r1 = 1000;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
}

static void test_an_operator_override_survives_a_version_bump(void) {
    // An old version string must migrate the version WITHOUT touching hardware_revision.
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
        TEST_ASSERT_EQUAL(!c1.kept, rurp_config_migrate(&c));
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
        TEST_ASSERT_EQUAL(!c2.kept, rurp_config_migrate(&c));
        if (c2.kept) {
            TEST_ASSERT_EQUAL_INT32(c2.v, c.r2);
        } else {
            TEST_ASSERT_EQUAL_INT32(VALUE_R2, c.r2);
        }
    }
}

// The bytes another sketch left at the configuration offset, with a 0x00
// where hardware_revision sits. 0x00 is REVISION_0, so keeping it would make
// the foreign byte a live shield override.
static void test_a_foreign_record_resets_every_field_and_the_override(void) {
    rurp_configuration_t c;
    memset(&c, 0, sizeof(c));
    const char foreign[sizeof(c.version)] = {'\x12', 'A', 'b', '\x00', '\x7F', '\x01'};
    memcpy(c.version, foreign, sizeof(c.version));
    c.r1 = VALUE_R1_MIN;  // in band, but foreign, so not trusted
    c.r2 = VALUE_R2_MAX;
    c.hardware_revision = 0x00;
    c.bandgap_mv = 1050;
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_STRING(CONFIG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, c.hardware_revision,
                                    "a foreign byte must not become a shield override");
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
    TEST_ASSERT_EQUAL_INT32(VALUE_R2, c.r2);
    TEST_ASSERT_EQUAL_UINT16(RURP_BANDGAP_NOMINAL_MV, c.bandgap_mv);
}

// A store that held no record leaves the zero-initialised global behind, and
// rurp_load_config() clears the version. hardware_revision 0 must not survive.
static void test_an_unloaded_zeroed_record_gets_no_override(void) {
    rurp_configuration_t c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_TRUE(rurp_config_migrate(&c));
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.hardware_revision);
    TEST_ASSERT_EQUAL_STRING(CONFIG_VERSION, c.version);
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, c.r1);
    TEST_ASSERT_EQUAL_INT32(VALUE_R2, c.r2);
    TEST_ASSERT_EQUAL_UINT16(RURP_BANDGAP_NOMINAL_MV, c.bandgap_mv);
}

static void test_only_ver_and_two_digits_is_a_firestarter_version(void) {
    const char* foreign[] = {"VERxx", "VER7", "ver07", "XXXXX", "VER0A", ""};
    for (unsigned i = 0; i < sizeof(foreign) / sizeof(foreign[0]); i++) {
        rurp_configuration_t c = good();
        memset(c.version, 0, sizeof(c.version));
        strncpy(c.version, foreign[i], sizeof(c.version) - 1);
        TEST_ASSERT_TRUE(rurp_config_migrate(&c));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, c.hardware_revision, foreign[i]);
    }
    // Non-vacuity: any other Firestarter version keeps the override.
    const char* ours[] = {"VER03", "VER06", "VER99"};
    for (unsigned i = 0; i < sizeof(ours) / sizeof(ours[0]); i++) {
        rurp_configuration_t c = good();
        strcpy(c.version, ours[i]);
        TEST_ASSERT_TRUE(rurp_config_migrate(&c));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, c.hardware_revision, ours[i]);
    }
}

static void test_an_unknown_revision_resets_to_no_override(void) {
    const uint8_t unknown[] = {REVISION_2_3 + 1, 0x80, REVISION_UNKNOWN};
    for (unsigned i = 0; i < sizeof(unknown); i++) {
        rurp_configuration_t c = good();
        c.hardware_revision = unknown[i];
        TEST_ASSERT_TRUE(rurp_config_migrate(&c));
        TEST_ASSERT_EQUAL_UINT8(0xFF, c.hardware_revision);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(VALUE_R1, c.r1, "the other fields stay");
    }
}

static void test_every_known_revision_is_kept(void) {
    const uint8_t known[] = {REVISION_0, REVISION_1, REVISION_2_0,
                             REVISION_2_1, REVISION_2_2, REVISION_2_3};
    for (unsigned i = 0; i < sizeof(known); i++) {
        rurp_configuration_t c = good();
        c.hardware_revision = known[i];
        TEST_ASSERT_FALSE(rurp_config_migrate(&c));
        TEST_ASSERT_EQUAL_UINT8(known[i], c.hardware_revision);
    }
}

static void test_validity_matches_the_bands_and_known_revisions(void) {
    rurp_configuration_t c = good();
    TEST_ASSERT_TRUE(rurp_config_is_valid(&c));
    c.hardware_revision = 0xFF;
    TEST_ASSERT_TRUE_MESSAGE(rurp_config_is_valid(&c), "no override is valid");

    struct { long r1; long r2; uint16_t bg; uint8_t rev; } bad[] = {
        {VALUE_R1_MIN - 1, VALUE_R2, 1100, 0xFF},
        {VALUE_R1_MAX + 1, VALUE_R2, 1100, 0xFF},
        {VALUE_R1, VALUE_R2_MIN - 1, 1100, 0xFF},
        {VALUE_R1, VALUE_R2_MAX + 1, 1100, 0xFF},
        {VALUE_R1, VALUE_R2, (uint16_t)(RURP_BANDGAP_MIN_MV - 1), 0xFF},
        {VALUE_R1, VALUE_R2, (uint16_t)(RURP_BANDGAP_MAX_MV + 1), 0xFF},
        {VALUE_R1, VALUE_R2, 1100, REVISION_2_3 + 1},
        {VALUE_R1, VALUE_R2, 1100, REVISION_UNKNOWN},
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        c = good();
        c.r1 = bad[i].r1;
        c.r2 = bad[i].r2;
        c.bandgap_mv = bad[i].bg;
        c.hardware_revision = bad[i].rev;
        TEST_ASSERT_FALSE(rurp_config_is_valid(&c));
    }
}

static void test_a_refused_commit_leaves_the_live_config_unchanged(void) {
    rurp_configuration_t live = good();
    rurp_configuration_t candidate = good();
    candidate.r1 = 390000;
    candidate.bandgap_mv = 1050;
    TEST_ASSERT_FALSE(rurp_config_commit(&live, &candidate));
    TEST_ASSERT_EQUAL_INT32(VALUE_R1, live.r1);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(RURP_BANDGAP_NOMINAL_MV, live.bandgap_mv,
                                     "no part of a refused candidate may land");

    candidate.r1 = 300000;
    TEST_ASSERT_TRUE(rurp_config_commit(&live, &candidate));
    TEST_ASSERT_EQUAL_INT32(300000, live.r1);
    TEST_ASSERT_EQUAL_UINT16(1050, live.bandgap_mv);
}

// The config command's flow without the calibration step: parse the frame
// into a copy of the live configuration, then commit the copy when a field
// changed. Returns false where the firmware answers MSG_ERR_PARSE_CFG.
static bool apply_config_frame(const char* json, rurp_configuration_t* live) {
    jsmntok_t tokens[NUMBER_JSNM_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, json, strlen(json), tokens, NUMBER_JSNM_TOKENS);
    TEST_ASSERT_TRUE_MESSAGE(token_count > 0, json);
    firestarter_handle_t handle = {};
    rurp_configuration_t copy = *live;
    int res = json_parse_config(json, tokens, token_count, &copy, &handle);
    if (res < 0) {
        return false;
    }
    if (res & JSON_CFG_FIELD_CHANGED) {
        return rurp_config_commit(live, &copy);
    }
    return true;
}

static void test_an_out_of_band_config_frame_is_refused_not_corrected(void) {
    const char* refused[] = {
        "{\"state\":14,\"r1\":390000}",
        "{\"state\":14,\"r1\":202499}",
        "{\"state\":14,\"r2\":55001}",
        "{\"state\":14,\"r2\":32999}",
        "{\"state\":14,\"bg\":999}",
        "{\"state\":14,\"bg\":1201}",
        "{\"state\":14,\"rev\":6}",
        "{\"state\":14,\"rev\":254}",
        // One good field does not carry a bad one.
        "{\"state\":14,\"r1\":300000,\"r2\":60000}",
        // A field the parser does not know refuses the frame part-way.
        "{\"state\":14,\"r1\":300000,\"zz\":1}",
        // A negative or oversized number is refused, never read as 0 or
        // wrapped into a value that passes the band check.
        "{\"state\":14,\"rev\":-1}",
        "{\"state\":14,\"rev\":256}",
        "{\"state\":14,\"r1\":-300000}",
        "{\"state\":14,\"r1\":4295267296}",
        "{\"state\":14,\"r2\":4295017296}",
        "{\"state\":14,\"bg\":66636}",
    };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        rurp_configuration_t live = good();
        const rurp_configuration_t before = live;
        TEST_ASSERT_FALSE_MESSAGE(apply_config_frame(refused[i], &live), refused[i]);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &live, sizeof(live), refused[i]);
    }
}

static void test_every_frame_the_host_sends_is_accepted(void) {
    struct { const char* json; long r1; long r2; uint8_t rev; uint16_t bg; } ok[] = {
        {"{\"state\":14}", VALUE_R1, VALUE_R2, 4, 1100},
        {"{\"state\":14,\"flags\":32}", VALUE_R1, VALUE_R2, 4, 1100},
        {"{\"state\":14,\"rev\":255}", VALUE_R1, VALUE_R2, 0xFF, 1100},
        {"{\"state\":14,\"rev\":0}", VALUE_R1, VALUE_R2, 0, 1100},
        {"{\"state\":14,\"rev\":5}", VALUE_R1, VALUE_R2, 5, 1100},
        {"{\"state\":14,\"r1\":202500,\"r2\":55000}", 202500, 55000, 4, 1100},
        {"{\"state\":14,\"rev\":2,\"r1\":337500,\"r2\":33000}", 337500, 33000, 2, 1100},
        {"{\"state\":14,\"bg\":1100}", VALUE_R1, VALUE_R2, 4, 1100},
        {"{\"state\":14,\"bg\":1000}", VALUE_R1, VALUE_R2, 4, 1000},
        {"{\"state\":14,\"bg\":1200}", VALUE_R1, VALUE_R2, 4, 1200},
    };
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        rurp_configuration_t live = good();
        TEST_ASSERT_TRUE_MESSAGE(apply_config_frame(ok[i].json, &live), ok[i].json);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(ok[i].r1, live.r1, ok[i].json);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(ok[i].r2, live.r2, ok[i].json);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(ok[i].rev, live.hardware_revision, ok[i].json);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ok[i].bg, live.bandgap_mv, ok[i].json);
    }
}

#if RURP_HAS_BANDGAP_CAL
// The measured supply voltage feeds the bandgap back-solve. A value above
// 10 V cannot be a supply reading, and a large one would overflow the
// back-solve product, so the frame is refused.
static void test_a_calibration_frame_with_an_implausible_vcc_is_refused(void) {
    const char* refused[] = {
        "{\"state\":14,\"vcc\":10001}",
        "{\"state\":14,\"vcc\":4199999}",
        "{\"state\":14,\"vcc\":-5000}",
    };
    for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        jsmntok_t tokens[NUMBER_JSNM_TOKENS];
        jsmn_parser parser;
        jsmn_init(&parser);
        int token_count = jsmn_parse(&parser, refused[i], strlen(refused[i]), tokens, NUMBER_JSNM_TOKENS);
        TEST_ASSERT_TRUE_MESSAGE(token_count > 0, refused[i]);
        firestarter_handle_t handle = {};
        rurp_configuration_t copy = good();
        TEST_ASSERT_TRUE_MESSAGE(json_parse_config(refused[i], tokens, token_count, &copy, &handle) < 0,
                                 refused[i]);
    }

    const char* ok = "{\"state\":14,\"vcc\":5000}";
    jsmntok_t tokens[NUMBER_JSNM_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, ok, strlen(ok), tokens, NUMBER_JSNM_TOKENS);
    firestarter_handle_t handle = {};
    rurp_configuration_t copy = good();
    int res = json_parse_config(ok, tokens, token_count, &copy, &handle);
    TEST_ASSERT_TRUE(res >= 0 && (res & JSON_CFG_CALIBRATE));
    TEST_ASSERT_EQUAL_INT32(5000, json_config_measured_vcc_mv());
}
#endif

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_current_config_is_left_alone);
    RUN_TEST(test_the_stranded_r1_is_corrected);
    RUN_TEST(test_an_operator_override_survives_a_version_bump);
    RUN_TEST(test_the_no_override_sentinel_also_survives);
    RUN_TEST(test_garbage_bandgap_from_an_old_layout_resets_to_nominal);
    RUN_TEST(test_a_real_calibration_is_never_discarded);
    RUN_TEST(test_the_r_bands_keep_plausible_values_and_reject_others);
    RUN_TEST(test_a_foreign_record_resets_every_field_and_the_override);
    RUN_TEST(test_an_unloaded_zeroed_record_gets_no_override);
    RUN_TEST(test_only_ver_and_two_digits_is_a_firestarter_version);
    RUN_TEST(test_an_unknown_revision_resets_to_no_override);
    RUN_TEST(test_every_known_revision_is_kept);
    RUN_TEST(test_validity_matches_the_bands_and_known_revisions);
    RUN_TEST(test_a_refused_commit_leaves_the_live_config_unchanged);
    RUN_TEST(test_an_out_of_band_config_frame_is_refused_not_corrected);
    RUN_TEST(test_every_frame_the_host_sends_is_accepted);
#if RURP_HAS_BANDGAP_CAL
    RUN_TEST(test_a_calibration_frame_with_an_implausible_vcc_is_refused);
#endif
    return UNITY_END();
}
