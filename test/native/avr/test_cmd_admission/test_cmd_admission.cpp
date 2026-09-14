/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Every case here asserts on is_memory_cmd() ONLY — never on dispatch,
 * configure_memory(), or any handler. That keeps this suite orthogonal to
 * test_dispatch (which tests what a command DOES) and focused purely on
 * what the admission gate ADMITS.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <stdio.h>

#include "firestarter.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    /* Stub Serial.write / Serial.flush so any LOG_* call reached indirectly
     * (e.g. via a widened build_src_filter in a later plan) doesn't abort —
     * mirrors test_configure_memory.cpp:36-62. This suite's own assertions
     * never touch Serial output. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();

    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
}

void tearDown(void) {
}

void test_admission_truth_table_over_every_cmd_value(void) {
    for (int c = 0; c <= 255; c++) {
        bool expected;
        switch (c) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 9:
            case 10:
            case 16:
                expected = true;
                break;
            default:
                expected = false;
                break;
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "is_memory_cmd(%d) mismatch", c);
        TEST_ASSERT_EQUAL_MESSAGE(expected, is_memory_cmd((uint8_t)c), msg);
    }
}

void test_admission_count_is_exactly_nine(void) {
    int count = 0;
    for (int c = 0; c <= 255; c++) {
        if (is_memory_cmd((uint8_t)c)) {
            count++;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(9, count,
        "is_memory_cmd() must admit exactly nine of the 256 possible uint8_t values (Phase 151, LOCK-02)");
}

/* Case 2 — cmd 7 and 8 (CMD_DEV_ADDRESS / CMD_DEV_REGISTER) are excluded.
 * These two macros are themselves defined inside a `#ifdef DEV_TOOLS` block
 * in firestarter.h:42-45, so this suite MUST NOT name them — doing so would
 * fail to compile under [env:native_nodevtools]. Bare numeric literals are
 * used instead, exactly the idiom firestarter_app's
 * test_revision_constants_parity.py:110-112 already uses for the identical
 * reason: "CMD_DEV_ADDRESS and CMD_DEV_REGISTER are #ifdef DEV_TOOLS in
 * firmware -- assert Python values as standalone literals only."
 * refusal in a release build, unchanged. */
void test_admission_rejects_dev_tool_ordinals_7_and_8(void) {
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(7),
        "cmd 7 (CMD_DEV_ADDRESS, DEV_TOOLS-conditional) must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(8),
        "cmd 8 (CMD_DEV_REGISTER, DEV_TOOLS-conditional) must not be admitted");
}

void test_admission_boundary_around_cmd_lock_status(void) {
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(15),
        "cmd 15 (CMD_HW_VERSION, highest pre-existing command) must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(17),
        "cmd 17 (first value above the new ninth admission, 16) must not be admitted");
}

void test_admission_rejects_cmd_idle_zero(void) {
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_IDLE),
        "CMD_IDLE (0) must not be admitted -- RESEARCH F-B2's third delta");
}

/* Case 4 — a sample of non-memory commands unconditionally defined in both
 * build configurations. Safe to name by macro because none is
 * DEV_TOOLS-gated. */
void test_admission_rejects_non_memory_commands(void) {
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_READ_VPP), "CMD_READ_VPP must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_READ_VPE), "CMD_READ_VPE must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_FW_VERSION), "CMD_FW_VERSION must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_CONFIG), "CMD_CONFIG must not be admitted");
    TEST_ASSERT_FALSE_MESSAGE(is_memory_cmd(CMD_HW_VERSION), "CMD_HW_VERSION must not be admitted");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_admission_truth_table_over_every_cmd_value);
    RUN_TEST(test_admission_count_is_exactly_nine);
    RUN_TEST(test_admission_rejects_dev_tool_ordinals_7_and_8);
    RUN_TEST(test_admission_boundary_around_cmd_lock_status);
    RUN_TEST(test_admission_rejects_cmd_idle_zero);
    RUN_TEST(test_admission_rejects_non_memory_commands);

    return UNITY_END();
}
