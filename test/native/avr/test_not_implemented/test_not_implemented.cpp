/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * dispatch unit tests for configure_not_implemented() and
 * fail-closed dispatch arms.
 *
 * Tests assert RESPONSE_CODE_ERROR and all-three-NULL op pointers (unlike
 * the sibling test_configure_memory.cpp which avoids pointer checks because
 * configure_sram() is a stub with NULL firestarter_operation_init).
 * The not-implemented handler is self-contained and always leaves all
 * three pointers NULL — pointer assertions are safe here.
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
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn(1);
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

void test_protocol_0x11_fwh_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x11, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_0x2A_gal_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x2A, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_0x2B_gal_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x2B, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_0x2C_pld_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x2C, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_unknown_nonzero_protocol_0x99_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x99, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_zero_fail_closes_not_implemented(void) {
    firestarter_handle_t h = make_handle(0, 0, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_0x0C_adjacent_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x0C, 0, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
}

void test_protocol_0x0F_adjacent_not_implemented(void) {
    firestarter_handle_t h = make_handle(0x0F, 0, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_init);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
    TEST_ASSERT_NULL(h.firestarter_operation_end);
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
