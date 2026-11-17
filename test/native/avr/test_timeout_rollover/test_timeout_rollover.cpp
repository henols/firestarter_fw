/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Drives the real firmware poll loops with a millis() clock that starts just
 * before the 32-bit counter wraps. Each loop must time out after its full
 * interval: not at once (the old `millis() > start + N` form, where
 * start + N wraps to a small number) and not never.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "flash_utils.h"
#include "operation_utils.h"
#include "rurp_shield.h"

using namespace fakeit;

extern "C" void set_mock_read_data_fn(uint8_t (*fn)(void));

/* A poll loop that never ends would hang the suite. The clock throws
 * after this many reads so the test fails instead. */
static const uint32_t kMaxClockReads = 100000U;
static const uint32_t kStartNearWrap = UINT32_MAX - 40U;

struct ClockRunaway {};

static uint32_t s_now;
static uint32_t s_reads;
static uint32_t s_release_after_ms;
static uint32_t s_started;

static uint32_t elapsed() { return s_now - s_started; }

static void start_clock(uint32_t step_ms, uint32_t start = kStartNearWrap) {
    s_now = start;
    s_started = start;
    s_reads = 0;
    When(Method(ArduinoFake(), millis)).AlwaysDo([step_ms]() -> unsigned long {
        if (++s_reads > kMaxClockReads) {
            throw ClockRunaway();
        }
        uint32_t now = s_now;
        s_now += step_ms;
        return now;
    });
}

void setUp(void) {
    ArduinoFakeReset();
    When(Method(ArduinoFake(), delay)).AlwaysDo([](unsigned long ms) { s_now += (uint32_t)ms; });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(Serial), available)).AlwaysReturn(0);
    set_mock_read_data_fn(NULL);
    s_release_after_ms = 0;
}

void tearDown(void) {
}

static void noop_set_ctrl_reg(struct firestarter_handle*, rurp_register_t, bool) {}
static void noop_set_data(struct firestarter_handle*, uint32_t, uint8_t) {}
static void noop_set_address(struct firestarter_handle*, uint32_t) {}
static uint8_t busy_status(struct firestarter_handle*, uint32_t) { return 0x00; }

/* The flash waits read through firestarter_get_data; route that to the
 * suite's read function so each case picks what the part answers. */
static uint8_t read_mock(struct firestarter_handle*, uint32_t) { return rurp_read_data_buffer(); }

static firestarter_handle_t make_handle(void) {
    firestarter_handle_t h = {};
    h.response_code = RESPONSE_CODE_OK;
    h.firestarter_set_control_register = noop_set_ctrl_reg;
    h.firestarter_set_data = noop_set_data;
    h.firestarter_set_address = noop_set_address;
    h.firestarter_get_data = busy_status;
    return h;
}

/* DQ7 never matches the expected byte, so only the 150 ms timeout ends the poll. */
static uint8_t dq7_low(void) { return 0x00; }

/* DQ7 matches once 149 ms have passed: one millisecond before the timeout. */
static uint8_t dq7_high_after_release(void) {
    return elapsed() >= s_release_after_ms ? 0x80 : 0x00;
}

/* DQ7 matches on every other read only: one sample alone never ends the wait. */
static uint8_t s_toggle;
static uint8_t dq7_alternating(void) {
    s_toggle ^= 0x80;
    return s_toggle;
}

void test_data_poll_times_out_after_150_ms_across_the_wrap(void) {
    set_mock_read_data_fn(dq7_low);
    start_clock(1);
    firestarter_handle_t h = make_handle();
    h.firestarter_get_data = read_mock;
    bool done = true;
    try {
        done = flash_util_wait_program(&h, 0, 0x80);
    } catch (const ClockRunaway&) {
        TEST_FAIL_MESSAGE("data poll never timed out");
    }
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(150U, elapsed());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(152U, elapsed());
}

void test_data_poll_completes_just_before_the_timeout_across_the_wrap(void) {
    s_release_after_ms = 149U;
    set_mock_read_data_fn(dq7_high_after_release);
    start_clock(1);
    firestarter_handle_t h = make_handle();
    h.firestarter_get_data = read_mock;
    TEST_ASSERT_TRUE(flash_util_wait_program(&h, 0, 0x80));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
}

void test_data_poll_needs_two_consecutive_matches(void) {
    s_toggle = 0x00;
    set_mock_read_data_fn(dq7_alternating);
    start_clock(1);
    firestarter_handle_t h = make_handle();
    h.firestarter_get_data = read_mock;
    TEST_ASSERT_FALSE_MESSAGE(flash_util_wait_ready(&h, 0, 0x80, 150U),
        "a DQ7 match that the next read does not repeat must not end the wait");
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(150U, elapsed());
}

void test_erase_wait_times_out_after_9000_ms_across_the_wrap(void) {
    set_mock_read_data_fn(dq7_low);
    start_clock(1, kStartNearWrap - 4000U);
    s_started = kStartNearWrap - 4000U;
    firestarter_handle_t h = make_handle();
    h.firestarter_get_data = read_mock;
    bool done = true;
    try {
        done = flash_util_wait_erase(&h, 0x10000UL);
    } catch (const ClockRunaway&) {
        TEST_FAIL_MESSAGE("erase wait never timed out");
    }
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(FLASH_ERASE_TIMEOUT_MS, elapsed());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(FLASH_ERASE_TIMEOUT_MS + 2U, elapsed());
}

void test_ack_wait_times_out_after_1000_ms_across_the_wrap(void) {
    start_clock(1);
    firestarter_handle_t h = make_handle();
    bool acked = true;
    try {
        acked = op_wait_for_ack(&h);
    } catch (const ClockRunaway&) {
        TEST_FAIL_MESSAGE("ack wait never timed out");
    }
    TEST_ASSERT_FALSE(acked);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1000U, elapsed());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(1012U, elapsed());
}

void test_intel_status_poll_times_out_after_15000_ms_across_the_wrap(void) {
    firestarter_handle_t h = make_handle();
    h.protocol = 0x10;
    h.cmd = CMD_ERASE;
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL(h.firestarter_operation_main);
    /* configure_memory installs the real bus callbacks; keep the chip busy. */
    h.firestarter_set_control_register = noop_set_ctrl_reg;
    h.firestarter_set_data = noop_set_data;
    h.firestarter_get_data = busy_status;
    /* The rail comes up in erase init, so the erase polls at once. */
    start_clock(1);
    try {
        h.firestarter_operation_main(&h);
    } catch (const ClockRunaway&) {
        TEST_FAIL_MESSAGE("status-register poll never timed out");
    }
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(15000U, elapsed());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(15002U, elapsed());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_data_poll_times_out_after_150_ms_across_the_wrap);
    RUN_TEST(test_data_poll_completes_just_before_the_timeout_across_the_wrap);
    RUN_TEST(test_data_poll_needs_two_consecutive_matches);
    RUN_TEST(test_erase_wait_times_out_after_9000_ms_across_the_wrap);
    RUN_TEST(test_ack_wait_times_out_after_1000_ms_across_the_wrap);
    RUN_TEST(test_intel_status_poll_times_out_after_15000_ms_across_the_wrap);
    return UNITY_END();
}
