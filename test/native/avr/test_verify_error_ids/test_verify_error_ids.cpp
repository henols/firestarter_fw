/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 204 Plan 02 -- the amended FWCMD-05's own native suite. Asserts the
 * emitted MESSAGE ID, not merely a generic error response code, for each of
 * the four failure ids the amended requirement names: MSG_ERR_VERIFY from
 * the shared final-pass verify AND from the 28C page read-back;
 * MSG_ERR_OP_TIMEOUT from the DQ7 data-poll wait; MSG_ERR_MAX_PULSES and
 * MSG_ERR_ENERGY_CAP from the per-pulse loop's two budget exits.
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
#include "eeprom_28c.h"
#include "memory_utils.h"
#include "flash_utils.h"
#include "messages.h"
}
#include "firestarter.h"
#include "../_shared/sdp_bus_config.h"

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

static firestarter_handle_t make_eeprom28c_handle(void) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D; /* PROTO_EEPROM_PARALLEL */
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = SDP_BUS_CONFIGS[0].mem_size; /* AT28C256, 32768 */
    h.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h.page_size = 0; /* absent -- eeprom28c_page_mask falls back to AT28C_PAGE_SIZE_FALLBACK (64) */
    return h;
}

static firestarter_handle_t make_eprom_write_handle(uint8_t protocol) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.pins = (protocol == 0x0B) ? 24 : 28;
    h.mem_size = 65536;
    h.bus_config = VERIFY_IDS_EPROM_BUS_CONFIG;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = 0;
    h.chip_id = 0;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
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
 * Id group 3: the 28C page read-back (eeprom28c_verify_page_readback,
 * static -- driven through eeprom28c_write_execute, its only caller)
 * ───────────────────────────────────────────────────────────────────────── */

void test_eeprom28c_page_readback_mismatch_raises_verify_id_not_eeprom_timeout(void) {
    firestarter_handle_t h = make_eeprom28c_handle();
    configure_memory(&h);
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0x01;
    /* DQ7 (0x80) of 0x02 equals DQ7 of expected 0x01 (both 0) -- so
     * eeprom28c_wait_for_page_write's completion poll succeeds immediately
     * -- but the FULL byte 0x02 != 0x01, so eeprom28c_verify_page_readback's
     * whole-byte compare fails. This is what isolates the verify id from
     * the sibling timeout id: a walker that only checked for presence would
     * pass on either. */
    verify_ids_set_readback(0x02);

    h.firestarter_operation_main(&h); /* eeprom28c_write_execute */

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "non-vacuity: a mismatching page read-back must capture at least one frame");
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_VERIFY),
        "a mismatching page read-back must raise MSG_ERR_VERIFY");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_EEPROM_TIMEOUT),
        "the DQ7 completion poll succeeded -- MSG_ERR_EEPROM_TIMEOUT (its sibling failure "
        "on the SAME function) must NOT also appear");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a mismatching page read-back must set RESPONSE_CODE_ERROR");
}

void test_eeprom28c_page_readback_match_raises_no_verify_id(void) {
    firestarter_handle_t h = make_eeprom28c_handle();
    configure_memory(&h);
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0x01;
    verify_ids_set_readback(0x01); /* matches exactly -- both the DQ7 poll and the full-byte compare succeed */
    h.response_code = RESPONSE_CODE_OK;

    h.firestarter_operation_main(&h); /* eeprom28c_write_execute */

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    /* Unlike memory_verify_execute and flash_util_verify_operation (which
     * emit nothing at all on success), eeprom28c_write_execute
     * unconditionally emits MSG_INFO_PAGE_LOAD_WORST_US even on a fully
     * successful write -- so a truly empty capture here would mean the
     * drive never actually ran, not that it succeeded quietly. */
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_frame_count(ids) >= 1,
        "non-vacuity: a successful page write still emits its cadence-report frame");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_VERIFY),
        "a matching page read-back must never raise MSG_ERR_VERIFY");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_EEPROM_TIMEOUT),
        "a matching page read-back must never raise MSG_ERR_EEPROM_TIMEOUT either");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a fully converging page write must leave RESPONSE_CODE_OK");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Id group 4: the per-pulse loop's two budget exits
 * (eprom_internal_report_budget_failure, via eprom.cpp's shared write path)
 * ───────────────────────────────────────────────────────────────────────── */

void test_per_pulse_loop_exhausts_pulse_budget_raises_max_pulses_not_energy_cap(void) {
    /* Protocol 0x07: max_pulses == 25, energy_cap_us == 0 (UNCAPPED) --
     * energy_cap_us == 0 means the energy-cap arm can never trip here, so
     * a byte that never converges can only exhaust the PULSE budget. */
    firestarter_handle_t h = make_eprom_write_handle(0x07);
    configure_memory(&h); /* dispatches to configure_eprom; pulse_delay defaults to 1000 for 0x07 */
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0x01; /* non-0xFF -- needs pulses */
    verify_ids_set_readback(0x00); /* never matches 0x01 -- the byte never converges */

    h.firestarter_operation_main(&h); /* eprom_write_execute */

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "non-vacuity: an exhausted pulse budget must capture at least one frame");
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_MAX_PULSES),
        "a byte that never converges, under an uncapped energy budget, must raise MSG_ERR_MAX_PULSES");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_ENERGY_CAP),
        "energy_cap_us == 0 (uncapped) on this protocol -- MSG_ERR_ENERGY_CAP must never appear");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "an exhausted pulse budget must set RESPONSE_CODE_ERROR");
}

void test_per_pulse_loop_exhausts_energy_cap_raises_energy_cap_not_max_pulses(void) {
    /* Protocol 0x0B: max_pulses == 255, energy_cap_us == 50000 (50 ms).
     * A pulse_delay of 6000 us accumulates past 50000 after 9 pulses --
     * far short of the 255-pulse ceiling -- so the ENERGY budget exhausts
     * first. Set BEFORE configure_memory/configure_eprom: pulse_delay is
     * only auto-defaulted when it is still 0 at that point, and 6000 <=
     * energy_cap_us (50000) so the pre-flight MSG_ERR_PULSE_TOO_WIDE
     * refusal does not fire either. */
    firestarter_handle_t h = make_eprom_write_handle(0x0B);
    h.pulse_delay = 6000;
    configure_memory(&h);
    h.address = 0;
    h.data_size = 1;
    h.data_buffer[0] = (char)0x01;
    verify_ids_set_readback(0x00); /* never matches -- the byte never converges */

    h.firestarter_operation_main(&h); /* eprom_write_execute */

    std::vector<uint8_t> ids;
    verify_ids_captured_frame_ids(&ids);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)verify_ids_frame_count(ids),
        "non-vacuity: an exhausted energy cap must capture at least one frame");
    TEST_ASSERT_TRUE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_ENERGY_CAP),
        "an energy cap small enough to exhaust before the pulse budget must raise MSG_ERR_ENERGY_CAP");
    TEST_ASSERT_FALSE_MESSAGE(verify_ids_contains(ids, (uint8_t)MSG_ERR_MAX_PULSES),
        "the pulse budget (255) is far from exhausted when the energy cap trips -- "
        "MSG_ERR_MAX_PULSES must not appear");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "an exhausted energy cap must set RESPONSE_CODE_ERROR");
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
    RUN_TEST(test_eeprom28c_page_readback_mismatch_raises_verify_id_not_eeprom_timeout);
    RUN_TEST(test_eeprom28c_page_readback_match_raises_no_verify_id);
    RUN_TEST(test_per_pulse_loop_exhausts_pulse_budget_raises_max_pulses_not_energy_cap);
    RUN_TEST(test_per_pulse_loop_exhausts_energy_cap_raises_energy_cap_not_max_pulses);

    return UNITY_END();
}
