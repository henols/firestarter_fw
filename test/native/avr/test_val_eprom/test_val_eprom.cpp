/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the EPROM family.
 * Phase 87 Plan 03 — INV-01..INV-06/INV-08 gap-fill assertions (NAME-03 / SAFE-02).
 * Phase 88 Plan 01 — Golden register traces for 0x07/0x08/0x0B write + chip-id (P4)
 *                    (PRIM-01 / SAFE-02 / D-01 / D-02 / D-03 / D-04).
 * HARN-01 / D-07 / D-08 (verify-can-fail posture).
 *
 * Proves configure_eprom behavior BY SIDE-EFFECT via the recording bus stub:
 *
 *   POSITIVE tests (CMD_WRITE): configure_memory() + firestarter_operation_init()
 *     → eprom_write_init → eprom_generic_init → eprom_check_vpp
 *     → rurp_write_to_register(CONTROL_REGISTER, value | CTRL_VPP_REGULATOR_ENABLE)
 *     The recording must contain at least one CONTROL_REGISTER write with
 *     CTRL_VPP_REGULATOR_ENABLE set.
 *
 *   NEGATIVE CONTROL (CMD_READ, configure-only phase): configure_memory() alone
 *     (no firestarter_operation_init call). configure_memory calls
 *     mem_util_set_address(handle, 0) which writes LSB/MSB/CONTROL registers
 *     with only address bits — no VPP-enable bits. Asserts CTRL_VPP_REGULATOR_ENABLE
 *     NEVER appears in the recording (proves in-tier verify-can-fail per D-08).
 *
 * Protocols covered: 0x07 (EPROM_STD), 0x08 (EPROM_QUICK), 0x0B (EPROM_LEGACY).
 *
 * VPP mechanism (from eprom.cpp source — not guessed):
 *   0x07/0x08: CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE
 *   0x0B:      CTRL_VPP_REGULATOR_ENABLE only (direct VPE path)
 * Both paths set CTRL_VPP_REGULATOR_ENABLE — we assert the common bit.
 *
 * INV gap-fill assertions added by Phase 87 Plan 03 (SAFE-02 third target):
 *   INV-01: test_inv01_eprom_0x0B_direct_vpe_rail
 *   INV-02: test_inv02_eprom_0x0B_oe_vpp_read_skip
 *   INV-03: test_inv03_eprom_0x08_p1_as_vpp
 *   INV-05: test_inv05_eprom_vpp_skip_on_read
 *   INV-06: test_inv06_eprom_pulse_delay_defaults
 *   INV-08: test_inv08_eprom_warning5_decode_preserved
 *
 * NOTE: delay() and delayMicroseconds() must be mocked; eprom_check_vpp calls
 * delay(100), eprom_write_execute calls delay(500), memory_set_data/get_data
 * call delayMicroseconds().
 * Hardware revision stub returns 1 (non-REVISION_0) via host_stubs.cpp so
 * eprom_check_vpp does not take the REV0 early-return path.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
#include "rurp_shield.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"
#include "memory_utils.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

/* Golden-trace helper (assert_trace_eq + GOLDEN_BLESS print mode).
 * Included AFTER the extern "C" recording decls above (required include order). */
#include "../_shared/golden_trace.h"

/* Golden expected arrays (one per traced path).
 * Each .inc file is a comma-list of { reg, data } rows produced by GOLDEN_BLESS mode.
 * Header comment in each .inc names the input and documents the low-byte caveat. */
static const golden_entry_t golden_eprom_0x07_write[] = {
#include "golden_eprom_0x07_write.inc"
};
static const int golden_eprom_0x07_write_n =
    (int)(sizeof(golden_eprom_0x07_write) / sizeof(golden_eprom_0x07_write[0]));

static const golden_entry_t golden_eprom_0x08_write[] = {
#include "golden_eprom_0x08_write.inc"
};
static const int golden_eprom_0x08_write_n =
    (int)(sizeof(golden_eprom_0x08_write) / sizeof(golden_eprom_0x08_write[0]));

static const golden_entry_t golden_eprom_0x0B_write[] = {
#include "golden_eprom_0x0B_write.inc"
};
static const int golden_eprom_0x0B_write_n =
    (int)(sizeof(golden_eprom_0x0B_write) / sizeof(golden_eprom_0x0B_write[0]));

static const golden_entry_t golden_eprom_chip_id[] = {
#include "golden_eprom_chip_id.inc"
};
static const int golden_eprom_chip_id_n =
    (int)(sizeof(golden_eprom_chip_id) / sizeof(golden_eprom_chip_id[0]));

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by eprom_check_vpp (delay(100)) and eprom_write_execute
     * (delay(500)). Must be stubbed before any ArduinoFake virtual is called. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    /* delayMicroseconds() is called by memory_set_data (3µs settle) and
     * memory_get_data (read strobe). Needed by INV-03 execute-phase test. */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

/* Build a zero-initialized handle with protocol, cmd, and a VPP setpoint of 0
 * (so eprom_check_vpp voltage comparison does not error on 0 mV reading). */
static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = protocol;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv     = 0;  /* vpp setpoint=0 matches stub voltage=0: no warn/error */
    h.chip_id    = 0;  /* skip chip-ID branch */
    h.mem_size   = 65536; /* 64 KB — keeps blank_check from NULL-ptr in mock */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

/* ─── Helper: scan recording for CONTROL_REGISTER writes with VPP bit set ─── */
static bool recording_has_vpp_enable(uint8_t vpp_bit) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & vpp_bit)) {
            return true;
        }
    }
    return false;
}

/* NOTE: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION is defined —
 * it does not fit in uint8_t. The recording buffer stores uint8_t data values,
 * so CTRL_VPP_VPE_DROP_ENABLE cannot be detected via the 8-bit recording when
 * HARDWARE_REVISION is defined. Use CTRL_VPP_REGULATOR_ENABLE (0x80) and
 * CTRL_VPP_P1_ENABLE (0x08) which fit in 8 bits for VPP-enable detection. */
static bool recording_has_any_vpp_enable(void) {
    return recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE) ||
           recording_has_vpp_enable(CTRL_VPP_P1_ENABLE);
}

/* ─── POSITIVE tests: CMD_WRITE + init → VPP regulator must fire ─────────── */

/* Protocol 0x07 (EPROM_STD): write init must enable CTRL_VPP_REGULATOR_ENABLE. */
void test_eprom_0x07_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x07 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x07 write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* Protocol 0x08 (EPROM_QUICK): same mechanism as 0x07. */
void test_eprom_0x08_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x08 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x08 write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* Protocol 0x0B (EPROM_LEGACY): direct VPE path — CTRL_VPP_REGULATOR_ENABLE only. */
void test_eprom_0x0B_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x0B CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x0B write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* ─── NEGATIVE CONTROL: CMD_READ, configure-only — VPP must NOT fire ─────── */

/* For CMD_READ, only configure_memory() is called (no firestarter_operation_init
 * call). configure_memory routes to configure_eprom which only sets function
 * pointers, then mem_util_set_address writes LSB/MSB/CONTROL with address bits
 * only — no VPP-enable bits. This asserts the configure/dispatch phase alone
 * never enables VPP (D-08 verify-can-fail: this test goes RED if a regression
 * puts VPP enable inside configure_memory or configure_eprom itself). */
void test_eprom_0x07_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x07 CMD_READ");
    /* Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION defined —
     * it cannot be detected via uint8_t recording; check 8-bit-fit VPP bits only. */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x07 CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "configure_eprom 0x07 CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
        }
    }
}

void test_eprom_0x08_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x08 CMD_READ");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x08 CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

void test_eprom_0x0B_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x0B CMD_READ");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x0B CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

/* ─── Phase 87 Plan 03: INV-01..INV-06/INV-08 gap-fill assertions (SAFE-02) ─ */

/* INV-01 — 0x0B direct-VPE rail (no CTRL_VPP_VPE_DROP_ENABLE drop).
 * SAFE-02 third target: grep -rn INV-01 must hit doc + handler + this test.
 * Asserts: 0x0B write+init enables CTRL_VPP_REGULATOR_ENABLE (direct VPE path)
 * AND does NOT enable CTRL_VPP_P1_ENABLE — distinguishing the 0x0B rail from
 * the 0x08 P1-as-VPP path (INV-03).
 * Source: eprom.cpp eprom_check_vpp lines 266–268 (0x0B → REGULATOR_ENABLE only). */
void test_inv01_eprom_0x0B_direct_vpe_rail(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-01: configure_memory must not error on 0x0B CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "INV-01: 0x0B write must record CTRL_VPP_REGULATOR_ENABLE (direct VPE rail)");
    /* 0x0B uses direct VPE path — CTRL_VPP_P1_ENABLE (0x08) must NOT be set
     * during the init VPP-check phase (P1 is the 0x08 EPROM-QUICK path, INV-03). */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "INV-01: 0x0B init phase must NOT set CTRL_VPP_P1_ENABLE (direct VPE, not P1 path)");
        }
    }
}

/* INV-02 — 0x0B shared OE/VPP pin: read operations skip VPP enable.
 * SAFE-02 third target: grep -rn INV-02 must hit doc + handler + this test.
 * Asserts: CMD_READ configure-only phase for 0x0B records NO VPP-enable bits.
 * Enabling VPP on 0x0B during read would drive the shared OE/VPP pin high with
 * 12–25 V, damaging the logic output (2732/2516 OE/VPP-shared pin).
 * Source: eprom.cpp INV-02 header block; configure_eprom does not call eprom_check_vpp
 * during configure (VPP fires only from firestarter_operation_init). */
void test_inv02_eprom_0x0B_oe_vpp_read_skip(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-02: configure_memory must not error on 0x0B CMD_READ");
    /* Configure-only phase: no VPP-enable bits allowed in any CONTROL_REGISTER write
     * (VPP is suppressed for read operations on 0x0B to protect the shared OE/VPP pin). */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "INV-02: 0x0B CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE (OE/VPP shared pin)");
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "INV-02: 0x0B CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
        }
    }
}

/* INV-03 — 0x08 P1-as-VPP: routes VPP to socket pin 1 via CTRL_VPP_P1_ENABLE.
 * SAFE-02 third target: grep -rn INV-03 must hit doc + handler + this test.
 * Asserts: 0x08 write+execute with pins=32 / vpp_line=VPP_P1_32_DIP activates
 * CTRL_VPP_P1_ENABLE (0x08) in the recording (eprom_internal_set_control_register
 * flips CTRL_VPE_ENABLE → CTRL_VPP_P1_ENABLE when using_p1_as_vpp is true).
 * Source: eprom.cpp lines 367–373 (eprom_internal_set_control_register); memory_utils.h
 * using_p1_as_vpp (pins==32 && vpp_line==VPP_P1_32_DIP). */
void test_inv03_eprom_0x08_p1_as_vpp(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
    /* Set up 32-pin / VPP_P1_32_DIP bus config so using_p1_as_vpp() returns true.
     * This causes eprom_internal_set_control_register to flip CTRL_VPE_ENABLE → CTRL_VPP_P1_ENABLE
     * during program_mismatched_bytes, producing the P1-as-VPP recording. */
    h.pins = 32;
    h.bus_config.vpp_line = VPP_P1_32_DIP; /* 0x15 — 32-pin DIP VPP routing */
    h.data_size = 1;
    /* data_buffer[0] is 0 (zero-init); rurp_read_data_buffer() stub returns 0 →
     * verify_and_update_mask finds no mismatch after first program pass → loop exits. */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-03: configure_memory must not error on 0x08 CMD_WRITE");

    /* Call init (eprom_write_init → eprom_generic_init → eprom_check_vpp) then execute. */
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    /* Clear recording before execute so we isolate the execute-phase P1 bit. */
    clear_bus_recording();

    if (h.firestarter_operation_main) {
        h.firestarter_operation_main(&h);
    }
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-03: eprom_write_execute must not error on 1-byte zero write");
    /* eprom_write_execute sets REGULATOR_ENABLE first; program_mismatched_bytes then
     * requests CTRL_VPE_ENABLE which eprom_internal_set_control_register flips to
     * CTRL_VPP_P1_ENABLE (0x08) because using_p1_as_vpp(handle) is true. */
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_P1_ENABLE),
        "INV-03: 0x08 execute with 32-pin/VPP_P1_32_DIP must record CTRL_VPP_P1_ENABLE "
        "(eprom_internal_set_control_register CTRL_VPE_ENABLE→CTRL_VPP_P1_ENABLE flip)");
}

/* INV-05 — VPP-skip-on-read: VPP NOT enabled for CMD_READ or CMD_BLANK_CHECK.
 * SAFE-02 third target: grep -rn INV-05 must hit doc + handler + this test.
 * Asserts: configure-only phase for CMD_READ across all three EPROM protocols
 * records NO VPP-enable bits (CTRL_VPP_REGULATOR_ENABLE or CTRL_VPP_P1_ENABLE).
 * CMD_READ does not call firestarter_operation_init in the production read path,
 * so VPP is never enabled during reads (protects OE/VPP-shared pins on 0x0B parts
 * and avoids regulator settle time on 0x07/0x08).
 * Source: eprom.cpp INV-05 header block; configure_eprom does not wire eprom_check_vpp
 * into the CMD_READ configure path. */
void test_inv05_eprom_vpp_skip_on_read(void) {
    /* Test all three EPROM protocols for CMD_READ VPP-skip. */
    const uint32_t protos[3] = {0x07, 0x08, 0x0B};
    for (int p = 0; p < 3; p++) {
        clear_bus_recording();
        firestarter_handle_t h = make_handle(protos[p], CMD_READ);
        configure_memory(&h);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "INV-05: configure_memory must not error on CMD_READ");
        for (int i = 0; i < bus_recording_count(); i++) {
            if (recorded_reg(i) == CONTROL_REGISTER) {
                TEST_ASSERT_BITS_LOW_MESSAGE(
                    (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                    recorded_data(i),
                    "INV-05: EPROM CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
                TEST_ASSERT_BITS_LOW_MESSAGE(
                    (uint8_t)CTRL_VPP_P1_ENABLE,
                    recorded_data(i),
                    "INV-05: EPROM CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
            }
        }
    }
    /* Repeat for CMD_BLANK_CHECK — VPP also suppressed on blank-check. */
    for (int p = 0; p < 3; p++) {
        clear_bus_recording();
        firestarter_handle_t h = make_handle(protos[p], CMD_BLANK_CHECK);
        configure_memory(&h);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "INV-05: configure_memory must not error on CMD_BLANK_CHECK");
        for (int i = 0; i < bus_recording_count(); i++) {
            if (recorded_reg(i) == CONTROL_REGISTER) {
                TEST_ASSERT_BITS_LOW_MESSAGE(
                    (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                    recorded_data(i),
                    "INV-05: EPROM CMD_BLANK_CHECK configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
            }
        }
    }
}

/* INV-06 — Pulse-delay defaults: 0x08→100µs, 0x0B→500µs, 0x07→1000µs.
 * SAFE-02 third target: grep -rn INV-06 must hit doc + handler + this test.
 * Asserts: configure_eprom sets handle->pulse_delay per protocol when the caller
 * supplies pulse_delay=0 (the default, meaning "use handler default").
 * Source: eprom.cpp lines 118–124 (the pulse_delay default-setting switch). */
void test_inv06_eprom_pulse_delay_defaults(void) {
    /* 0x08 EPROM_QUICK: 100 µs (Quick-Pulse algorithm). */
    {
        firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
        h.pulse_delay = 0; /* ensure handler default is applied */
        configure_memory(&h);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, h.pulse_delay,
            "INV-06: 0x08 EPROM_QUICK must set pulse_delay=100µs");
    }
    /* 0x0B EPROM_LEGACY: 500 µs (older NMOS parts need longer initial pulse). */
    {
        firestarter_handle_t h = make_handle(0x0B, CMD_WRITE);
        h.pulse_delay = 0;
        configure_memory(&h);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(500, h.pulse_delay,
            "INV-06: 0x0B EPROM_LEGACY must set pulse_delay=500µs");
    }
    /* 0x07 EPROM_STD: 1000 µs (classic 1ms JEDEC algorithm). */
    {
        firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
        h.pulse_delay = 0;
        configure_memory(&h);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, h.pulse_delay,
            "INV-06: 0x07 EPROM_STD must set pulse_delay=1000µs");
    }
    /* Non-zero pulse_delay must NOT be overridden by the handler. */
    {
        firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
        h.pulse_delay = 250; /* DB-supplied value — must not be clobbered */
        configure_memory(&h);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(250, h.pulse_delay,
            "INV-06: non-zero pulse_delay from DB must not be overridden by handler default");
    }
}

/* INV-08 — 0x07 dispatch arm preserved (DISPATCH-ONLY scope).
 * SAFE-02 third target: grep -rn INV-08 must hit doc + handler + this test.
 * SCOPE CAVEAT: WARNING-5 (the build_db.py Rule-2 reclassification of 28C-series
 * EE-EPROMs from 0x07 to 0x0D) is HOST-SIDE Python logic — it is NOT reachable
 * from firmware and CANNOT be regression-tested here. This firmware test pins
 * ONLY the downstream firmware consequence: that protocol 0x07 still dispatches
 * to configure_eprom (NOT configure_eeprom28c). It is, in effect, the same
 * dispatch assertion as the 0x07 path test — see PROTOCOLS.md §3 INV-08 row,
 * which is scoped to "dispatch-only" for the same reason. The host-side
 * WARNING-5 retirement itself is owned by Phase 86's build_db.py / diff_db gates.
 * Asserts: protocol 0x07 dispatches to configure_eprom (NOT configure_eeprom28c).
 * Source: eprom.cpp INV-08 header block; PROTOCOLS.md §3 INV-08 row. */
void test_inv08_eprom_warning5_decode_preserved(void) {
    /* Post-Phase-86: 0x07 dispatches to configure_eprom → eprom_write_execute.
     * Verify configure_memory with 0x07 CMD_WRITE wires firestarter_operation_main
     * (non-NULL) and response_code is OK — confirming the 0x07 dispatch route is
     * correct. NOTE (dispatch-only scope): this checks the firmware 0x07 arm only;
     * it does NOT and cannot detect a host-side WARNING-5 re-activation (that lives
     * in build_db.py and is gated by diff_db.py, not by firmware tests). */
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-08: configure_memory must not error on 0x07 CMD_WRITE (post-Phase-86 dispatch)");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "INV-08: 0x07 CMD_WRITE must wire firestarter_operation_main (routes to configure_eprom, not 0x0D path)");
    /* Also verify 0x07 CMD_READ routes OK — the 0x0D eeprom28c path uses a different init. */
    firestarter_handle_t hr = make_handle(0x07, CMD_READ);
    configure_memory(&hr);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, hr.response_code,
        "INV-08: configure_memory must not error on 0x07 CMD_READ (post-Phase-86 dispatch)");
}

/* ─── Phase 88 Plan 01: Golden register traces (PRIM-01 / SAFE-02 / D-01..D-04) ── */

/* Scripted-byte mock for chip-id path (Pitfall 3 — configure_memory overwrites
 * firestarter_get_data; re-assign this pointer AFTER configure_memory). */
static uint8_t s_chipid_mock_bytes[4];
static int     s_chipid_mock_idx;

static uint8_t mock_chipid_get_data(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    if (s_chipid_mock_idx < (int)sizeof(s_chipid_mock_bytes))
        return s_chipid_mock_bytes[s_chipid_mock_idx++];
    return 0xFF;
}

/*
 * test_golden_eprom_0x07_write — byte-exact golden trace for protocol 0x07 write.
 *
 * Input: 1-byte write (minimal representative — D-04; exercises VPP init + one
 * program pulse + one verify cycle).  FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE
 * from make_handle() ensure blank-check and erase branches are suppressed so
 * the trace covers init+execute only.
 *
 * Trace captures: init (eprom_check_vpp VPP-enable/measure/disable) + execute
 * (REGULATOR_ENABLE|CTRL_VPP_VPE_DROP_ENABLE set, program_mismatched_bytes
 * CTRL_VPE_ENABLE pulse, verify → success).
 *
 * Note: clear_bus_recording() is called AFTER configure_memory() to isolate
 * the init+execute trace from the configure-phase address writes.
 */
void test_golden_eprom_0x07_write(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    h.data_size = 1;  /* minimal representative input (D-04) */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden 0x07 write: configure_memory must not error");
    clear_bus_recording();  /* isolate init+execute from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eprom_0x07_write, golden_eprom_0x07_write_n,
                    "golden trace drift: eprom 0x07 write");
#endif
}

/*
 * test_golden_eprom_0x08_write — byte-exact golden trace for protocol 0x08 write.
 * Same structure as 0x07 but EPROM_QUICK path: CTRL_VPP_VPE_DROP_ENABLE + 100µs
 * pulse instead of 1000µs.
 */
void test_golden_eprom_0x08_write(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
    h.data_size = 1;
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden 0x08 write: configure_memory must not error");
    clear_bus_recording();
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eprom_0x08_write, golden_eprom_0x08_write_n,
                    "golden trace drift: eprom 0x08 write");
#endif
}

/*
 * test_golden_eprom_0x0B_write — byte-exact golden trace for protocol 0x0B write.
 * EPROM_LEGACY path: direct VPE rail (CTRL_VPP_REGULATOR_ENABLE only, no
 * CTRL_VPP_VPE_DROP_ENABLE), 500µs pulse.
 */
void test_golden_eprom_0x0B_write(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_WRITE);
    h.data_size = 1;
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden 0x0B write: configure_memory must not error");
    clear_bus_recording();
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eprom_0x0B_write, golden_eprom_0x0B_write_n,
                    "golden trace drift: eprom 0x0B write");
#endif
}

/*
 * test_golden_eprom_chip_id — byte-exact golden trace for the eprom chip-id (P4) path.
 *
 * Uses CMD_CHECK_CHIP_ID with a non-zero chip_id (0x1F00) to enable the compare
 * branch in eprom_internal_check_chip_id.  The scripted-byte mock returns 0x1F then
 * 0x00 so the chip_id matches and no error is set.
 *
 * Pitfall 3: configure_memory() overwrites firestarter_get_data; we re-assign the
 * mock AFTER configure_memory() and before driving operation_init/main.
 *
 * Trace captures: eprom_check_chip_id_init (eprom_check_vpp) + chip-id execute
 * (CTRL_VPP_REGULATOR_ENABLE + CTRL_VPP_A9_ENABLE set for read, then disabled).
 */
void test_golden_eprom_chip_id(void) {
    s_chipid_mock_idx = 0;
    s_chipid_mock_bytes[0] = 0x1F;  /* manufacturer byte */
    s_chipid_mock_bytes[1] = 0x00;  /* device byte — combined = 0x1F00 */
    memset(s_chipid_mock_bytes + 2, 0xFF, 2);

    firestarter_handle_t h = make_handle(0x07, CMD_CHECK_CHIP_ID);
    h.chip_id = 0x1F00;  /* non-zero: enables compare branch (D-03 P4 path) */
    configure_memory(&h);
    /* Re-assign after configure_memory() overwrites the pointer (Pitfall 3). */
    h.firestarter_get_data = mock_chipid_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden chip-id: configure_memory must not error");
    clear_bus_recording();
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eprom_chip_id, golden_eprom_chip_id_n,
                    "golden trace drift: eprom chip-id (P4)");
#endif
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* POSITIVE: write + init path enables VPP regulator, one test per protocol */
    RUN_TEST(test_eprom_0x07_write_enables_vpp_regulator);
    RUN_TEST(test_eprom_0x08_write_enables_vpp_regulator);
    RUN_TEST(test_eprom_0x0B_write_enables_vpp_regulator);

    /* NEGATIVE CONTROL: configure-only (CMD_READ, no init) — no VPP bits set */
    RUN_TEST(test_eprom_0x07_read_configure_only_does_not_enable_vpp);
    RUN_TEST(test_eprom_0x08_read_configure_only_does_not_enable_vpp);
    RUN_TEST(test_eprom_0x0B_read_configure_only_does_not_enable_vpp);

    /* Phase 87 Plan 03: INV gap-fill assertions (SAFE-02 / NAME-03) */
    RUN_TEST(test_inv01_eprom_0x0B_direct_vpe_rail);
    RUN_TEST(test_inv02_eprom_0x0B_oe_vpp_read_skip);
    RUN_TEST(test_inv03_eprom_0x08_p1_as_vpp);
    RUN_TEST(test_inv05_eprom_vpp_skip_on_read);
    RUN_TEST(test_inv06_eprom_pulse_delay_defaults);
    RUN_TEST(test_inv08_eprom_warning5_decode_preserved);

    /* Phase 88 Plan 01: byte-exact golden register traces (PRIM-01 / SAFE-02) */
    RUN_TEST(test_golden_eprom_0x07_write);
    RUN_TEST(test_golden_eprom_0x08_write);
    RUN_TEST(test_golden_eprom_0x0B_write);
    RUN_TEST(test_golden_eprom_chip_id);

    return UNITY_END();
}
