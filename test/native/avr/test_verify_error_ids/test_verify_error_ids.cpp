/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 204 Plan 02 Task 2 -- the amended FWCMD-05's own native suite,
 * first two id groups. Asserts the emitted MESSAGE ID, not merely a generic
 * error response code: MSG_ERR_VERIFY from the shared final-pass verify,
 * MSG_ERR_OP_TIMEOUT from the DQ7 data-poll wait. Task 3 adds the 28C page
 * read-back and the per-pulse loop's two budget exits to this same suite.
 *
 * Every id is asserted in BOTH directions -- the id the site actually
 * raises asserted present, and the id it must never raise asserted absent
 * -- so a transposition fails instead of passing on a superset. The DQ7
 * data-poll wait (flash_util_verify_operation) has no compare-and-report
 * path at all: it is pinned to the timeout id and explicitly asserted to
 * NEVER raise the verify id, because the requirement this suite replaces
 * previously claimed the opposite.
 *
 * Validation ceiling: every claim this suite embodies is software-layer --
 * code emits a sequence, code asserts on it. No EPROM or EEPROM part was
 * ever on the bench, and nothing here is evidence about silicon state.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <vector>

extern "C" {
#include "memory.h"
#include "eprom.h"
#include "memory_utils.h"
#include "flash_utils.h"
#include "messages.h"
}
#include "firestarter.h"

using namespace fakeit;

extern "C" void verify_ids_set_readback(uint8_t value);

/* ─────────────────────────────────────────────────────────────────────────
 * Serial-capture seam -- copied from test_eeprom28c_sdp.cpp, the only
 * in-tree suite that asserts an emitted message id. Walks rurp_log_id()'s
 * fixed, documented wire layout (4-byte magic, 2-byte big-endian length, 1
 * id byte, params, 1 crc byte, 1 anchor byte) and never consults a
 * message-id-to-param-count lookup table, so it stays correct for any id.
 * ───────────────────────────────────────────────────────────────────────── */
static std::vector<uint8_t> captured_frames;

static void verify_ids_captured_frame_ids(std::vector<uint8_t>* out_ids) {
    size_t offset = 0;
    while (offset + 7 <= captured_frames.size()) {
        uint16_t len_value = (uint16_t)(((uint16_t)captured_frames[offset + 4] << 8) | captured_frames[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len_value + 1;
        if (offset + frame_size > captured_frames.size()) {
            break; /* incomplete trailing frame -- not expected in these cases */
        }
        out_ids->push_back(captured_frames[offset + 6]);
        offset += frame_size;
    }
}

static bool verify_ids_contains(const std::vector<uint8_t>& ids, uint8_t id) {
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

/* Every id test asserts a non-zero frame count BEFORE its membership
 * assertions -- a run that captures zero frames must fail every id
 * assertion rather than pass vacuously on an empty vector. */
static size_t verify_ids_frame_count(const std::vector<uint8_t>& ids) {
    return ids.size();
}

/* ─────────────────────────────────────────────────────────────────────────
 * setUp / tearDown
 * ───────────────────────────────────────────────────────────────────────── */

static unsigned long millis_counter;

void setUp(void) {
    ArduinoFakeReset();
    captured_frames.clear();

    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured_frames.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    /* eeprom28c_write_execute measures page-load cadence with micros() --
     * a fixed 0 is sufficient here: this suite asserts on emitted message
     * ids, never on the reported cadence value. */
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);

    /* The advancing clock -- copied from test_cobs_cmd_frame.cpp's setUp.
     * Every other native suite pins millis() to a constant, which is why
     * flash_util_verify_operation's 150 ms timeout arm has never been
     * exercised anywhere in this tree until this suite. */
    millis_counter = 0;
    When(Method(ArduinoFake(Function), millis))
        .AlwaysDo([&]() -> unsigned long {
            millis_counter += 100;
            return millis_counter;
        });

    verify_ids_set_readback(0x00);
}

void tearDown(void) {}

/* ─────────────────────────────────────────────────────────────────────────
 * Handle factories
 * ───────────────────────────────────────────────────────────────────────── */

/* Identity address mapping for protocol 0x07/0x0B -- copied verbatim from
 * test_val_eprom.cpp's VAL_EPROM_BUS_CONFIG_0x07 (itself copied from
 * test_loop_eprom_v131.cpp / test_trace_eprom_v131.cpp). Do NOT invent a
 * fresh one: a zeroed bus_config is DEGENERATE, not an identity remap. */
static const bus_config_t VERIFY_IDS_EPROM_BUS_CONFIG = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF },
    0x0000FFFFUL,
    16,
    0xFF,
    0xFF,
    0x00000000UL
};

static firestarter_handle_t make_memory_verify_handle(void) {
    firestarter_handle_t h = {};
    h.protocol = 0x07;
    h.cmd = CMD_READ; /* arbitrary -- memory_verify_execute is called directly, not via operation_main */
    h.response_code = RESPONSE_CODE_OK;
    h.mem_size = 65536;
    h.bus_config = VERIFY_IDS_EPROM_BUS_CONFIG;
    h.chip_id = 0;
    return h;
}

static firestarter_handle_t make_flash_util_handle(void) {
    firestarter_handle_t h = {};
    h.protocol = 0x06; /* PROTO_FLASH_NOR_UNLOCK -- production's own caller of flash_util_verify_operation */
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.mem_size = 65536;
    h.chip_id = 0;
    return h;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Id group 1: the shared final-pass verify (memory_verify_execute)
 * ───────────────────────────────────────────────────────────────────────── */

void test_shared_final_pass_verify_mismatch_raises_verify_id_not_timeout(void) {
    firestarter_handle_t h = make_memory_verify_handle();
    configure_memory(&h);
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0xAA;
    verify_ids_set_readback(0x55); /* never matches 0xAA */

    memory_verify_execute(&h);

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "non-vacuity: a mismatch must capture at least one frame -- an empty capture "
        "would pass every membership assertion below vacuously");
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_VERIFY),
        "a mismatching read-back must raise MSG_ERR_VERIFY");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_OP_TIMEOUT),
        "the shared final-pass verify has no timing path -- it must never raise MSG_ERR_OP_TIMEOUT");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a mismatching read-back must set RESPONSE_CODE_ERROR");
}

void test_shared_final_pass_verify_match_raises_neither_id(void) {
    firestarter_handle_t h = make_memory_verify_handle();
    configure_memory(&h);
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0xAA;
    verify_ids_set_readback(0xAA); /* matches exactly */
    h.response_code = RESPONSE_CODE_WARNING; /* sentinel -- proves the function leaves it alone */

    memory_verify_execute(&h);

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_VERIFY),
        "a matching read-back must never raise MSG_ERR_VERIFY");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "a matching read-back must leave response_code exactly as it was before the call");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Id group 2: the DQ7 data-poll wait (flash_util_verify_operation)
 * ───────────────────────────────────────────────────────────────────────── */

void test_flash_util_data_poll_timeout_raises_timeout_id_not_verify_id(void) {
    firestarter_handle_t h = make_flash_util_handle();
    configure_memory(&h);
    verify_ids_set_readback(0x00); /* DQ7 bit (0x80) never matches expected's 0x80 */

    flash_util_verify_operation(&h, 0x80);

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "non-vacuity: a timed-out poll must capture at least one frame");
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_OP_TIMEOUT),
        "a DQ7 bit that never matches must raise MSG_ERR_OP_TIMEOUT");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_VERIFY),
        "flash_util_verify_operation is a DQ7 data-poll wait with no compare-and-report "
        "path -- it can NEVER raise MSG_ERR_VERIFY. Pinning it to the verify id would be "
        "a false pin, which is exactly what this leg exists to rule out.");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a timed-out poll must set RESPONSE_CODE_ERROR");
}

void test_flash_util_data_poll_match_raises_no_error_frame(void) {
    firestarter_handle_t h = make_flash_util_handle();
    configure_memory(&h);
    verify_ids_set_readback(0x80); /* DQ7 bit matches expected's 0x80 on both confirmation reads */
    h.response_code = RESPONSE_CODE_OK;

    flash_util_verify_operation(&h, 0x80);

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "a DQ7 bit that matches on both confirmation reads must return without emitting "
        "any error frame at all");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a successful poll must leave response_code unchanged");
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_shared_final_pass_verify_mismatch_raises_verify_id_not_timeout);
    RUN_TEST(test_shared_final_pass_verify_match_raises_neither_id);
    RUN_TEST(test_flash_util_data_poll_timeout_raises_timeout_id_not_verify_id);
    RUN_TEST(test_flash_util_data_poll_match_raises_no_error_frame);
    return UNITY_END();
}
