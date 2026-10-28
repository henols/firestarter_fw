/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * JSON parser hardening cases: numeric overflow, nested values, token
 * lookahead and bus-config line validation. main() in
 * test_read_timing_params.cpp runs them through run_json_hardening_tests().
 */

#include <Arduino.h>
#include <unity.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "json_parser.h"
#include "jsmn.h"
}
#include "firestarter.h"

static jsmntok_t tokens[NUMBER_JSNM_TOKENS];

static int tokenize(const char* json) {
    jsmn_parser parser;
    jsmn_init(&parser);
    return jsmn_parse(&parser, json, strlen(json), tokens, NUMBER_JSNM_TOKENS);
}

static int parse(const char* json, firestarter_handle_t* h) {
    int n = tokenize(json);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "test JSON must tokenize");
    return json_parse(json, tokens, n, h);
}

static uint8_t get_cmd(const char* json, firestarter_handle_t* h) {
    int n = tokenize(json);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "test JSON must tokenize");
    return json_get_cmd(json, tokens, n, h);
}

static int parse_config(const char* json, rurp_configuration_t* config,
                        firestarter_handle_t* h) {
    int n = tokenize(json);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "test JSON must tokenize");
    return json_parse_config(json, tokens, n, config, h);
}

/* ---- henols/firestarter#130: numeric overflow ---- */

void test_overflow_uint32_max_is_accepted(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse("{\"cmd\":2,\"memory-size\":4294967295}", &h));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, h.mem_size);
    TEST_ASSERT_EQUAL_INT(0, parse("{\"cmd\":2,\"memory-size\":0}", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.mem_size);
}

void test_overflow_uint32_max_plus_one_is_refused(void) {
    firestarter_handle_t h = {};
    /* 4294967296 wraps to 0, 4294967300 to 4 and 42949672960 to 0. */
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"memory-size\":4294967296}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"pulse-delay\":4294967300}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"address\":42949672960}", &h));
}

void test_overflow_long_decimal_string_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"vpp_mv\":123456789012345678901234567890}", &h));
}

void test_overflow_on_a_saturating_field_is_refused_not_saturated(void) {
    firestarter_handle_t h = {};
    /* A value that fits 32 bits still saturates to the field width. */
    TEST_ASSERT_EQUAL_INT(0, parse("{\"cmd\":2,\"algorithm\":4294967295}", &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.protocol);
    /* 4294967301 wraps to 5 (PROTO_FLASH_5V_PAGE): refuse the frame. */
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"algorithm\":4294967301}", &h));
}

void test_overflow_flag_does_not_leak_into_the_next_frame(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"chip-id\":99999999999}", &h));
    TEST_ASSERT_EQUAL_INT(0, parse("{\"cmd\":2,\"chip-id\":4660}", &h));
    TEST_ASSERT_EQUAL_UINT16(4660, h.chip_id);
}

void test_overflow_in_cmd_or_flags_gives_no_command(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_READ, get_cmd("{\"cmd\":1,\"flags\":5}", &h));
    TEST_ASSERT_EQUAL_UINT16(5, h.ctrl_flags);
    /* 4294967297 wraps to 1 = CMD_READ. */
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"cmd\":4294967297}", &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"cmd\":1,\"flags\":4294967296}", &h));
    /* 257 fits 32 bits but not the command byte: it must not become 1. */
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"cmd\":257}", &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"state\":255}", &h));
}

void test_overflow_in_a_config_frame_is_refused(void) {
    firestarter_handle_t h = {};
    rurp_configuration_t config = {};
    TEST_ASSERT_EQUAL_INT(0, parse_config("{\"state\":14,\"flags\":3}", &config, &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_config("{\"state\":14,\"flags\":4294967299}", &config, &h));
}

/* ---- henols/firestarter#124: nested values and token lookahead ---- */

void test_nested_unknown_object_is_skipped_whole(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse(
        "{\"cmd\":2,\"extra\":{\"address\":42,\"flags\":7},\"page-size\":128}", &h));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "a key nested in an unknown value must not be read as a top-level field");
    TEST_ASSERT_EQUAL_UINT16(0, h.ctrl_flags);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(128, h.page_size,
        "the known key after the nested value must still be parsed");
}

void test_nested_unknown_array_and_deep_nesting_are_skipped(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse(
        "{\"x\":[1,{\"memory-size\":5}],\"y\":{\"z\":{\"w\":[[[\"chip-id\",9]]]}},"
        "\"memory-size\":64,\"chip-id\":4660}", &h));
    TEST_ASSERT_EQUAL_UINT32(64, h.mem_size);
    TEST_ASSERT_EQUAL_UINT16(4660, h.chip_id);
}

void test_known_numeric_key_with_a_nested_value_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"address\":{\"x\":1}}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"memory-size\":[65536]}", &h));
}

void test_truncated_token_list_is_refused(void) {
    firestarter_handle_t h = {};
    const char* json = "{\"cmd\":2,\"memory-size\":65536}";
    int n = tokenize(json);
    TEST_ASSERT_EQUAL_INT(5, n);
    /* The last value is cut off: no lookahead past the token count. */
    TEST_ASSERT_EQUAL_INT(-1, json_parse(json, tokens, n - 1, &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, json_get_cmd(json, tokens, n - 1, &h));
    TEST_ASSERT_EQUAL_INT(0, json_parse(json, tokens, n, &h));
}

void test_cmd_is_matched_only_as_a_top_level_key(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_READ, get_cmd("{\"note\":\"cmd\",\"cmd\":1}", &h));
    TEST_ASSERT_EQUAL_UINT8(CMD_READ, get_cmd("{\"cmd\":1,\"extra\":{\"cmd\":3}}", &h));
    TEST_ASSERT_EQUAL_UINT8(CMD_READ, get_cmd("{\"extra\":{\"cmd\":3,\"flags\":9},\"cmd\":1}", &h));
    TEST_ASSERT_EQUAL_UINT16(0, h.ctrl_flags);
    /* A string value "cmd" as the last token must not read past the list. */
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"note\":\"cmd\"}", &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("{\"flags\":1}", &h));
    TEST_ASSERT_EQUAL_UINT8(0xFF, get_cmd("[\"cmd\",1]", &h));
}

void test_bus_config_must_be_an_object(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"bus-config\":5,\"page-size\":64}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"bus-config\":[0,1]}", &h));
}

void test_bus_config_skips_a_nested_unknown_value(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse(
        "{\"cmd\":2,\"bus-config\":{\"extra\":{\"bus\":[5],\"rw-pin\":3},"
        "\"bus\":[0,1]},\"page-size\":64}", &h));
    TEST_ASSERT_EQUAL_UINT8(0, h.bus_config.address_lines[0]);
    TEST_ASSERT_EQUAL_UINT8(1, h.bus_config.address_lines[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.address_lines[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.rw_line);
    TEST_ASSERT_EQUAL_UINT16(64, h.page_size);
}

void test_bus_array_with_a_nested_element_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"bus-config\":{\"bus\":[0,[1],2]}}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse("{\"cmd\":2,\"bus-config\":{\"static-high\":[{\"a\":1}]}}", &h));
}

void test_config_frame_refuses_a_nested_value(void) {
    firestarter_handle_t h = {};
    rurp_configuration_t config = {};
    TEST_ASSERT_EQUAL_INT(-1, parse_config("{\"state\":14,\"r1\":{\"r2\":40000}}", &config, &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_config("{\"state\":14,\"flags\":[1]}", &config, &h));
}

/* ---- henols/firestarter#115: bus-config lines ---- */

static int parse_bus(const char* bus_config, firestarter_handle_t* h) {
    static char json[256];
    snprintf(json, sizeof(json), "{\"cmd\":2,\"bus-config\":%s}", bus_config);
    return parse(json, h);
}

void test_bus_line_outside_the_bus_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[32]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[0,1,24]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[255]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[256]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[-1]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[true]}", &h));
    TEST_ASSERT_EQUAL_INT(0, parse_bus("{\"bus\":[0,23]}", &h));
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)1 << 23) | 1, h.bus_config.address_mask);
    TEST_ASSERT_EQUAL_UINT8(23, h.bus_config.address_lines[1]);
}

void test_bus_duplicate_line_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[0,1,1]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"bus\":[5,0,5]}", &h));
}

void test_bus_longer_than_the_address_line_table_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse_bus(
        "{\"bus\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19]}", &h));
    TEST_ASSERT_EQUAL_UINT8(20, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFF, h.bus_config.address_mask);
    TEST_ASSERT_EQUAL_INT(-1, parse_bus(
        "{\"bus\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]}", &h));
}

void test_static_high_line_outside_the_bus_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"static-high\":[32]}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"static-high\":[13,24]}", &h));
    TEST_ASSERT_EQUAL_INT(0, parse_bus("{\"static-high\":[13,23]}", &h));
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)1 << 23) | ((uint32_t)1 << 13),
                             h.bus_config.static_high_mask);
}

void test_rw_and_vpp_line_outside_the_bus_is_refused(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"rw-pin\":32,\"vpp-pin\":32}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"rw-pin\":24}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"vpp-pin\":24}", &h));
    /* 256 truncated to rw line 0 (A0) before this fix. */
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"rw-pin\":256}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"vpp-pin\":-1}", &h));
    TEST_ASSERT_EQUAL_INT(-1, parse_bus("{\"vpp-pin\":[21]}", &h));
    TEST_ASSERT_EQUAL_INT(0, parse_bus("{\"rw-pin\":23,\"vpp-pin\":0}", &h));
    TEST_ASSERT_EQUAL_UINT8(23, h.bus_config.rw_line);
    TEST_ASSERT_EQUAL_UINT8(0, h.bus_config.vpp_line);
}

void test_bus_array_cut_off_by_the_token_count_is_refused(void) {
    firestarter_handle_t h = {};
    const char* json = "{\"cmd\":2,\"bus-config\":{\"bus\":[0,1,2]}}";
    int n = tokenize(json);
    TEST_ASSERT_EQUAL_INT(10, n);
    /* The array claims three elements; only the first is inside the list. */
    TEST_ASSERT_EQUAL_INT(-1, json_parse(json, tokens, n - 2, &h));
    TEST_ASSERT_EQUAL_INT(0, json_parse(json, tokens, n, &h));
}

/* The bus-configs a 3.2.4 host sends for a 32-pin 27C020 and a 24-pin 2716
 * map exactly as before. */
void test_host_bus_configs_map_as_before(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_INT(0, parse_bus(
        "{\"bus\": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20],"
        " \"rw-pin\": 22, \"vpp-pin\": 21}", &h));
    TEST_ASSERT_EQUAL_UINT8(17, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0x11FFFF, h.bus_config.address_mask);
    TEST_ASSERT_EQUAL_UINT8(20, h.bus_config.address_lines[17]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.address_lines[18]);
    TEST_ASSERT_EQUAL_UINT8(22, h.bus_config.rw_line);
    TEST_ASSERT_EQUAL_UINT8(21, h.bus_config.vpp_line);
    TEST_ASSERT_EQUAL_UINT32(0, h.bus_config.static_high_mask);

    TEST_ASSERT_EQUAL_INT(0, parse_bus(
        "{\"bus\": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10], \"vpp-pin\": 11,"
        " \"static-high\": [13]}", &h));
    TEST_ASSERT_EQUAL_UINT8(11, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0x7FF, h.bus_config.address_mask);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.rw_line);
    TEST_ASSERT_EQUAL_UINT8(11, h.bus_config.vpp_line);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)1 << 13, h.bus_config.static_high_mask);
}

void run_json_hardening_tests(void) {
    RUN_TEST(test_overflow_uint32_max_is_accepted);
    RUN_TEST(test_overflow_uint32_max_plus_one_is_refused);
    RUN_TEST(test_overflow_long_decimal_string_is_refused);
    RUN_TEST(test_overflow_on_a_saturating_field_is_refused_not_saturated);
    RUN_TEST(test_overflow_flag_does_not_leak_into_the_next_frame);
    RUN_TEST(test_overflow_in_cmd_or_flags_gives_no_command);
    RUN_TEST(test_overflow_in_a_config_frame_is_refused);
    RUN_TEST(test_nested_unknown_object_is_skipped_whole);
    RUN_TEST(test_nested_unknown_array_and_deep_nesting_are_skipped);
    RUN_TEST(test_known_numeric_key_with_a_nested_value_is_refused);
    RUN_TEST(test_truncated_token_list_is_refused);
    RUN_TEST(test_cmd_is_matched_only_as_a_top_level_key);
    RUN_TEST(test_bus_config_must_be_an_object);
    RUN_TEST(test_bus_config_skips_a_nested_unknown_value);
    RUN_TEST(test_bus_array_with_a_nested_element_is_refused);
    RUN_TEST(test_config_frame_refuses_a_nested_value);
    RUN_TEST(test_bus_line_outside_the_bus_is_refused);
    RUN_TEST(test_bus_duplicate_line_is_refused);
    RUN_TEST(test_bus_longer_than_the_address_line_table_is_refused);
    RUN_TEST(test_static_high_line_outside_the_bus_is_refused);
    RUN_TEST(test_rw_and_vpp_line_outside_the_bus_is_refused);
    RUN_TEST(test_bus_array_cut_off_by_the_token_count_is_refused);
    RUN_TEST(test_host_bus_configs_map_as_before);
}
