/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * dispatch unit tests for configure_memory().
 *
 * One test per protocol in KNOWN_PROTOCOLS (build_db.py:89). Each test
 * constructs a minimal firestarter_handle_t (protocol, cmd, response_code)
 * and asserts that `configure_memory()` sets the operation pointers of the
 * expected configure_<family>() function.
 *
 * (protocol-only dispatch): dispatch is keyed on `protocol` alone
 * — there is no backward-compat fallback axis. Any unrecognized protocol,
 * including 0, reaches `configure_not_implemented()`; see
 * test_not_implemented.cpp for fail-closed coverage.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <stdio.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "eeprom_28c.h"
#include "eprom.h"
#include "flash_5v_page.h"
#include "flash_intel.h"
#include "flash_nor_unlock.h"
#include "sram.h"

#include "../_shared/dispatch_expect.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    /* Stub Serial.write and Serial.flush so that LOG_ERROR_ID_* calls in the
     * error dispatch path (e.g. MSG_ERR_PROTOCOL_NOT_IMPLEMENTED) don't abort.
     * Dispatch tests never assert on serial output — only on response_code. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {
}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t mem_type, uint8_t cmd) {
    (void)mem_type;
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    return h;
}

/* Positive dispatch tests -- one per protocol in KNOWN_PROTOCOLS. Each
 * compares the operation pointers that configure_memory() sets with the
 * pointers that the expected configure_<family>() sets on a fresh handle, for
 * CMD_WRITE and CMD_CHECK_CHIP_ID. CMD_READ cannot tell the families apart:
 * every family keeps configure_memory's memory_read_execute. */
static void check_protocol_dispatch(uint32_t protocol, configure_family_fn_t family, const char* family_name) {
    static const uint8_t cmds[] = {CMD_WRITE, CMD_CHECK_CHIP_ID};
    static const char* cmd_names[] = {"CMD_WRITE", "CMD_CHECK_CHIP_ID"};
    for (size_t c = 0; c < 2; c++) {
        char ctx[96];
        snprintf(ctx, sizeof(ctx), "protocol 0x%02X %s -> %s", (unsigned)protocol, cmd_names[c], family_name);
        assert_dispatches_to(make_handle(protocol, 0, cmds[c]), family, ctx);
    }
}

void test_protocol_0x06_dispatches_nor_unlock(void) {
    check_protocol_dispatch(0x06, configure_flash_nor_unlock, "configure_flash_nor_unlock");
}

void test_protocol_0x05_dispatches_5v_page(void) {
    check_protocol_dispatch(0x05, configure_flash_5v_page, "configure_flash_5v_page");
}

void test_protocol_0x35_dispatches_5v_page(void) {
    check_protocol_dispatch(0x35, configure_flash_5v_page, "configure_flash_5v_page");
}

void test_protocol_0x39_dispatches_5v_page(void) {
    check_protocol_dispatch(0x39, configure_flash_5v_page, "configure_flash_5v_page");
}

void test_protocol_0x07_dispatches_eprom(void) {
    check_protocol_dispatch(0x07, configure_eprom, "configure_eprom");
}

void test_protocol_0x08_dispatches_eprom(void) {
    check_protocol_dispatch(0x08, configure_eprom, "configure_eprom");
}

void test_protocol_0x0B_dispatches_eprom(void) {
    check_protocol_dispatch(0x0B, configure_eprom, "configure_eprom");
}

void test_protocol_0x0E_dispatches_sram(void) {
    check_protocol_dispatch(0x0E, configure_sram, "configure_sram");
}

void test_protocol_0x27_dispatches_sram(void) {
    check_protocol_dispatch(0x27, configure_sram, "configure_sram");
}

void test_protocol_0x28_dispatches_sram(void) {
    check_protocol_dispatch(0x28, configure_sram, "configure_sram");
}

void test_protocol_0x29_dispatches_sram(void) {
    check_protocol_dispatch(0x29, configure_sram, "configure_sram");
}

void test_protocol_0x10_dispatches_flash_intel(void) {
    check_protocol_dispatch(0x10, configure_flash_intel, "configure_flash_intel");
}

void test_protocol_0x0D_dispatches_eeprom28c(void) {
    check_protocol_dispatch(0x0D, configure_eeprom28c, "configure_eeprom28c");
}

/* CMD_CHECK_CHIP_ID on the 0x05 family must set a chip-ID read: a NULL main
 * would make the op layer refuse the command. */
static void check_5v_page_chip_id(uint32_t protocol) {
    firestarter_handle_t h = make_handle(protocol, 0, CMD_CHECK_CHIP_ID);
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "CMD_CHECK_CHIP_ID on the 0x05 family must set a non-NULL operation_main");
    assert_dispatches_to(make_handle(protocol, 0, CMD_CHECK_CHIP_ID), configure_flash_5v_page,
                         "CMD_CHECK_CHIP_ID on the 0x05 family");
}

void test_5v_page_check_chip_id_0x05_sets_operation(void) {
    check_5v_page_chip_id(0x05);
}

void test_5v_page_check_chip_id_0x35_sets_operation(void) {
    check_5v_page_chip_id(0x35);
}

void test_5v_page_check_chip_id_0x39_sets_operation(void) {
    check_5v_page_chip_id(0x39);
}

struct protocol_family_row_t {
    uint32_t protocol;
    const char* family_name;
    configure_family_fn_t family;
};

static const protocol_family_row_t kAllProtocolFamilies[] = {
    {0x07, "eprom (0x07/0x08/0x0B)", configure_eprom},
    {0x08, "eprom (0x07/0x08/0x0B)", configure_eprom},
    {0x0B, "eprom (0x07/0x08/0x0B)", configure_eprom},
    {0x0D, "eeprom28c (0x0D)", configure_eeprom28c},
    {0x10, "flash_intel (0x10)", configure_flash_intel},
    {0x06, "flash_nor_unlock (0x06)", configure_flash_nor_unlock},
    {0x05, "flash_5v_page (0x05/0x35/0x39)", configure_flash_5v_page},
    {0x35, "flash_5v_page (0x05/0x35/0x39)", configure_flash_5v_page},
    {0x39, "flash_5v_page (0x05/0x35/0x39)", configure_flash_5v_page},
    {0x0E, "sram (0x0E/0x27/0x28/0x29)", configure_sram},
    {0x27, "sram (0x0E/0x27/0x28/0x29)", configure_sram},
    {0x28, "sram (0x0E/0x27/0x28/0x29)", configure_sram},
    {0x29, "sram (0x0E/0x27/0x28/0x29)", configure_sram},
};
static const size_t kAllProtocolFamiliesCount = sizeof(kAllProtocolFamilies) / sizeof(kAllProtocolFamilies[0]);

void test_case_group1_read_write_verify_never_null_main_for_any_protocol(void) {
    /* CMD_VERIFY was retired in Phase 204 (ordinal 6, dropped from both arrays
     * and the loop bound below) -- it never reaches configure_memory() any
     * more, so this group is CMD_READ / CMD_WRITE only now. */
    static const uint8_t cmds[] = {CMD_READ, CMD_WRITE};
    static const char* cmd_names[] = {"CMD_READ", "CMD_WRITE"};
    for (size_t i = 0; i < kAllProtocolFamiliesCount; i++) {
        for (size_t c = 0; c < 2; c++) {
            firestarter_handle_t h = make_handle(kAllProtocolFamilies[i].protocol, 0, cmds[c]);
            configure_memory(&h);
            char msg[224];
            snprintf(msg, sizeof(msg),
                "Case group 1 (LOCK-04 positive invariant): %s on protocol 0x%02X (%s) must leave "
                "firestarter_operation_main NON-NULL -- this is the case a blanket default: arm in "
                "any configure_* handler would fail",
                cmd_names[c], (unsigned)kAllProtocolFamilies[i].protocol,
                kAllProtocolFamilies[i].family_name);
            TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main, msg);
            assert_dispatches_to(make_handle(kAllProtocolFamilies[i].protocol, 0, cmds[c]),
                                 kAllProtocolFamilies[i].family, msg);
        }
    }
}

void test_case_group2_sdp_cmds_null_main_for_every_non_0x0d_protocol(void) {
    for (size_t i = 0; i < kAllProtocolFamiliesCount; i++) {
        uint32_t protocol = kAllProtocolFamilies[i].protocol;
        if (protocol == 0x0D) {
            continue;
        }

        firestarter_handle_t h_unlock = make_handle(protocol, 0, CMD_SDP_UNLOCK);
        configure_memory(&h_unlock);
        char msg_u[224];
        snprintf(msg_u, sizeof(msg_u),
            "Case group 2 (LOCK-04 fail-closed, D-06): CMD_SDP_UNLOCK on protocol 0x%02X (%s) must "
            "leave firestarter_operation_main NULL -- refused by the generic op-layer guard, never "
            "by a per-handler default: arm",
            (unsigned)protocol, kAllProtocolFamilies[i].family_name);
        TEST_ASSERT_NULL_MESSAGE(h_unlock.firestarter_operation_main, msg_u);

        firestarter_handle_t h_lock = make_handle(protocol, 0, CMD_SDP_LOCK);
        configure_memory(&h_lock);
        char msg_l[224];
        snprintf(msg_l, sizeof(msg_l),
            "Case group 2 (LOCK-04 fail-closed, D-06): CMD_SDP_LOCK on protocol 0x%02X (%s) must "
            "leave firestarter_operation_main NULL",
            (unsigned)protocol, kAllProtocolFamilies[i].family_name);
        TEST_ASSERT_NULL_MESSAGE(h_lock.firestarter_operation_main, msg_l);
    }
}

void test_case_group3_sdp_cmds_dispatch_on_0x0d_with_null_init_end(void) {
    firestarter_handle_t h_unlock = make_handle(0x0D, 0, CMD_SDP_UNLOCK);
    configure_memory(&h_unlock);
    TEST_ASSERT_NOT_NULL_MESSAGE(h_unlock.firestarter_operation_main,
        "Case group 3 (LOCK-02): CMD_SDP_UNLOCK on 0x0D must set a non-NULL operation_main");
    TEST_ASSERT_NULL_MESSAGE(h_unlock.firestarter_operation_init,
        "Case group 3 (LOCK-02, F-T correction): CMD_SDP_UNLOCK's init is NULL -- this does NOT "
        "mean the INIT phase is skipped; _execute_operation_house_keeping_func still calls "
        "op_wait_for_ack() and still emits the INIT/END frame pair. What is genuinely absent is the "
        "DONE round-trip and any '#' data frame");
    TEST_ASSERT_NULL_MESSAGE(h_unlock.firestarter_operation_end,
        "Case group 3 (LOCK-02, F-T correction): CMD_SDP_UNLOCK's end is NULL, same correction as "
        "init above");

    firestarter_handle_t h_lock = make_handle(0x0D, 0, CMD_SDP_LOCK);
    configure_memory(&h_lock);
    TEST_ASSERT_NOT_NULL_MESSAGE(h_lock.firestarter_operation_main,
        "Case group 3 (LOCK-02): CMD_SDP_LOCK on 0x0D must set a non-NULL operation_main");
    TEST_ASSERT_NULL_MESSAGE(h_lock.firestarter_operation_init,
        "Case group 3 (LOCK-02, F-T correction): CMD_SDP_LOCK's init is NULL, same correction as "
        "unlock above");
    TEST_ASSERT_NULL_MESSAGE(h_lock.firestarter_operation_end,
        "Case group 3 (LOCK-02, F-T correction): CMD_SDP_LOCK's end is NULL, same correction as "
        "unlock above");
}

void test_case_group4a_0x0d_erase_dispatches_to_a_real_op_erase03(void) {
    firestarter_handle_t h_erase = make_handle(0x0D, 0, CMD_ERASE);
    configure_memory(&h_erase);
    TEST_ASSERT_NOT_NULL_MESSAGE(h_erase.firestarter_operation_main,
        "Case group 4a (ERASE-03 fw half): CMD_ERASE on 0x0D must now set a non-NULL "
        "firestarter_operation_main -- configure_eeprom28c carries a `case CMD_ERASE:` arm "
        "assigning eeprom28c_erase_execute, so this cell has left Phase 119 D-06's op-layer "
        "NULL-main guard's coverage; the replacement proof that the arm really emits the AN-0544B "
        "erase sequence (not merely that dispatch resolves) is the stream-equality case in "
        "test_eeprom28c_sdp.cpp, not this dispatch check");
    TEST_ASSERT_NULL_MESSAGE(h_erase.firestarter_operation_init,
        "Case group 4a (ERASE-03 fw half): CMD_ERASE's init is NULL -- configure_eeprom28c's new "
        "case CMD_ERASE: arm assigns only firestarter_operation_main, mirroring case CMD_SDP_LOCK: "
        "immediately above it in the same switch");
    TEST_ASSERT_NULL_MESSAGE(h_erase.firestarter_operation_end,
        "Case group 4a (ERASE-03 fw half): CMD_ERASE's end is NULL, same reason as init above -- "
        "the new arm assigns only firestarter_operation_main");
}

/* Case group 4b — the still-true half of the old combined case: CMD_CHECK_CHIP_ID
 * on 0x0D leaves firestarter_operation_main NULL. Unchanged from the old
 * function's chip-id half; only the function name and the erase half were
 * split out (see Case group 4a above). */
void test_case_group4b_0x0d_chip_id_null_main_devtest01(void) {
    firestarter_handle_t h_chip_id = make_handle(0x0D, 0, CMD_CHECK_CHIP_ID);
    configure_memory(&h_chip_id);
    TEST_ASSERT_NULL_MESSAGE(h_chip_id.firestarter_operation_main,
        "Case group 4: CMD_CHECK_CHIP_ID on 0x0D also leaves firestarter_operation_main NULL -- also "
        "a phantom today, honestly qualified: eprom_check_chip_id already refuses earlier with "
        "MSG_ERR_NO_CHIP_ID when handle->chip_id == 0, and TRACE-05 pinned chip_id_check: false "
        "across all 84 algorithm==13 DB entries, so in practice the host never sends a non-zero "
        "chip id for 0x0D and this cell is already refused upstream -- this guard's coverage here "
        "is real but not the primary mitigation, so this is not over-claimed as the fix for this "
        "cell");
}

/* Case group 5 — the newly-refused non-0x0D cells, enumerated: the SRAM
 * group's CMD_ERASE and CMD_CHECK_CHIP_ID. configure_sram's body is
 * literally just a debug log, which is why these were silent OKs before
 * this task. Tested per-id (not one representative) since configure_sram
 * is the one handler with zero per-command logic to diverge on.
 *
 * Phase 204 (Fork B): this group's third case, the standalone blank-check
 * command's assertion that SRAM's configure handler leaves
 * firestarter_operation_main NULL for it, is DELETED here, not re-keyed.
 * Its own message recorded a Phase 120 disposition of KEEP that rested on
 * a firmware command surface which no longer exists -- after this
 * release that ordinal reaches no configure handler at all, so the same
 * claim is true for a reason that has nothing to do with what the case
 * was written to check. A vacuous assertion carrying a stale KEEP
 * rationale is worse than no assertion, because a reader trusts it. */
void test_case_group5_sram_erase_chip_id_null_main(void) {
    static const uint32_t sram_protocols[] = {0x0E, 0x27, 0x28, 0x29};
    for (size_t i = 0; i < 4; i++) {
        uint32_t protocol = sram_protocols[i];
        char msg[288];

        firestarter_handle_t h_erase = make_handle(protocol, 0, CMD_ERASE);
        configure_memory(&h_erase);
        snprintf(msg, sizeof(msg),
            "Case group 5: CMD_ERASE on SRAM protocol 0x%02X must leave firestarter_operation_main "
            "NULL -- configure_sram's body is literally just a debug log",
            (unsigned)protocol);
        TEST_ASSERT_NULL_MESSAGE(h_erase.firestarter_operation_main, msg);

        firestarter_handle_t h_chip_id = make_handle(protocol, 0, CMD_CHECK_CHIP_ID);
        configure_memory(&h_chip_id);
        snprintf(msg, sizeof(msg),
            "Case group 5: CMD_CHECK_CHIP_ID on SRAM protocol 0x%02X must leave "
            "firestarter_operation_main NULL, subject to the same upstream chip_id==0 gate noted "
            "for 0x0D in case group 4",
            (unsigned)protocol);
        TEST_ASSERT_NULL_MESSAGE(h_chip_id.firestarter_operation_main, msg);
    }
}

/* Case group 6 — the unchanged path. Lets a reviewer SEE the absence of a
 * regression rather than infer it: configure_not_implemented protocols set
 * response_code = RESPONSE_CODE_ERROR themselves, so op_execute_function
 * already returns false and parse_command already emits MSG_ERR_SETUP -- the
 * op layer is never reached here, so this task's guard changes nothing. */
void test_case_group6_not_implemented_protocol_unchanged_no_double_error(void) {
    firestarter_handle_t h = make_handle(0x11, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case group 6 (D-08 item 7): a configure_not_implemented protocol (0x11) must still produce "
        "RESPONSE_CODE_ERROR from configure_memory itself -- op_execute_function(configure_memory, "
        "...) already returns false and parse_command already emits MSG_ERR_SETUP; the op layer is "
        "NEVER reached for this protocol -- no double error, no new frame");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_main,
        "Case group 6: configure_not_implemented leaves firestarter_operation_main NULL, unchanged "
        "by this task");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    /* 13 protocol-positive tests (one per KNOWN_PROTOCOLS entry) */
    RUN_TEST(test_protocol_0x06_dispatches_nor_unlock);
    RUN_TEST(test_protocol_0x05_dispatches_5v_page);
    RUN_TEST(test_protocol_0x35_dispatches_5v_page);
    RUN_TEST(test_protocol_0x39_dispatches_5v_page);
    RUN_TEST(test_protocol_0x07_dispatches_eprom);
    RUN_TEST(test_protocol_0x08_dispatches_eprom);
    RUN_TEST(test_protocol_0x0B_dispatches_eprom);
    RUN_TEST(test_protocol_0x0E_dispatches_sram);
    RUN_TEST(test_protocol_0x27_dispatches_sram);
    RUN_TEST(test_protocol_0x28_dispatches_sram);
    RUN_TEST(test_protocol_0x29_dispatches_sram);
    RUN_TEST(test_protocol_0x10_dispatches_flash_intel);
    RUN_TEST(test_protocol_0x0D_dispatches_eeprom28c);

    /* FIX-02A: CMD_CHECK_CHIP_ID dispatch tests (RED before flash_5v_page.cpp fix) */
    RUN_TEST(test_5v_page_check_chip_id_0x05_sets_operation);
    RUN_TEST(test_5v_page_check_chip_id_0x35_sets_operation);
    RUN_TEST(test_5v_page_check_chip_id_0x39_sets_operation);

    RUN_TEST(test_case_group1_read_write_verify_never_null_main_for_any_protocol);
    RUN_TEST(test_case_group2_sdp_cmds_null_main_for_every_non_0x0d_protocol);
    RUN_TEST(test_case_group3_sdp_cmds_dispatch_on_0x0d_with_null_init_end);
    RUN_TEST(test_case_group4a_0x0d_erase_dispatches_to_a_real_op_erase03);
    RUN_TEST(test_case_group4b_0x0d_chip_id_null_main_devtest01);
    RUN_TEST(test_case_group5_sram_erase_chip_id_null_main);
    RUN_TEST(test_case_group6_not_implemented_protocol_unchanged_no_double_error);

    return UNITY_END();
}
