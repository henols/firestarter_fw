/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Native Unity tests for the "rev" key of a CMD_CONFIG command.
 *
 * The value is the hardware-revision override that parse_json saves to
 * EEPROM. Only REVISION_0..REVISION_2_3 and 0xFF (no override) are stored.
 * Every other value makes json_parse_config return JSON_CONFIG_INVALID_REV
 * and leaves config->hardware_revision unchanged.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "json_parser.h"
#include "jsmn.h"
}
#include "firestarter.h"

using namespace fakeit;

/* A value that no case below stores, so "unchanged" is observable. */
#define SENTINEL_REVISION 0x77

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {}

static int parse_config(const char* json_str, rurp_configuration_t* config) {
    jsmntok_t tokens[NUMBER_JSNM_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, json_str, strlen(json_str),
                                 tokens, NUMBER_JSNM_TOKENS);
    TEST_ASSERT_GREATER_THAN_INT(0, token_count);
    firestarter_handle_t handle = {};
    return json_parse_config(json_str, tokens, token_count, config, &handle);
}

static rurp_configuration_t make_config(void) {
    rurp_configuration_t config = {};
    config.hardware_revision = SENTINEL_REVISION;
    return config;
}

static void assert_stored(const char* json, uint8_t expected) {
    rurp_configuration_t config = make_config();
    TEST_ASSERT_EQUAL_INT(1, parse_config(json, &config));
    TEST_ASSERT_EQUAL_HEX8(expected, config.hardware_revision);
}

static void assert_refused(const char* json) {
    rurp_configuration_t config = make_config();
    TEST_ASSERT_EQUAL_INT(JSON_CONFIG_INVALID_REV, parse_config(json, &config));
    TEST_ASSERT_EQUAL_HEX8(SENTINEL_REVISION, config.hardware_revision);
}

void test_rev_0_is_stored(void) {
    assert_stored("{\"state\":14,\"rev\":0}", REVISION_0);
}

void test_rev_2_2_byte_is_stored(void) {
    assert_stored("{\"state\":14,\"rev\":4}", REVISION_2_2);
}

/* Boundary: 5 is the highest revision byte. */
void test_rev_5_is_stored(void) {
    assert_stored("{\"state\":14,\"rev\":5}", REVISION_2_3);
}

/* 0xFF removes the override. */
void test_rev_255_is_stored(void) {
    assert_stored("{\"state\":14,\"rev\":255}", 0xFF);
}

/* Boundary: one above REVISION_2_3. */
void test_rev_6_is_refused(void) {
    assert_refused("{\"state\":14,\"rev\":6}");
}

/* REVISION_UNKNOWN is a detection result, never an override. */
void test_rev_254_is_refused(void) {
    assert_refused("{\"state\":14,\"rev\":254}");
}

void test_rev_256_is_refused(void) {
    assert_refused("{\"state\":14,\"rev\":256}");
}

/* 260 truncates to 4 (Rev 2.2) in a uint8_t store. */
void test_rev_260_is_refused_not_truncated_to_rev_2_2(void) {
    assert_refused("{\"state\":14,\"rev\":260}");
}

/* simple_strtoul reads "-1" as 0 (Rev 0). */
void test_rev_minus_1_is_refused_not_read_as_rev_0(void) {
    assert_refused("{\"state\":14,\"rev\":-1}");
}

void test_rev_as_string_is_refused(void) {
    assert_refused("{\"state\":14,\"rev\":\"4\"}");
}

void test_rev_with_fraction_is_refused(void) {
    assert_refused("{\"state\":14,\"rev\":2.2}");
}

/* A config command with no rev key does not touch the revision. */
void test_r1_only_leaves_revision_unchanged(void) {
    rurp_configuration_t config = make_config();
    TEST_ASSERT_EQUAL_INT(1, parse_config("{\"state\":14,\"r1\":249000}", &config));
    TEST_ASSERT_EQUAL_HEX8(SENTINEL_REVISION, config.hardware_revision);
    TEST_ASSERT_EQUAL_INT32(249000, config.r1);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_rev_0_is_stored);
    RUN_TEST(test_rev_2_2_byte_is_stored);
    RUN_TEST(test_rev_5_is_stored);
    RUN_TEST(test_rev_255_is_stored);
    RUN_TEST(test_rev_6_is_refused);
    RUN_TEST(test_rev_254_is_refused);
    RUN_TEST(test_rev_256_is_refused);
    RUN_TEST(test_rev_260_is_refused_not_truncated_to_rev_2_2);
    RUN_TEST(test_rev_minus_1_is_refused_not_read_as_rev_0);
    RUN_TEST(test_rev_as_string_is_refused);
    RUN_TEST(test_rev_with_fraction_is_refused);
    RUN_TEST(test_r1_only_leaves_revision_unchanged);
    return UNITY_END();
}
