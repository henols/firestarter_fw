/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Wave 0 native Unity tests for the two host-tunable
 * read-timing knobs: `read_settling_us` and `read_strobe_us`.
 *
 * RED state before Task 2: these tests will FAIL TO COMPILE because the
 * `read_settling_us` and `read_strobe_us` fields do not yet exist on
 * `firestarter_handle_t`. That compile error IS the RED gate.
 *
 * Wave 1 (Task 2) adds the fields, PROGMEM parser entries, and
 * memory_get_data() instrumentation — flipping this suite GREEN.
 *
 * Test coverage:
 *   T1 — "read-settling-delay" JSON key → handle.read_settling_us stored
 *   T2 — "read-strobe-us" JSON key      → handle.read_strobe_us stored
 *   T3 — absent keys                   → both fields default to 0
 *   T4 — value above cap (T-44-01)     → read_settling_us clamped to cap
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "json_parser.h"
#include "jsmn.h"
#include "memory.h"
}
#include "firestarter.h"

using namespace fakeit;

/* Maximum allowed value for read-timing knobs (T-44-01 / RESEARCH §Security
 * Domain). Mirrors the cap defined in memory.cpp / json_parser.c.
 * rather than removed. */
#define READ_TIMING_MAX_US 1000UL

void setUp(void) {
    ArduinoFakeReset();
    /* Stub Serial.write and Serial.flush so LOG_ERROR_ID_* calls in any
     * transitive parse path don't abort. Tests never assert on serial. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {}

/* Build a zero-initialized handle suitable for JSON parse tests. */
static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};   /* zero-init: ensures new fields default 0 */
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    return h;
}

static int parse_json(const char* json_str, firestarter_handle_t* handle) {
    jsmntok_t tokens[NUMBER_JSNM_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, json_str, strlen(json_str),
                                 tokens, NUMBER_JSNM_TOKENS);
    if (token_count < 0) return token_count;
    return json_parse(json_str, tokens, token_count, handle);
}

/* T1: "read-settling-delay":50 → handle.read_settling_us == 50 */
void test_read_settling_us_parsed_from_json(void) {
    const char* json = "{\"cmd\":1,\"read-settling-delay\":50}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(50, h.read_settling_us);
}

/* T2: "read-strobe-us":25 → handle.read_strobe_us == 25 */
void test_read_strobe_us_parsed_from_json(void) {
    const char* json = "{\"cmd\":1,\"read-strobe-us\":25}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(25, h.read_strobe_us);
}

/* T3: absent timing keys → both fields remain 0 (zero-init default) */
void test_read_timing_fields_default_zero_when_absent(void) {
    const char* json = "{\"cmd\":1}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(0, h.read_settling_us);
    TEST_ASSERT_EQUAL_UINT32(0, h.read_strobe_us);
}

/* T4: value above cap → read_settling_us clamped to EXACTLY READ_TIMING_MAX_US
 * (T-44-01 mitigation: an absurd JSON value cannot hang the read loop).
 *
 * Equality, not an upper bound: 0 passes an upper-bound assertion, and 0 is
 * this knob's own loaded value ("no settling delay" is the explicit test
 * point for read_settling_us == 0). A clamp regression that stores 0 instead
 * of the cap would pass a "<=" assertion silently; only an equality catches
 * it. */
void test_read_settling_us_capped_at_max(void) {
    /* Use a value well above 1000µs to confirm the cap fires */
    const char* json = "{\"cmd\":1,\"read-settling-delay\":9999}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(READ_TIMING_MAX_US, h.read_settling_us,
        "read_settling_us must clamp to EXACTLY READ_TIMING_MAX_US -- T-44-01's "
        "mitigation for an absurd JSON value reaching delayMicroseconds() in the "
        "read loop; an equality, not an upper bound, because 0 passes an upper "
        "bound and 0 is this knob's own loaded value (\"no settling delay\")");
}

void test_read_strobe_us_capped_at_max(void) {
    const char* json = "{\"cmd\":1,\"read-strobe-us\":9999}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(READ_TIMING_MAX_US, h.read_strobe_us,
        "read_strobe_us must clamp to EXACTLY READ_TIMING_MAX_US -- T-44-01's "
        "mitigation for an absurd JSON value reaching delayMicroseconds() in the "
        "read loop; an equality, not an upper bound, because 0 passes an upper "
        "bound and 0 is this knob's own loaded value (\"use the firmware default "
        "of 3 microseconds\")");
}

/*
 * T5: "page-size":128 -> handle.page_size == 128 */
void test_page_size_parsed_from_json(void) {
    const char* json = "{\"cmd\":2,\"page-size\":128}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16(128, h.page_size);
}

/* T6: absent key -> handle.page_size stays 0 (fresh handle) */
void test_page_size_defaults_zero_when_absent(void) {
    const char* json = "{\"cmd\":2}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16(0, h.page_size);
}

void test_page_size_resets_between_two_parses_on_the_same_handle(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);

    int rc1 = parse_json("{\"cmd\":2,\"page-size\":128}", &h);
    TEST_ASSERT_EQUAL_INT(0, rc1);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(128, h.page_size,
        "first parse must store the delivered page-size");

    int rc2 = parse_json("{\"cmd\":2}", &h);
    TEST_ASSERT_EQUAL_INT(0, rc2);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "a stale 128 surviving into the next, page-size-absent command "
        "makes \"absent means 64\" false in practice -- the exact page "
        "overrun PGSZ-02 exists to prevent");
}

void test_unknown_key_before_a_known_key_does_not_desync_the_token_walk(void) {
    const char* json = "{\"cmd\":2,\"totally-unknown-key\":7,\"read-strobe-us\":25}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(25, h.read_strobe_us,
        "the known key after an unknown one must still be parsed -- a "
        "desynced token_idx would silently drop it");
}

void test_unknown_key_before_page_size_does_not_desync_the_token_walk(void) {
    const char* json = "{\"cmd\":2,\"totally-unknown-key\":7,\"page-size\":128}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16(128, h.page_size);
}

void test_out_of_range_algorithm_saturates_not_truncates(void) {
    const char* json = "{\"cmd\":1,\"algorithm\":261}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, h.protocol,
        "261 must saturate to 0xFF, not truncate to 0x05 -- 0x05 is "
        "PROTO_FLASH_5V_PAGE, a real handler configure_memory would "
        "dispatch into");
}

void test_out_of_range_algorithm_dispatch_fail_closes(void) {
    const char* json = "{\"cmd\":1,\"algorithm\":261}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "this is the case that would have caught the defect: the stored "
        "byte being right (S1) is not the same claim as the dispatch "
        "fail-closing -- a saturated 0xFF must reach configure_memory's "
        "generic fail-closed tail");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init,
        "fail-closed refusal must leave firestarter_operation_init NULL");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_main,
        "fail-closed refusal must leave firestarter_operation_main NULL");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_end,
        "fail-closed refusal must leave firestarter_operation_end NULL");
}

void test_in_range_algorithm_still_dispatches(void) {
    const char* json = "{\"cmd\":1,\"algorithm\":5}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, h.protocol,
        "a valid in-range algorithm must be stored unchanged");
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "this case exists so S1 and S2 cannot be satisfied by breaking "
        "every algorithm -- a valid protocol must still dispatch to a "
        "real handler, not fail-close");
}

void test_out_of_range_flags_masks_never_sets_every_flag(void) {
    const char* json = "{\"cmd\":2,\"flags\":65536}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "an out-of-range wire flags value must mask to the width-limited "
        "truncation, never saturate to 0xFFFF -- saturating a bitmask "
        "turns on every flag at once, a fail-open in a fail-closed phase");
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ctrl_flags & FLAG_FORCE,
        "FLAG_FORCE must not be set by an out-of-range flags value");
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ctrl_flags & FLAG_SKIP_ERASE,
        "FLAG_SKIP_ERASE must not be set by an out-of-range flags value");
    TEST_ASSERT_EQUAL_MESSAGE(0, h.ctrl_flags & FLAG_VPE_AS_VPP,
        "FLAG_VPE_AS_VPP must not be set by an out-of-range flags "
        "value");
}

void test_out_of_range_page_size_saturates_not_truncates_to_a_valid_size(void) {
    const char* json = "{\"cmd\":2,\"page-size\":65600}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, h.page_size,
        "65600 must saturate to 0xFFFF, not truncate to 64 -- 64 is a "
        "perfectly valid page size, which is what makes the hole silent");
}

/* memory-size -> handle->mem_size (uint32_t) */
void test_memory_size_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"memory-size\":65536}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(65536, h.mem_size,
        "a wrong offsetof in the memory-size row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "memory-size's row must not write into address");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "memory-size's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.chip_id,
        "memory-size's row must not write into chip_id");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.pins,
        "memory-size's row must not write into pins");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "memory-size's row must not write into pulse_delay");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.vpp_mv,
        "memory-size's row must not write into vpp_mv");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "memory-size's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "memory-size's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "memory-size's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "memory-size's row must not write into page_size");
}

/* address -> handle->address (uint32_t) */
void test_address_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"address\":4096}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4096, h.address,
        "a wrong offsetof in the address row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.mem_size,
        "address's row must not write into mem_size");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "address's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.chip_id,
        "address's row must not write into chip_id");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.pins,
        "address's row must not write into pins");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "address's row must not write into pulse_delay");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.vpp_mv,
        "address's row must not write into vpp_mv");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "address's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "address's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "address's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "address's row must not write into page_size");
}

/* pulse-delay -> handle->pulse_delay (uint32_t) */
void test_pulse_delay_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"pulse-delay\":1000}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, h.pulse_delay,
        "a wrong offsetof in the pulse-delay row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.mem_size,
        "pulse-delay's row must not write into mem_size");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "pulse-delay's row must not write into address");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "pulse-delay's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.chip_id,
        "pulse-delay's row must not write into chip_id");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.pins,
        "pulse-delay's row must not write into pins");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.vpp_mv,
        "pulse-delay's row must not write into vpp_mv");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "pulse-delay's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "pulse-delay's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "pulse-delay's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "pulse-delay's row must not write into page_size");
}

/* chip-id -> handle->chip_id (uint16_t) */
void test_chip_id_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"chip-id\":4660}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(4660, h.chip_id,
        "a wrong offsetof in the chip-id row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.mem_size,
        "chip-id's row must not write into mem_size");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "chip-id's row must not write into address");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "chip-id's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.pins,
        "chip-id's row must not write into pins");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "chip-id's row must not write into pulse_delay");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.vpp_mv,
        "chip-id's row must not write into vpp_mv");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "chip-id's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "chip-id's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "chip-id's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "chip-id's row must not write into page_size");
}

/* vpp_mv -> handle->vpp_mv (uint16_t). Wire key is the UNDERSCORE form, as
 * declared: `const char key_vpp_mv[] PROGMEM = "vpp_mv";`. A hyphenated
 * spelling here would silently never match -- the same trap key_page_size's
 * own comment documents in the opposite direction. */
void test_vpp_mv_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"vpp_mv\":12000}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(12000, h.vpp_mv,
        "a wrong offsetof in the vpp_mv row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.mem_size,
        "vpp_mv's row must not write into mem_size");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "vpp_mv's row must not write into address");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "vpp_mv's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.chip_id,
        "vpp_mv's row must not write into chip_id");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.pins,
        "vpp_mv's row must not write into pins");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "vpp_mv's row must not write into pulse_delay");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "vpp_mv's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "vpp_mv's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "vpp_mv's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "vpp_mv's row must not write into page_size");
}

/* pin-count -> handle->pins (uint8_t) */
void test_pin_count_round_trips_through_the_field_table(void) {
    const char* json = "{\"cmd\":1,\"pin-count\":28}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(28, h.pins,
        "a wrong offsetof in the pin-count row would write into a "
        "neighbouring member instead -- the compile-time guards cannot see "
        "that, only this executing round-trip can");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.mem_size,
        "pin-count's row must not write into mem_size");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "pin-count's row must not write into address");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.ctrl_flags,
        "pin-count's row must not write into ctrl_flags");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.chip_id,
        "pin-count's row must not write into chip_id");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "pin-count's row must not write into pulse_delay");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.vpp_mv,
        "pin-count's row must not write into vpp_mv");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, h.protocol,
        "pin-count's row must not write into protocol");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_settling_us,
        "pin-count's row must not write into read_settling_us");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.read_strobe_us,
        "pin-count's row must not write into read_strobe_us");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, h.page_size,
        "pin-count's row must not write into page_size");
}

/* T7: a wire `address` beginning with '-' must be REFUSED, not silently
 * clamped to 0. `simple_strtoul` consumes only `[0-9]`, so a leading '-'
 * makes its loop body never run and the address arrives as 0 --
 * indistinguishable from a legitimate address-0 frame. json_parse must
 * return -1 for this frame and leave handle->address at the 0 its own
 * init block set (json_parse:269), never at a value simple_strtoul
 * converted from the digits after the sign. */
void test_negative_address_is_refused_not_clamped_to_zero(void) {
    const char* json = "{\"cmd\":2,\"address\":-256}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    h.address = 0xDEADBEEF; /* poison value: a real store would overwrite this */
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, rc,
        "json_parse must refuse a frame whose address value begins with '-' "
        "-- the wire cannot represent a negative address, so the frame is "
        "malformed, not zero");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.address,
        "the handle's address member must be left at the 0 json_parse's own "
        "init block sets, never written with a value simple_strtoul "
        "converted from the digits following the sign");
}

/* T8: a positive address must still parse to exactly that value -- the
 * refusal above must not become a blanket rejection of the address field. */
void test_positive_address_still_parses_to_its_value(void) {
    const char* json = "{\"cmd\":2,\"address\":4096}";
    firestarter_handle_t h = make_handle(CMD_WRITE);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4096, h.address,
        "a positive address must parse unchanged -- the negative-address "
        "refusal above is scoped to values that begin with '-', not to the "
        "address field wholesale");
}

/* T9 (scope guard, OQ-3): a negative value on a NON-address numeric field
 * must parse EXACTLY as it does today -- unchanged, not refused. This case
 * must be GREEN from the start: it asserts behaviour this plan does not
 * change, and its value is to go RED if a later author widens the refusal
 * past the address field. `pulse-delay` shares simple_strtoul's conversion
 * with every other numeric field; a leading '-' makes the loop body never
 * run here exactly as it did for address before this fix, so today's
 * (unchanged) behaviour is "parses to 0", not a refusal. */
void test_negative_value_on_a_non_address_field_is_unchanged(void) {
    const char* json = "{\"cmd\":1,\"pulse-delay\":-500}";
    firestarter_handle_t h = make_handle(CMD_READ);
    int rc = parse_json(json, &h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc,
        "OQ-3 scope guard: a negative value on pulse-delay (a non-address "
        "field) must NOT be refused -- only the address field's policy bit "
        "is set, so json_parse must still return 0 for this frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.pulse_delay,
        "OQ-3 scope guard: simple_strtoul's leading-'-' behaviour on a "
        "non-address field is unchanged by this fix -- the loop body never "
        "runs for a leading '-', so the field parses to 0 exactly as it "
        "did before this plan, and this case must go RED if a later "
        "author widens the refusal beyond the address field");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_read_settling_us_parsed_from_json);
    RUN_TEST(test_read_strobe_us_parsed_from_json);
    RUN_TEST(test_read_timing_fields_default_zero_when_absent);
    RUN_TEST(test_read_settling_us_capped_at_max);
    RUN_TEST(test_read_strobe_us_capped_at_max);
    RUN_TEST(test_page_size_parsed_from_json);
    RUN_TEST(test_page_size_defaults_zero_when_absent);
    RUN_TEST(test_page_size_resets_between_two_parses_on_the_same_handle);
    RUN_TEST(test_unknown_key_before_a_known_key_does_not_desync_the_token_walk);
    RUN_TEST(test_unknown_key_before_page_size_does_not_desync_the_token_walk);
    RUN_TEST(test_out_of_range_algorithm_saturates_not_truncates);
    RUN_TEST(test_out_of_range_algorithm_dispatch_fail_closes);
    RUN_TEST(test_in_range_algorithm_still_dispatches);
    RUN_TEST(test_out_of_range_flags_masks_never_sets_every_flag);
    RUN_TEST(test_out_of_range_page_size_saturates_not_truncates_to_a_valid_size);
    RUN_TEST(test_memory_size_round_trips_through_the_field_table);
    RUN_TEST(test_address_round_trips_through_the_field_table);
    RUN_TEST(test_pulse_delay_round_trips_through_the_field_table);
    RUN_TEST(test_chip_id_round_trips_through_the_field_table);
    RUN_TEST(test_vpp_mv_round_trips_through_the_field_table);
    RUN_TEST(test_pin_count_round_trips_through_the_field_table);
    RUN_TEST(test_negative_address_is_refused_not_clamped_to_zero);
    RUN_TEST(test_positive_address_still_parses_to_its_value);
    RUN_TEST(test_negative_value_on_a_non_address_field_is_unchanged);
    return UNITY_END();
}
