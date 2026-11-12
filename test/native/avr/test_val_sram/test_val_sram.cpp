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

#include "../_shared/bus_recording.h"

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

/* Issue #120: real range behavior, not source-text assertions. The start-address
 * refusal before configure_memory() is in the decoder (refusal ADDRESS_RANGE),
 * covered by the generated command vectors. */
#include "memory_utils.h"
#include "messages.h"
void memory_read_execute(firestarter_handle_t* handle);
void memory_write_execute(firestarter_handle_t* handle);
void memory_verify_execute(firestarter_handle_t* handle);

static unsigned issue120_reads;
static unsigned issue120_writes;
static uint8_t issue120_read(firestarter_handle_t*, uint32_t) {
    ++issue120_reads;
    return 0x5A;
}
static void issue120_write(firestarter_handle_t*, uint32_t, uint8_t) {
    ++issue120_writes;
}
static firestarter_handle_t issue120_handle(uint32_t address, uint32_t mem_size) {
    firestarter_handle_t h = {};
    h.cmd = CMD_READ;
    h.address = address;
    h.mem_size = mem_size;
    h.response_code = RESPONSE_CODE_OK;
    h.firestarter_get_data = issue120_read;
    h.firestarter_set_data = issue120_write;
    return h;
}

void test_issue120_range_edges_do_not_wrap(void) {
    TEST_ASSERT_TRUE(mem_util_range_fits(1023, 1, 1024));
    TEST_ASSERT_TRUE(mem_util_range_fits(1024, 0, 1024));
    TEST_ASSERT_FALSE(mem_util_range_fits(1024, 1, 1024));
    TEST_ASSERT_FALSE(mem_util_range_fits(1025, 0, 1024));
    TEST_ASSERT_FALSE(mem_util_range_fits(UINT32_MAX, 1, UINT32_MAX));
    TEST_ASSERT_FALSE(mem_util_range_fits(UINT32_MAX - 1, 3, UINT32_MAX));
    TEST_ASSERT_TRUE(mem_util_range_fits(UINT32_MAX - 1, 1, UINT32_MAX));
    TEST_ASSERT_FALSE(mem_util_range_fits(510, 3, 512));
    TEST_ASSERT_TRUE(mem_util_range_fits(510, 2, 512));
}

void test_issue120_read_out_of_range_does_not_access_bus(void) {
    issue120_reads = 0;
    firestarter_handle_t h = issue120_handle(1025, 1024);
    h.data_size = 3;
    memory_read_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(0, h.data_size);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_reads);

    h = issue120_handle(UINT32_MAX, 1024);
    memory_read_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_reads);
}

void test_issue120_read_end_is_empty_and_last_byte_is_read(void) {
    issue120_reads = 0;
    firestarter_handle_t h = issue120_handle(1024, 1024);
    memory_read_execute(&h);
    TEST_ASSERT_EQUAL_UINT32(0, h.data_size);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_reads);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_OK, h.response_code);
    h.address = 1023;
    memory_read_execute(&h);
    TEST_ASSERT_EQUAL_UINT32(1, h.data_size);
    TEST_ASSERT_EQUAL_UINT8(0x5A, (uint8_t)h.data_buffer[0]);
    TEST_ASSERT_EQUAL_UINT32(1, issue120_reads);
}

void test_issue120_direct_write_and_verify_refuse_out_of_range(void) {
    issue120_reads = 0;
    issue120_writes = 0;
    firestarter_handle_t h = issue120_handle(UINT32_MAX - 1, UINT32_MAX);
    h.cmd = CMD_WRITE;
    h.data_size = 3;
    memory_write_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_writes);

    h = issue120_handle(510, 1024);
    h.cmd = CMD_WRITE;
    h.region_end = 512;
    h.data_size = 3;
    memory_write_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_writes);

    h = issue120_handle(UINT32_MAX, 1024);
    h.data_size = 1;
    memory_verify_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(0, issue120_reads);
}

void test_issue120_direct_write_accepts_valid_end_boundary(void) {
    issue120_writes = 0;
    firestarter_handle_t h = issue120_handle(1023, 1024);
    h.cmd = CMD_WRITE;
    h.data_size = 1;
    h.data_buffer[0] = 0x42;
    memory_write_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(1, issue120_writes);

    h.address = 1024;
    h.data_size = 0;
    memory_write_execute(&h);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_EQUAL_UINT32(1, issue120_writes);
}

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

/* mem_util_operation_end: region_end 0 means the whole device, a region end
 * inside the device is kept, and one past the device is clamped to mem_size. */
void test_operation_end_resolves_and_clamps_region_end(void) {
    firestarter_handle_t h = {};
    h.mem_size = 1024;
    const struct { uint32_t region_end; uint32_t expected; const char* name; } cases[] = {
        {0, 1024, "region_end 0 is the whole device"},
        {1, 1, "a region end inside the device is kept"},
        {700, 700, "a region end inside the device is kept"},
        {1024, 1024, "a region end at mem_size is kept"},
        {1025, 1024, "a region end past mem_size is clamped"},
        {0xFFFFFFFFUL, 1024, "the largest region end is clamped"},
    };
    for (const auto& c : cases) {
        h.region_end = c.region_end;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.expected, mem_util_operation_end(&h), c.name);
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

    RUN_TEST(test_issue120_range_edges_do_not_wrap);
    RUN_TEST(test_issue120_read_out_of_range_does_not_access_bus);
    RUN_TEST(test_issue120_read_end_is_empty_and_last_byte_is_read);
    RUN_TEST(test_issue120_direct_write_and_verify_refuse_out_of_range);
    RUN_TEST(test_issue120_direct_write_accepts_valid_end_boundary);
    RUN_TEST(test_operation_end_resolves_and_clamps_region_end);
    return UNITY_END();
}
