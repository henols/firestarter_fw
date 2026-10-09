/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Two-tier assertions:
 *   1. Direct handler test (configure_sram called standalone): bus_recording_count()
 *      == 0 — documents the handler is a pure no-op with zero side-effects.
 *   2. Full dispatch test (configure_memory): h.firestarter_operation_init == NULL,
 *      response_code != ERROR, and no VPP-enable CTL bits in the recording —
 *      proves SRAM never reaches the VPP regulator through any dispatch path.
 *
 * Protocols covered: 0x0E (SRAM_5V), 0x27 (SRAM_NVRAM), 0x28, 0x29.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <stdio.h>

extern "C" {
#include "memory.h"
#include "sram.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" rurp_register_t recorded_data(int i);

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = protocol;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.mem_size   = 2048; /* 2 KB (6116 SRAM) */
    return h;
}

void test_sram_handler_direct_0x0E_records_zero_writes(void) {
    firestarter_handle_t h = make_handle(0x0E, CMD_READ);
    h.firestarter_set_control_register = NULL; /* not needed for direct test */
    configure_sram(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_sram must not set RESPONSE_CODE_ERROR");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bus_recording_count(),
        "configure_sram (sram.cpp no-op) must not issue any register writes");
}

void test_sram_handler_direct_0x27_records_zero_writes(void) {
    firestarter_handle_t h = make_handle(0x27, CMD_READ);
    h.firestarter_set_control_register = NULL;
    configure_sram(&h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bus_recording_count(),
        "configure_sram 0x27 must not issue any register writes (no-op state)");
}

void test_sram_handler_direct_0x28_records_zero_writes(void) {
    firestarter_handle_t h = make_handle(0x28, CMD_READ);
    h.firestarter_set_control_register = NULL;
    configure_sram(&h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bus_recording_count(),
        "configure_sram 0x28 must not issue any register writes (no-op state)");
}

void test_sram_handler_direct_0x29_records_zero_writes(void) {
    firestarter_handle_t h = make_handle(0x29, CMD_READ);
    h.firestarter_set_control_register = NULL;
    configure_sram(&h);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bus_recording_count(),
        "configure_sram 0x29 must not issue any register writes (no-op state)");
}

/* ─── Full dispatch tests: configure_memory for SRAM — no VPP-enable bits ──
 * configure_memory calls mem_util_set_address (address-only register writes),
 * then configure_sram (no additional writes, no function-pointer wiring).
 * Assert: response_code != ERROR, operation_init == NULL, no VPP-enable bits. */

void test_sram_dispatch_0x0E_no_vpp_no_init(void) {
    firestarter_handle_t h = make_handle(0x0E, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x0E SRAM CMD_READ");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init,
        "configure_sram must NOT wire firestarter_operation_init (no-op state)");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE((uint8_t)CTRL_VPP_REGULATOR_ENABLE, recorded_data(i),
                "SRAM dispatch must NOT set CTRL_VPP_REGULATOR_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE((uint8_t)CTRL_VPP_P1_ENABLE, recorded_data(i),
                "SRAM dispatch must NOT set CTRL_VPP_P1_ENABLE");
        }
    }
}

void test_sram_dispatch_0x27_no_vpp_no_init(void) {
    firestarter_handle_t h = make_handle(0x27, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x27 SRAM CMD_READ");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init,
        "configure_sram must NOT wire firestarter_operation_init (no-op state)");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE((uint8_t)CTRL_VPP_REGULATOR_ENABLE, recorded_data(i),
                "SRAM 0x27 dispatch must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

/* Every HV control bit at the full register width. On the wide layout
 * CTRL_VPP_VPE_DROP_ENABLE is above bit 7, so an 8-bit mask would miss it. */
static const rurp_register_t kHvBits = (rurp_register_t)(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE |
                                                         CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE |
                                                         CTRL_VPP_VPE_DROP_ENABLE);

void test_sram_dispatch_all_protocols_and_commands_assert_no_hv(void) {
    const uint32_t protocols[] = {0x0E, 0x27, 0x28, 0x29};
    const uint8_t commands[] = {CMD_READ, CMD_WRITE};
    for (unsigned p = 0; p < sizeof(protocols) / sizeof(protocols[0]); p++) {
        for (unsigned c = 0; c < sizeof(commands) / sizeof(commands[0]); c++) {
            clear_bus_recording();
            firestarter_handle_t h = make_handle(protocols[p], commands[c]);
            configure_memory(&h);
            char msg[48];
            snprintf(msg, sizeof(msg), "protocol 0x%02X cmd %u", (unsigned)protocols[p], (unsigned)commands[c]);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, msg);
            TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init, msg);
            for (int i = 0; i < bus_recording_count(); i++) {
                if (recorded_reg(i) == CONTROL_REGISTER) {
                    TEST_ASSERT_BITS_LOW_MESSAGE(kHvBits, recorded_data(i), msg);
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_sram_handler_direct_0x0E_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x27_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x28_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x29_records_zero_writes);

    /* Full dispatch tests: configure_memory for SRAM — no VPP, no init wiring */
    RUN_TEST(test_sram_dispatch_0x0E_no_vpp_no_init);
    RUN_TEST(test_sram_dispatch_0x27_no_vpp_no_init);
    RUN_TEST(test_sram_dispatch_all_protocols_and_commands_assert_no_hv);

    return UNITY_END();
}
