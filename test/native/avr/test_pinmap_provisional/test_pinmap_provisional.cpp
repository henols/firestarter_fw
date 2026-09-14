/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Protocol choice, deliberate: every per-command case uses
 * PROTO_EPROM_28PIN (0x07), a protocol that dispatches successfully to a
 * REAL handler (configure_eprom(), which enables the 12V VPP boost
 * regulator) when the command is admitted and the guard does not fire.
 * Refusing under a protocol that would otherwise configure real hardware
 * is a stronger proof than refusing under an already-unrecognized
 * protocol.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <stdio.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "proto_constants.h"
#include "rurp_pinmap_guard.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    /* Stub Serial.write / Serial.flush so any LOG_* call reached (the
     * refusal path calls LOG_ERROR_ID_U8) doesn't abort. Mirrors
     * test_configure_memory.cpp:39-46 and test_cmd_admission.cpp:34-42. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();

    /* LOAD-BEARING, do not remove as "unused" (test_cmd_admission.cpp:44-50
     * precedent): configure_eprom()/its dependents may reach these; a
     * SIGABRT in this brand-new suite is Pitfall 5, not the deferred
     * test_flash_intel_vpp Unity-teardown flake. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
}

void tearDown(void) {
}

/* Build a zero-initialized handle with only the fields these tests need.
 * Mirrors test_configure_memory.cpp's make_handle() shape. */
static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    return h;
}

static void assert_cmd_refused(uint8_t cmd, const char* cmd_name) {
    firestarter_handle_t h = make_handle(PROTO_EPROM_28PIN, cmd);
    configure_memory(&h);

    char msg[96];
    snprintf(msg, sizeof(msg), "%s must be refused under a provisional pin map (response_code)", cmd_name);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, msg);

    snprintf(msg, sizeof(msg), "%s: firestarter_operation_init must stay NULL when refused", cmd_name);
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init, msg);
    snprintf(msg, sizeof(msg), "%s: firestarter_operation_main must stay NULL when refused", cmd_name);
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_main, msg);
    snprintf(msg, sizeof(msg), "%s: firestarter_operation_end must stay NULL when refused", cmd_name);
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_end, msg);
}

void test_pinmap_provisional_refuses_cmd_read(void) {
    assert_cmd_refused(CMD_READ, "CMD_READ");
}

void test_pinmap_provisional_refuses_cmd_write(void) {
    assert_cmd_refused(CMD_WRITE, "CMD_WRITE");
}

void test_pinmap_provisional_refuses_cmd_erase(void) {
    assert_cmd_refused(CMD_ERASE, "CMD_ERASE");
}

void test_pinmap_provisional_refuses_cmd_blank_check(void) {
    assert_cmd_refused(CMD_BLANK_CHECK, "CMD_BLANK_CHECK");
}

void test_pinmap_provisional_refuses_cmd_check_chip_id(void) {
    assert_cmd_refused(CMD_CHECK_CHIP_ID, "CMD_CHECK_CHIP_ID");
}

void test_pinmap_provisional_refuses_cmd_verify(void) {
    assert_cmd_refused(CMD_VERIFY, "CMD_VERIFY");
}

void test_pinmap_provisional_refuses_cmd_sdp_unlock(void) {
    assert_cmd_refused(CMD_SDP_UNLOCK, "CMD_SDP_UNLOCK");
}

void test_pinmap_provisional_refuses_cmd_sdp_lock(void) {
    assert_cmd_refused(CMD_SDP_LOCK, "CMD_SDP_LOCK");
}

void test_pinmap_provisional_refuses_cmd_lock_status(void) {
    assert_cmd_refused(CMD_LOCK_STATUS, "CMD_LOCK_STATUS");
}

void test_pinmap_refuses_predicate_truth_table(void) {
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_READ), "CMD_READ must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_WRITE), "CMD_WRITE must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_ERASE), "CMD_ERASE must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_BLANK_CHECK), "CMD_BLANK_CHECK must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_CHECK_CHIP_ID), "CMD_CHECK_CHIP_ID must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_VERIFY), "CMD_VERIFY must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_SDP_UNLOCK), "CMD_SDP_UNLOCK must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_SDP_LOCK), "CMD_SDP_LOCK must be in the refused set");
    TEST_ASSERT_TRUE_MESSAGE(rurp_pinmap_refuses(CMD_LOCK_STATUS), "CMD_LOCK_STATUS must be in the refused set");

    /* Named negative control: a command outside is_memory_cmd()'s set must
     * NOT be refused by the predicate. */
    TEST_ASSERT_FALSE_MESSAGE(rurp_pinmap_refuses(CMD_FW_VERSION),
        "CMD_FW_VERSION is outside is_memory_cmd()'s set and must not be refused by the predicate");
}

/* Negative control 2 -- the SCOPED-not-blanket proof: an identity/config
 * command must NOT be refused end-to-end through configure_memory(), so
 * the board stays discoverable under a provisional pin map. Uses the same
 * real-handler protocol (PROTO_EPROM_28PIN) as the refused cases above, so
 * the only variable is the command. */
void test_pinmap_provisional_does_not_refuse_identity_command(void) {
    firestarter_handle_t h = make_handle(PROTO_EPROM_28PIN, CMD_FW_VERSION);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "CMD_FW_VERSION must NOT be refused under a provisional pin map -- the board "
        "must stay discoverable (identity/config commands are outside D-12's set)");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_pinmap_provisional_refuses_cmd_read);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_write);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_erase);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_blank_check);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_check_chip_id);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_verify);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_sdp_unlock);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_sdp_lock);
    RUN_TEST(test_pinmap_provisional_refuses_cmd_lock_status);

    /* Two negative controls */
    RUN_TEST(test_pinmap_refuses_predicate_truth_table);
    RUN_TEST(test_pinmap_provisional_does_not_refuse_identity_command);

    return UNITY_END();
}
