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

/* ─── Phase 89 CR-01 / WR-02: chip-id mismatch-fork regression guard ─────── */

/* Scripted-byte mock for mismatch tests — returns a fixed byte sequence. */
static uint8_t s_mismatch_mock_bytes[4];
static int     s_mismatch_mock_idx;

static uint8_t mock_mismatch_get_data(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    if (s_mismatch_mock_idx < (int)sizeof(s_mismatch_mock_bytes))
        return s_mismatch_mock_bytes[s_mismatch_mock_idx++];
    return 0xFF;
}

/* WR-02a — Direct unit test of chip_id_report(force_warning=false):
 * mismatch ALWAYS yields ERROR regardless of FLAG_FORCE being set.
 *
 * CR-01 regression guard: this is the minimal direct test that would have
 * caught the Phase-89-02 regression (where FLAG_FORCE was read inside
 * chip_id_report instead of being passed by the caller). */
void test_wr02a_chip_id_report_false_keying_always_errors(void) {
    /* Script get_data to return 0xDE, 0xAD → read_id=0xDEAD; handle.chip_id=0x1F00 → mismatch. */
    s_mismatch_mock_idx = 0;
    s_mismatch_mock_bytes[0] = 0xDE;
    s_mismatch_mock_bytes[1] = 0xAD;
    memset(s_mismatch_mock_bytes + 2, 0xFF, 2);

    /* Case 1: FLAG_FORCE NOT set — expect ERROR (baseline). */
    {
        firestarter_handle_t h = make_handle(0x07, CMD_CHECK_CHIP_ID);
        h.chip_id = 0x1F00;
        /* ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE, no FLAG_FORCE */
        configure_memory(&h);
        h.firestarter_get_data = mock_mismatch_get_data;
        s_mismatch_mock_idx = 0;
        if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
        if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "WR-02a: CHECK_CHIP_ID mismatch without FLAG_FORCE must yield ERROR");
    }
    /* Case 2: FLAG_FORCE IS set — eprom_check_chip_id_execute must STILL yield ERROR (CR-01). */
    {
        firestarter_handle_t h = make_handle(0x07, CMD_CHECK_CHIP_ID);
        h.chip_id = 0x1F00;
        h.ctrl_flags |= FLAG_FORCE;  /* set FLAG_FORCE — must NOT downgrade to WARNING */
        configure_memory(&h);
        h.firestarter_get_data = mock_mismatch_get_data;
        s_mismatch_mock_idx = 0;
        if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
        if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "WR-02a CR-01: CHECK_CHIP_ID mismatch WITH FLAG_FORCE must STILL yield ERROR "
            "(eprom_check_chip_id_execute always passes force_warning=false)");
    }
}

/* WR-02b — chip_id_report(force_warning=true) yields WARNING on mismatch.
 *
 * Exercises the generic-init path (eprom_generic_init → eprom_internal_check_chip_id
 * with error_code=RESPONSE_CODE_WARNING when FLAG_FORCE is set).  This is the
 * FORCE→WARNING path that must remain intact for normal write operations. */
void test_wr02b_chip_id_report_true_keying_yields_warning(void) {
    /* Script get_data to return 0xDE, 0xAD → mismatch against chip_id=0x1F00. */
    s_mismatch_mock_idx = 0;
    s_mismatch_mock_bytes[0] = 0xDE;
    s_mismatch_mock_bytes[1] = 0xAD;
    memset(s_mismatch_mock_bytes + 2, 0xFF, 2);

    /* CMD_WRITE + FLAG_FORCE: eprom_generic_init passes RESPONSE_CODE_WARNING to
     * eprom_internal_check_chip_id → chip_id_report(force_warning=true) → WARNING. */
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    h.chip_id = 0x1F00;
    h.ctrl_flags |= FLAG_FORCE;
    configure_memory(&h);
    h.firestarter_get_data = mock_mismatch_get_data;
    s_mismatch_mock_idx = 0;
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "WR-02b: generic-init mismatch WITH FLAG_FORCE must yield WARNING "
        "(eprom_generic_init passes RESPONSE_CODE_WARNING → force_warning=true)");
}

/* WR-02c — generic-init path WITHOUT FLAG_FORCE yields ERROR on mismatch.
 *
 * Symmetry test: eprom_generic_init with FLAG_FORCE CLEAR passes
 * RESPONSE_CODE_ERROR → chip_id_report(force_warning=false) → ERROR.
 * This confirms the generic-init caller-keying is also correct. */
void test_wr02c_generic_init_mismatch_without_force_yields_error(void) {
    s_mismatch_mock_idx = 0;
    s_mismatch_mock_bytes[0] = 0xDE;
    s_mismatch_mock_bytes[1] = 0xAD;
    memset(s_mismatch_mock_bytes + 2, 0xFF, 2);

    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    h.chip_id = 0x1F00;
    /* ctrl_flags: FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE, no FLAG_FORCE */
    configure_memory(&h);
    h.firestarter_get_data = mock_mismatch_get_data;
    s_mismatch_mock_idx = 0;
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "WR-02c: generic-init mismatch WITHOUT FLAG_FORCE must yield ERROR");
}

/* ─── Phase 98 Plan 02 / RC-1 fix: corrected 0x08 32-pin ≤256K PGM-assert ── */

/* Helper: count CONTROL_REGISTER writes in the current recording. */
static int count_control_reg_writes(void) {
    int n = 0;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            n++;
        }
    }
    return n;
}

/* Helper: detect two consecutive CONTROL_REGISTER writes in the recording.
 * This is the implementation-artifact discriminator for the deliberate PGM-hold
 * write: memory_set_data first does mem_util_set_address (emits CONTROL write),
 * then the gated PGM-hold adds a second CONTROL write immediately after —
 * producing CONTROL, CONTROL in sequence. Pre-fix, these writes are never
 * adjacent (they are separated by LSB/MSB register writes of the next op). */
static bool recording_has_consecutive_control_writes(void) {
    int n = bus_recording_count();
    for (int i = 0; i + 1 < n; i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            recorded_reg(i + 1) == CONTROL_REGISTER) {
            return true;
        }
    }
    return false;
}

/*
 * RC-98A — Corrected 0x08 32-pin ≤256K path: deliberate PGM-hold CONTROL write
 * (CODE-STRUCTURE assertion — HIGH-3 honesty).
 *
 * Verifies that memory_set_data, when gated on protocol==0x08 && pins==32 &&
 * mem_size<=262144, emits an ADDITIONAL, deliberate CONTROL_REGISTER write
 * that explicitly holds pin 31's bus line (line 22 / CTRL_ADDRESS_LINE_18) at
 * the program-active LOW level across the CE-pulse window.
 *
 * The RED-able discriminator is the PRESENCE/COUNT of this extra CONTROL write
 * (an implementation artifact the fix UNIQUELY emits) — NOT the mere LOW level
 * of CTRL_ADDRESS_LINE_18 at addr=0, which is ALREADY zero before the fix
 * (pin 31 is off the address bus in DIP32_27C020, and addr=0 sets no high-order
 * bits). Asserting on the bit level alone would be circular (true pre-fix too).
 *
 * Pre-fix CONTROL write count in execute phase (1 byte, addr=0, DIP32_27C020,
 * vpp_line=VPP_P1_32_DIP → using_p1_as_vpp=true → CTRL_VPE→CTRL_VPP_P1 swap):
 *   VPP regulator + DROP_ENABLE set                       — 1 CONTROL write
 *   set_ctrl(CTRL_VPP_P1_ENABLE, 1) via VPE→P1 swap      — 1 CONTROL write
 *   mem_util_set_address (write byte): LSB, MSB, CONTROL  — 1 CONTROL write
 *   [no deliberate PGM-hold write pre-fix]
 *   set_ctrl(CTRL_VPP_P1_ENABLE, 0) via VPE→P1 swap      — 1 CONTROL write
 *   mem_util_set_address (verify byte): LSB, MSB, CONTROL — 1 CONTROL write
 *   Total pre-fix: 5 CONTROL writes (empirically confirmed)
 *
 * Post-fix: the gated PGM-hold write adds 1 more → 6 CONTROL writes minimum.
 * This test FAILS pre-fix (RED) because the extra write is absent.
 *
 * GREEN: A green Test A verifies CODE STRUCTURE (deliberate PGM assert is
 * emitted). It does NOT imply bits flip on silicon — under RC-1 the addr-0
 * register state is byte-unchanged (pin 31 already at VIL at addr 0). Phase 99
 * is the sole empirical gate.
 *
 * bus_config: DIP32_27C020 shape (pin 31 OFF the address bus, no static_high_mask
 * for line 22, vpp on pin 1 via CTRL_VPP_P1_ENABLE — matching Plan 01's pinout).
 * mem_size = 262144 (256K, ≤262144 gate — includes AM27C020).
 */
void test_rc98a_0x08_32pin_256k_deliberate_pgm_hold_emitted(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);

    /* DIP32_27C020 bus_config: pin 31 NOT in address bus, vpp on pin 1 via
     * CTRL_VPP_P1_ENABLE. All address_lines[] entries = 0xFF (no remapped lines
     * beyond the default address_mask), address_mask covers A0-A17 (18 bits =
     * 0x3FFFF), matching_lines = 0. static_high_mask = 0 (no static-high entry
     * in DIP32_27C020). rw_line = 0xFF (no RW pin). vpp_line = VPP_P1_32_DIP
     * (pin 1 → P1 VPP routing — matches DIP32_27C020 vpp-pin:[1] field).
     * Pitfall 4: re-assign any mock AFTER configure_memory (it clobbers
     * firestarter_get_data at memory.cpp:91). */
    h.pins = 32;
    h.mem_size = 262144;                          /* 256K — within the ≤262144 gate */
    h.data_size = 1;
    h.bus_config.vpp_line   = VPP_P1_32_DIP;     /* pin 1 VPP routing */
    h.bus_config.address_mask = 0x3FFFF;          /* A0-A17 only; A18 (0x40000) excluded */
    h.bus_config.rw_line    = 0xFF;               /* no RW pin */
    h.bus_config.static_high_mask = 0;            /* no static-high lines */
    h.bus_config.matching_lines   = 0;
    /* Fill address_lines[0..19] = 0xFF (no remapped lines) */
    for (int i = 0; i < 20; i++) h.bus_config.address_lines[i] = 0xFF;
    /* data_buffer[0] = 0 (zero-init); rurp_read_data_buffer() stub returns 0 →
     * verify_and_update_mask finds no mismatch → program loop exits after 1 pass. */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98A: configure_memory must not error on 0x08 CMD_WRITE");
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);

    /* Isolate execute phase: clear recording after init so we count only the
     * execute-phase CONTROL writes (same pattern as test_inv03_eprom_0x08_p1_as_vpp). */
    clear_bus_recording();

    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98A: eprom_write_execute must not error on 1-byte zero write");

    /* Assert the PRESENCE of the deliberate PGM-hold CONTROL write.
     * Pre-fix count = 5 (empirically confirmed — see comment above).
     * Post-fix = 6+ (one extra deliberate PGM-hold write in memory_set_data
     * before rurp_chip_enable, uniquely emitted by the gated branch).
     * This is the implementation-artifact discriminator (HIGH-3): the fix
     * UNIQUELY emits this extra write; the bit-level LOW-ness of line 22 at
     * addr=0 is already true pre-fix and would make the test circular. */
    int ctrl_writes = count_control_reg_writes();
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(6, ctrl_writes,
        "RC-98A [CODE-STRUCTURE]: 0x08 32-pin ≤256K execute phase must emit ≥6 "
        "CONTROL_REGISTER writes (5 pre-fix + 1 deliberate PGM-hold write in "
        "memory_set_data before chip_enable). "
        "A green test verifies code structure only — does NOT imply bits flip on silicon. "
        "Under RC-1, addr-0 register state is byte-unchanged (pin 31 already VIL at addr 0); "
        "Phase 99 is the sole empirical gate.");
}

/*
 * RC-98B — D-04 gate exclusion: PGM-hold MUST NOT fire for 512K (A18 user).
 *
 * Verifies that the gated PGM-hold branch (`protocol==0x08 && pins==32 &&
 * mem_size<=262144`) is UNREACHABLE for a 512K AM27C040 (mem_size=524288).
 * On Rev 2, CTRL_VPP_P1_ENABLE (0x08) == CTRL_ADDRESS_LINE_18_REV2 — firing
 * the PGM-hold on a 512K part would corrupt A18 (hardware damage, D-04).
 *
 * Asserts: the execute-phase CONTROL write count equals the pre-fix baseline
 * (4 writes) — the extra deliberate PGM-hold write is ABSENT for mem_size=524288.
 */
void test_rc98b_0x08_32pin_512k_pgm_hold_excluded(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);

    /* Same bus_config shape as RC-98A but mem_size = 524288 (512K, A18 user).
     * The gate predicate (mem_size <= 262144) MUST exclude this chip. */
    h.pins = 32;
    h.mem_size = 524288;                          /* 512K — OUTSIDE the ≤262144 gate */
    h.data_size = 1;
    h.bus_config.vpp_line   = VPP_P1_32_DIP;
    h.bus_config.address_mask = 0x7FFFF;          /* A0-A18 for 512K */
    h.bus_config.rw_line    = 0xFF;
    h.bus_config.static_high_mask = 0;
    h.bus_config.matching_lines   = 0;
    for (int i = 0; i < 20; i++) h.bus_config.address_lines[i] = 0xFF;

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98B: configure_memory must not error on 0x08 512K CMD_WRITE");
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    clear_bus_recording();
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    /* Gate exclusion: the extra deliberate PGM-hold write is ABSENT.
     * Execute-phase CONTROL writes must equal the pre-fix baseline (5):
     *   VPP regulator set, CTRL_VPP_P1 set (via VPE→P1 swap), set_address write,
     *   CTRL_VPP_P1 clear, set_address verify.
     * If the gate fires incorrectly (missing mem_size term), an extra write would
     * appear (≥6), which would also corrupt CTRL_ADDRESS_LINE_18_REV2 on real hardware. */
    int ctrl_writes = count_control_reg_writes();
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(5, ctrl_writes,
        "RC-98B [D-04 gate exclusion]: 0x08 32-pin 512K execute phase must NOT emit "
        "the deliberate PGM-hold write (only ≤5 CONTROL writes, same as pre-fix baseline). "
        "The size gate (mem_size<=262144) is the D-04 firmware belt: CTRL_VPP_P1_ENABLE_REV2 "
        "== CTRL_ADDRESS_LINE_18_REV2 == 0x08; firing it on a 512K part corrupts A18.");
}

/*
 * RC-98C — Mismatch failure-case (P89 CR-01 mandatory test):
 * 0x08 32-pin ≤256K write where verify NEVER matches.
 *
 * Verifies two things:
 * (a) The corrected path STILL drives the PGM-hold (deliberate CONTROL write
 *     present — same CODE-STRUCTURE assertion as RC-98A).
 * (b) The write correctly ERRORs after the retry budget is exhausted — proving
 *     the test catches the correct-vs-incorrect fork (not just happy-path).
 *
 * Scripted mismatch: mock get_data always returns 0xFF; data_buffer[0] = 0x00
 * → every verify fails → program_mismatched_bytes is called NUMBER_OF_RETRIES
 * times → each round emits ≥1 PGM-hold write → large CONTROL write count.
 *
 * Model: WR-02a (test_val_eprom.cpp:615-657). Pitfall 4: re-assign
 * firestarter_get_data AFTER configure_memory.
 */

/* Mismatch mock for RC-98C: always returns 0xFF (never matches 0x00). */
static uint8_t s_rc98c_mock_idx;
static uint8_t mock_rc98c_always_mismatch(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    (void)s_rc98c_mock_idx++;
    return 0xFF;  /* always mismatches data_buffer[0]=0x00 */
}

void test_rc98c_0x08_32pin_256k_mismatch_errors_and_pgm_asserted(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
    h.pins = 32;
    h.mem_size = 262144;
    h.data_size = 1;
    h.bus_config.vpp_line   = VPP_P1_32_DIP;
    h.bus_config.address_mask = 0x3FFFF;
    h.bus_config.rw_line    = 0xFF;
    h.bus_config.static_high_mask = 0;
    h.bus_config.matching_lines   = 0;
    for (int i = 0; i < 20; i++) h.bus_config.address_lines[i] = 0xFF;
    /* data_buffer[0] remains 0x00 (zero-init from make_handle) */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98C: configure_memory must not error");

    /* Pitfall 4: re-assign get_data AFTER configure_memory. */
    h.firestarter_get_data = mock_rc98c_always_mismatch;
    s_rc98c_mock_idx = 0;

    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    clear_bus_recording();

    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    /* (a) Mismatch failure-case: must ERROR after exhausting retry budget.
     * This is the primary assertion — proves correct-vs-incorrect fork vs happy-path. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98C: 0x08 32-pin ≤256K write with permanent mismatch must yield ERROR "
        "(retry budget exhausted — proves correct-vs-incorrect fork, not just happy-path)");

    /* (b) CODE-STRUCTURE: the corrected path STILL drove PGM-hold across the
     * CE pulses (NUMBER_OF_RETRIES=20 rounds × ≥1 deliberate CONTROL write each).
     * Counting: with mock_rc98c_always_mismatch replacing firestarter_get_data,
     * memory_get_data is NOT called (verify uses the mock directly) → no set_addr
     * for verify. Per retry: CTRL_VPP_P1 set (1) + set_addr write (1) + CTRL_VPP_P1
     * clear (1) + [PGM-hold if fix] = 3 pre-fix, 4 post-fix.
     * Plus: VPP-regulator set (1) at start, VPP-regulator clear (1) at end.
     * Pre-fix total: 1 + 20×3 + 1 = 62.
     * Post-fix total: 1 + 20×4 + 1 = 82.
     * Assert ≥63 (pre-fix=62; post-fix=82): RED pre-fix (PGM-hold absent). */
    int ctrl_writes = count_control_reg_writes();
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(63, ctrl_writes,
        "RC-98C: mismatch path must emit ≥63 CONTROL writes (1+20×4+1=82 post-fix; "
        "pre-fix=62 — the deliberate PGM-hold write per CE-pulse is absent pre-fix, "
        "making this RED before Task 2; mismatch+ERROR proves correct-vs-incorrect fork)");
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

    /* Phase 89 CR-01 / WR-02: chip-id mismatch-fork regression guard */
    RUN_TEST(test_wr02a_chip_id_report_false_keying_always_errors);
    RUN_TEST(test_wr02b_chip_id_report_true_keying_yields_warning);
    RUN_TEST(test_wr02c_generic_init_mismatch_without_force_yields_error);

    /* Phase 98 Plan 02 / RC-1 fix: 0x08 32-pin ≤256K PGM-assert coverage */
    RUN_TEST(test_rc98a_0x08_32pin_256k_deliberate_pgm_hold_emitted);
    RUN_TEST(test_rc98b_0x08_32pin_512k_pgm_hold_excluded);
    RUN_TEST(test_rc98c_0x08_32pin_256k_mismatch_errors_and_pgm_asserted);

    return UNITY_END();
}
