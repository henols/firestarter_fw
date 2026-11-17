/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * dispatch unit tests for configure_not_implemented() and
 * fail-closed dispatch arms.
 *
 * Tests assert RESPONSE_CODE_ERROR, all-three-NULL op pointers, and the one
 * MSG_ERR_PROTOCOL_NOT_IMPLEMENTED frame that carries the protocol.
 */
#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "messages.h"

#include <vector>

#include "../_shared/frame_capture.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    captured_frames.clear();
    capture_serial_writes();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t)))
        .AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t mem_type, uint8_t cmd) {
    (void)mem_type;
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    return h;
}

/* Dispatches `protocol` and asserts the fail-closed result: an error, all
 * three operation pointers NULL, and exactly one frame,
 * MSG_ERR_PROTOCOL_NOT_IMPLEMENTED with the protocol as its one u8 param. */
static void check_not_implemented(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = make_handle(protocol, 0, cmd);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);

    std::vector<uint8_t> ids;
    captured_frame_ids(&ids);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, ids.size(), "exactly one frame");
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_PROTOCOL_NOT_IMPLEMENTED, ids[0]);

    std::vector<std::vector<uint8_t>> params;
    captured_frame_params(&params);
    TEST_ASSERT_EQUAL_size_t(1, params.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, params[0].size(), "one u8 param");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)protocol, params[0][0], "the param is the protocol");
}

void test_protocol_0x11_fwh_not_implemented(void) {
    check_not_implemented(0x11, CMD_READ);
}

void test_protocol_0x2A_gal_not_implemented(void) {
    check_not_implemented(0x2A, CMD_READ);
}

void test_protocol_0x2B_gal_not_implemented(void) {
    check_not_implemented(0x2B, CMD_READ);
}

void test_protocol_0x2C_pld_not_implemented(void) {
    check_not_implemented(0x2C, CMD_READ);
}

void test_unknown_nonzero_protocol_0x99_not_implemented(void) {
    check_not_implemented(0x99, CMD_READ);
}

void test_protocol_zero_fail_closes_not_implemented(void) {
    check_not_implemented(0, CMD_READ);
}

void test_protocol_0x0C_adjacent_not_implemented(void) {
    check_not_implemented(0x0C, CMD_WRITE);
}

void test_protocol_0x0F_adjacent_not_implemented(void) {
    check_not_implemented(0x0F, CMD_WRITE);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    /* Named infeasibility arms */
    RUN_TEST(test_protocol_0x11_fwh_not_implemented);
    RUN_TEST(test_protocol_0x2A_gal_not_implemented);
    RUN_TEST(test_protocol_0x2B_gal_not_implemented);
    RUN_TEST(test_protocol_0x2C_pld_not_implemented);

    /* Generic catch-all */
    RUN_TEST(test_unknown_nonzero_protocol_0x99_not_implemented);

    /* SC#1: protocol == 0 fail-closed (no mem_type fallback) */
    RUN_TEST(test_protocol_zero_fail_closes_not_implemented);

    RUN_TEST(test_protocol_0x0C_adjacent_not_implemented);
    RUN_TEST(test_protocol_0x0F_adjacent_not_implemented);

    return UNITY_END();
}
