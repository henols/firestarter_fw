/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the SRAM family.
 * Phase 87 Plan 03 — INV-07 gap-fill assertion (NAME-03 / SAFE-02).
 * HARN-01 / D-07 / T-71-SRAM-FALSE / Pitfall 2 (from 71-RESEARCH.md).
 *
 * Documents the CURRENT SRAM no-op state (sram.cpp:15-17 is a one-liner with
 * no function-pointer wiring). This is a GREEN baseline for Phase 71.
 *
 * Phase 74 FIX-01 will change this suite: once VAL-06 (Phase 73) resolves
 * whether SRAM should perform register writes, the assertions here will be
 * updated. Until then:
 *   - configure_sram itself records ZERO register writes (pure no-op).
 *   - configure_memory for SRAM protocols records only address-setup writes
 *     (LSB/MSB/CONTROL from mem_util_set_address) with NO VPP-enable bits.
 *   - h.firestarter_operation_init is NULL after configure_memory for SRAM
 *     (configure_sram does not wire up an init function).
 *
 * Two-tier assertions:
 *   1. Direct handler test (configure_sram called standalone): bus_recording_count()
 *      == 0 — documents the handler is a pure no-op with zero side-effects.
 *   2. Full dispatch test (configure_memory): h.firestarter_operation_init == NULL,
 *      response_code != ERROR, and no VPP-enable CTL bits in the recording —
 *      proves SRAM never reaches the VPP regulator through any dispatch path.
 *
 * Protocols covered: 0x0E (SRAM_5V), 0x27 (SRAM_NVRAM), 0x28, 0x29.
 *
 * VAL-06 deferred: Whether SRAM should write any registers is an open question.
 * This suite's GREEN state pins the current no-op as the Phase-71 baseline.
 *
 * INV gap-fill assertion added by Phase 87 Plan 03 (SAFE-02 third target):
 *   INV-07: test_inv07_sram_fm1608_routes_to_sram
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

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
extern "C" uint8_t recorded_data(int i);

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

/* ─── Direct handler tests: configure_sram records ZERO writes ─────────────
 * These tests call configure_sram standalone (no configure_memory overhead).
 * configure_sram is sram.cpp:15-17: LOG_DEBUG_ID_SUB(DBG_CONFIGURING_SRAM) —
 * a single LOG call with NO rurp_write_to_register calls.
 * Phase 74 FIX-01 will update these GREEN tests when VAL-06 is resolved. */

void test_sram_handler_direct_0x0E_records_zero_writes(void) {
    /* configure_sram is currently a no-op (sram.cpp:15-17) — assert zero writes.
     * Phase 74 FIX-01 will change this; until then GREEN = confirmed no-op. */
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

/* ─── Phase 87 Plan 03: INV-07 gap-fill assertion (SAFE-02) ─────────────────── */

/* INV-07 — FM1608 routes to configure_sram() as SRAM_STD/FRAM (algorithm=0x28).
 * SAFE-02 third target: grep -rn INV-07 must hit doc + handler + this test.
 * Asserts: protocol 0x28 (SRAM_STD — the algorithm that FM1608 resolves to after
 * Phase-86 variant decode) dispatches to configure_sram() and NEVER reaches
 * configure_eprom(). This is the BLOCKER-2 mitigation: FRAM must not see the
 * VPP boost regulator (enabling VPP on a 5V FRAM would destroy it).
 *
 * FM1608 identity: raw infoic.xml type=4/proto=0x07/variant=0x4126 → algorithm=0x28
 * (Phase-86 variant decode, Ramtron FRAM class discriminator in high byte 0x41).
 * After Phase-86 DB regeneration, the JSON "algorithm" field is 0x28 so the
 * firmware receives protocol=0x28 and dispatches to configure_sram().
 *
 * Verification (two observable behaviors):
 *   1. configure_memory with protocol=0x28 succeeds and does NOT set VPP-enable bits.
 *   2. firestarter_operation_init is NULL (configure_sram does not wire an init fn).
 * Both confirm the 0x28 → configure_sram() path (not configure_eprom()).
 * Source: sram.cpp INV-07 header block; PROTOCOLS.md §1.10 NAME-04 call-out + §3 INV-07 row. */
void test_inv07_sram_fm1608_routes_to_sram(void) {
    /* FM1608 algorithm=0x28 (SRAM_STD): must route to configure_sram, not configure_eprom.
     * configure_sram is a no-op (zero register writes, no init wiring). */
    firestarter_handle_t h = make_handle(0x28, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-07: configure_memory must not error on 0x28 (FM1608 SRAM_STD/FRAM algorithm)");
    TEST_ASSERT_NULL_MESSAGE(h.firestarter_operation_init,
        "INV-07: 0x28 must NOT wire firestarter_operation_init (configure_sram no-op, not configure_eprom)");
    /* No VPP-enable bits: configure_sram touches no VPP control lines (BLOCKER-2 mitigation). */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE((uint8_t)CTRL_VPP_REGULATOR_ENABLE, recorded_data(i),
                "INV-07: 0x28 (FM1608/FRAM) dispatch must NOT set CTRL_VPP_REGULATOR_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE((uint8_t)CTRL_VPP_P1_ENABLE, recorded_data(i),
                "INV-07: 0x28 (FM1608/FRAM) dispatch must NOT set CTRL_VPP_P1_ENABLE");
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* Direct handler tests: configure_sram standalone = zero register writes
     * (pins the Phase-71 no-op baseline; Phase 74 FIX-01 will revisit) */
    RUN_TEST(test_sram_handler_direct_0x0E_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x27_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x28_records_zero_writes);
    RUN_TEST(test_sram_handler_direct_0x29_records_zero_writes);

    /* Full dispatch tests: configure_memory for SRAM — no VPP, no init wiring */
    RUN_TEST(test_sram_dispatch_0x0E_no_vpp_no_init);
    RUN_TEST(test_sram_dispatch_0x27_no_vpp_no_init);

    /* Phase 87 Plan 03: INV-07 gap-fill assertion (SAFE-02 / NAME-03) */
    RUN_TEST(test_inv07_sram_fm1608_routes_to_sram);

    return UNITY_END();
}
