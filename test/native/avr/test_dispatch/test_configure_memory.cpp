/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 12 Wave 0 — dispatch unit tests for configure_memory().
 *
 * One test per protocol in KNOWN_PROTOCOLS (build_db.py:89). Each test
 * constructs a minimal firestarter_handle_t (protocol, mem_type, cmd,
 * response_code) and asserts `configure_memory()` does not raise
 * RESPONSE_CODE_ERROR (i.e. the chip resolved to a real handler, not the
 * "Memory type 0x%02x not supported" fallback).
 *
 * RED state on this commit (Wave 0): protocols 0x05, 0x06, 0x07, 0x08, 0x0B,
 * 0x0E, 0x27, 0x28, 0x29, 0x35, 0x39 are NOT yet routed by protocol prefix —
 * they fall through to `firestarter_error_response_format(...)` and set
 * response_code = RESPONSE_CODE_ERROR. Only 0x10 and 0x0D pass today.
 *
 * Wave 1 (Plan 02) lands the C++ dispatch extension and flips these tests
 * GREEN. The negative test (`test_unknown_protocol_with_unknown_mem_type_errors`)
 * must remain GREEN at every wave — it asserts the fallback error path still
 * fires for genuinely-unknown (protocol, mem_type) pairs.
 *
 * Why TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, ...) and not an operation-
 * pointer check? `configure_sram()` is a stub today and leaves the
 * firestarter_operation_init pointer NULL — pointer-set assertions would
 * spuriously fail. response_code is the robust dispatch-success signal.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    /* Stub Serial.write and Serial.flush so that LOG_ERROR_ID_* calls in the
     * error dispatch path (e.g. MSG_ERR_MEM_TYPE_UNSUPPORTED) don't abort.
     * Dispatch tests never assert on serial output — only on response_code. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* Phase 84: stub delay() so the VPP-skip init tests can call
     * eprom_generic_init → eprom_check_vpp without an ArduinoFake
     * UnexpectedMethodCallException on the delay(100) call inside
     * eprom_check_vpp. Existing dispatch tests never call the init function
     * so this stub is additive-only and cannot break prior tests. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
}

void tearDown(void) {
}

/* Build a zero-initialized handle with only the three named fields set. */
static firestarter_handle_t make_handle(uint32_t protocol, uint8_t mem_type, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.mem_type = mem_type;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    return h;
}

/* Positive dispatch tests — one per protocol in KNOWN_PROTOCOLS.
 * Each asserts that configure_memory() does NOT set response_code to
 * RESPONSE_CODE_ERROR (i.e. dispatch reached a real handler). */

void test_protocol_0x06_dispatches_flash3(void) {
    firestarter_handle_t h = make_handle(0x06, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x05_dispatches_flash4(void) {
    firestarter_handle_t h = make_handle(0x05, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x35_dispatches_flash4(void) {
    firestarter_handle_t h = make_handle(0x35, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x39_dispatches_flash4(void) {
    firestarter_handle_t h = make_handle(0x39, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x07_dispatches_eprom(void) {
    firestarter_handle_t h = make_handle(0x07, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x08_dispatches_eprom(void) {
    firestarter_handle_t h = make_handle(0x08, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x0B_dispatches_eprom(void) {
    firestarter_handle_t h = make_handle(0x0B, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x0E_dispatches_sram(void) {
    firestarter_handle_t h = make_handle(0x0E, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x27_dispatches_sram(void) {
    firestarter_handle_t h = make_handle(0x27, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x28_dispatches_sram(void) {
    firestarter_handle_t h = make_handle(0x28, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x29_dispatches_sram(void) {
    firestarter_handle_t h = make_handle(0x29, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x10_dispatches_flash_intel(void) {
    firestarter_handle_t h = make_handle(0x10, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

void test_protocol_0x0D_dispatches_eeprom28c(void) {
    firestarter_handle_t h = make_handle(0x0D, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/* Negative test: genuinely-unknown (protocol, mem_type) pair must surface
 * the error response. This must remain green across every wave. */
void test_unknown_protocol_with_unknown_mem_type_errors(void) {
    firestarter_handle_t h = make_handle(0, 99, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/* Fallback test: protocol=0 with a known mem_type still resolves via the
 * legacy mem_type chain. Must remain green across every wave. */
void test_protocol_zero_with_mem_type_eprom_dispatches_eprom(void) {
    firestarter_handle_t h = make_handle(0, 1, CMD_READ); /* TYPE_EPROM = 1 */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/* FIX-02A (Phase 74 Plan 02): configure_flash4 must handle CMD_CHECK_CHIP_ID
 * by setting a non-NULL operation_main pointer (mirroring configure_flash3).
 * These three tests are RED before the fix (no case in configure_flash4 switch
 * → firestarter_operation_main stays NULL). */

void test_flash4_check_chip_id_0x05_sets_operation(void) {
    firestarter_handle_t h = make_handle(0x05, 0, CMD_CHECK_CHIP_ID);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "CMD_CHECK_CHIP_ID on 0x05 must not error");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "CMD_CHECK_CHIP_ID on 0x05 must set a non-NULL operation_main");
}

void test_flash4_check_chip_id_0x35_sets_operation(void) {
    firestarter_handle_t h = make_handle(0x35, 0, CMD_CHECK_CHIP_ID);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "CMD_CHECK_CHIP_ID on 0x35 must not error");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "CMD_CHECK_CHIP_ID on 0x35 must set a non-NULL operation_main");
}

void test_flash4_check_chip_id_0x39_sets_operation(void) {
    firestarter_handle_t h = make_handle(0x39, 0, CMD_CHECK_CHIP_ID);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "CMD_CHECK_CHIP_ID on 0x39 must not error");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "CMD_CHECK_CHIP_ID on 0x39 must set a non-NULL operation_main");
}

/* =========================================================================
 * Phase 84 Plan 01 — VPP-skip gate tests (D-11, FIX-01, T-84-01)
 *
 * Threat model (T-84-01): the VPP-skip must be EXACTLY scoped to
 * CMD_READ and CMD_BLANK_CHECK.  Write/erase/chip-id must still gate VPP
 * so the over-voltage block is preserved.
 *
 * Observable: response_code after calling firestarter_operation_init.
 * - eprom_check_vpp with rurp_read_voltage_mv()=0 and vpp_mv=12000 always
 *   hits the "VPP is low" branch (0 < 11400) → response_code = WARNING.
 * - If eprom_check_vpp is skipped (the D-11 fix), response_code stays OK.
 *
 * Positive tests (CMD_READ / CMD_BLANK_CHECK):
 *   After the D-11 fix:  response_code == OK  (VPP check skipped) → PASS.
 *   Before the fix:      response_code == WARNING (VPP check ran) → FAIL.
 *
 * Negative tests (CMD_WRITE / CMD_ERASE / CMD_CHECK_CHIP_ID):
 *   response_code must be WARNING (VPP check still ran, returning low-VPP
 *   warning with the stub's 0 mV reading) → PASS always (no code change
 *   for these paths, so both before and after the fix they gate VPP).
 * ========================================================================= */

static bool recording_get_control_register(
        struct firestarter_handle* handle,
        rurp_register_t bit)
{
    (void)handle; (void)bit;
    return false;
}

/* Build a minimal EPROM handle for the VPP-skip gate tests.
 * - vpp_mv=12000 ensures eprom_check_vpp will set WARNING when it runs
 *   (rurp_read_voltage_mv returns 0, which is < 12000*95/100 = 11400).
 * - FLAG_SKIP_BLANK_CHECK avoids needing firestarter_get_data stubs for
 *   the CMD_WRITE path.
 * - firestarter_get_control_register stub prevents NULL-ptr crash in
 *   eprom_write_init's is_operation_in_progress check. */
static firestarter_handle_t make_vpp_gate_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = 12000;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK;
    h.firestarter_get_control_register = recording_get_control_register;
    return h;
}

/* --- Positive tests: read/blank-check must NOT run eprom_check_vpp
 *     (response_code must stay RESPONSE_CODE_OK after the D-11 fix).
 *     These tests FAIL against the unmodified source (RED gate). --- */

void test_eprom_read_does_not_run_vpp_check(void) {
    firestarter_handle_t h = make_vpp_gate_handle(0x07, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x07/CMD_READ");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_init,
        "eprom_generic_init must be set as init for CMD_READ");
    h.firestarter_operation_init(&h);
    /* After D-11 fix: VPP check skipped → response_code stays OK.
     * Unmodified code: VPP check runs → response_code = WARNING (FAIL). */
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "CMD_READ must NOT run eprom_check_vpp (D-11 VPP-skip): "
        "response_code must stay OK, not be set to WARNING by VPP low check");
}

void test_eprom_blank_check_does_not_run_vpp_check(void) {
    firestarter_handle_t h = make_vpp_gate_handle(0x07, CMD_BLANK_CHECK);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x07/CMD_BLANK_CHECK");
    h.firestarter_operation_init(&h);
    /* After D-11 fix: response_code stays OK (VPP skipped).
     * Unmodified: response_code = WARNING (FAIL). */
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "CMD_BLANK_CHECK must NOT run eprom_check_vpp (D-11 VPP-skip): "
        "response_code must stay OK, not be set to WARNING by VPP low check");
}

/* --- Negative tests (T-84-01 over-broadening hazard mitigation):
 *     write/erase/chip-id MUST still run eprom_check_vpp exactly as before.
 *     With 0 mV stub voltage: response_code = WARNING (VPP low).
 *     These tests pass both before and after the D-11 fix. --- */

void test_eprom_write_still_runs_vpp_check(void) {
    firestarter_handle_t h = make_vpp_gate_handle(0x07, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x07/CMD_WRITE");
    h.firestarter_operation_init(&h);
    /* VPP check MUST have run: voltage 0 < 11400 → WARNING */
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "CMD_WRITE MUST still run eprom_check_vpp (over-voltage gate preserved): "
        "0 mV stub voltage must produce WARNING response");
}

void test_eprom_erase_still_runs_vpp_check(void) {
    firestarter_handle_t h = make_vpp_gate_handle(0x07, CMD_ERASE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x07/CMD_ERASE");
    h.firestarter_operation_init(&h);
    /* VPP check MUST have run → WARNING */
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "CMD_ERASE MUST still run eprom_check_vpp (over-voltage gate preserved)");
}

void test_eprom_check_chip_id_still_runs_vpp_check(void) {
    firestarter_handle_t h = make_vpp_gate_handle(0x07, CMD_CHECK_CHIP_ID);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x07/CMD_CHECK_CHIP_ID");
    /* eprom_check_chip_id_init → eprom_check_vpp directly */
    h.firestarter_operation_init(&h);
    /* VPP check MUST have run → WARNING (12V on A9 for ID requires VPP gate) */
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "CMD_CHECK_CHIP_ID MUST still run eprom_check_vpp "
        "(12V on A9 for ID; over-voltage gate preserved)");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    /* 13 protocol-positive tests (one per KNOWN_PROTOCOLS entry) */
    RUN_TEST(test_protocol_0x06_dispatches_flash3);
    RUN_TEST(test_protocol_0x05_dispatches_flash4);
    RUN_TEST(test_protocol_0x35_dispatches_flash4);
    RUN_TEST(test_protocol_0x39_dispatches_flash4);
    RUN_TEST(test_protocol_0x07_dispatches_eprom);
    RUN_TEST(test_protocol_0x08_dispatches_eprom);
    RUN_TEST(test_protocol_0x0B_dispatches_eprom);
    RUN_TEST(test_protocol_0x0E_dispatches_sram);
    RUN_TEST(test_protocol_0x27_dispatches_sram);
    RUN_TEST(test_protocol_0x28_dispatches_sram);
    RUN_TEST(test_protocol_0x29_dispatches_sram);
    RUN_TEST(test_protocol_0x10_dispatches_flash_intel);
    RUN_TEST(test_protocol_0x0D_dispatches_eeprom28c);

    /* 1 negative + 1 fallback test (must remain green across every wave) */
    RUN_TEST(test_unknown_protocol_with_unknown_mem_type_errors);
    RUN_TEST(test_protocol_zero_with_mem_type_eprom_dispatches_eprom);

    /* FIX-02A: CMD_CHECK_CHIP_ID dispatch tests (RED before flash_type_4.cpp fix) */
    RUN_TEST(test_flash4_check_chip_id_0x05_sets_operation);
    RUN_TEST(test_flash4_check_chip_id_0x35_sets_operation);
    RUN_TEST(test_flash4_check_chip_id_0x39_sets_operation);

    /* Phase 84 D-11 VPP-skip gate tests (RED before eprom.cpp fix) */
    RUN_TEST(test_eprom_read_does_not_run_vpp_check);
    RUN_TEST(test_eprom_blank_check_does_not_run_vpp_check);
    RUN_TEST(test_eprom_write_still_runs_vpp_check);
    RUN_TEST(test_eprom_erase_still_runs_vpp_check);
    RUN_TEST(test_eprom_check_chip_id_still_runs_vpp_check);

    return UNITY_END();
}
