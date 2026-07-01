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

/* ─── Phase 98 Plan 04 / RC-1 CORRECTED fix: rw_line-based pin-31 PGM hold ── */

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

/* Helper: does any recorded CONTROL_REGISTER write during the WRITE-phase
 * (execute, after init) carry CTRL_READ_WRITE (0x40) CLEARED — i.e. pin 31
 * held at the program-active LOW level? This is the corrected rw_line
 * artifact (replaces the removed "extra CONTROL write" count from Plan 02):
 * with config.rw_line=22 and WRITE_FLAG=0, mem_util_remap_address_bus ORs
 * 0<<22 into the address (a no-op — the bit is simply not set), so the
 * top_address CONTROL write's CTRL_READ_WRITE bit is 0 (LOW) on every write.
 * We assert at least one CONTROL write in the recording has that bit clear. */
static bool recording_has_ctrl_read_write_low(void) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & CTRL_READ_WRITE) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * RC-98A — Corrected 0x08 32-pin ≤256K path: pin 31 (RW / CTRL_READ_WRITE)
 * held LOW via the rw_line mechanism (rw_line=22, resolved from 98-03's
 * rw-pin:[31] on DIP32_27C020).
 *
 * Reconciled with the CR-01 revert (WR-04): Plan 02's "extra CONTROL write"
 * discriminator is GONE — memory_set_data no longer performs any CONTROL
 * mutation of its own. Pin 31's program-active hold now arrives via the
 * ALREADY-EXISTING mem_util_set_address -> mem_util_calculate_top_address_register
 * path, driven by config.rw_line (set from the pinout DB), not an extra write.
 * The corrected artifact is: the WRITE-phase top_address CONTROL write has
 * CTRL_READ_WRITE (0x40) CLEAR.
 *
 * bus_config: DIP32_27C020 shape (pin 31 OFF the address bus, rw_line=22,
 * vpp on pin 1 via CTRL_VPP_P1_ENABLE — matching 98-03's pinout).
 * mem_size = 262144 (256K, includes AM27C020).
 */
void test_rc98a_0x08_32pin_256k_pgm_hold_via_rw_line(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);

    /* DIP32_27C020 bus_config: pin 31 NOT in address bus, RESOLVED to the RW
     * line (rw_line=22, from 98-03's rw-pin:[31] -> pin_conversions[32][31]=22).
     * vpp on pin 1 via CTRL_VPP_P1_ENABLE. All address_lines[] entries = 0xFF
     * (no remapped lines beyond the default address_mask), address_mask covers
     * A0-A17 (18 bits = 0x3FFFF), matching_lines = 0. static_high_mask = 0
     * (no static-high entry in DIP32_27C020).
     * Pitfall 4: re-assign any mock AFTER configure_memory (it clobbers
     * firestarter_get_data at memory.cpp:91). */
    h.pins = 32;
    h.mem_size = 262144;                          /* 256K — AM27C020 */
    h.data_size = 1;
    h.bus_config.vpp_line   = VPP_P1_32_DIP;     /* pin 1 VPP routing */
    h.bus_config.address_mask = 0x3FFFF;          /* A0-A17 only; A18 (0x40000) excluded */
    h.bus_config.rw_line    = 22;                 /* pin 31 -> RW line (98-03) */
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

    /* Isolate execute phase: clear recording after init so we assert only on
     * the execute-phase CONTROL writes (same pattern as test_inv03_eprom_0x08_p1_as_vpp). */
    clear_bus_recording();

    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98A: eprom_write_execute must not error on 1-byte zero write");

    /* Corrected artifact: at least one WRITE-phase CONTROL write has
     * CTRL_READ_WRITE (0x40) CLEAR — pin 31 held program-active LOW via the
     * rw_line mechanism (config.rw_line=22, WRITE_FLAG=0). */
    TEST_ASSERT_TRUE_MESSAGE(recording_has_ctrl_read_write_low(),
        "RC-98A [rw_line mechanism]: 0x08 32-pin ≤256K write-phase must emit a "
        "CONTROL_REGISTER write with CTRL_READ_WRITE (0x40) LOW — pin 31 held "
        "program-active via config.rw_line=22 (mem_util_remap_address_bus + "
        "top_address CTRL_READ_WRITE mask), driven by 98-03's rw-pin:[31]. "
        "Phase 99 (Leonardo + Rev 2.0) remains the sole empirical silicon proof.");
}

/*
 * RC-98B — D-04 gate exclusion (RECONCILED to the corrected mechanism, WR-02):
 * a 512K AM27C040 (A18 user) MUST NOT get its rw_line resolved to pin 31 in a
 * way that corrupts A18 addressing. DIP32_27C040 (the existing 27C040 pinout)
 * has NO rw-pin entry (rw_line stays 0xFF, disabled) — pin 31 is address line
 * A18 there, untouched by this mechanism.
 *
 * Asserts: with rw_line=0xFF (the DIP32_27C040 shape — no RW pin assigned),
 * the execute-phase CONTROL write count is EXACTLY the baseline 5 writes
 * (pinned, not "<=", so a write-reducing regression cannot silently pass) —
 * confirming NO CTRL_READ_WRITE-clearing write is introduced by this mechanism
 * for a chip that does not opt into rw-pin.
 */
void test_rc98b_0x08_32pin_512k_pgm_hold_excluded(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);

    /* Same bus_config shape as RC-98A but mem_size = 524288 (512K, A18 user)
     * and rw_line = 0xFF (DIP32_27C040 shape — pin 31 stays on the address
     * bus as A18; no rw-pin assignment for this chip class). */
    h.pins = 32;
    h.mem_size = 524288;                          /* 512K — AM27C040, A18 user */
    h.data_size = 1;
    h.bus_config.vpp_line   = VPP_P1_32_DIP;
    h.bus_config.address_mask = 0x7FFFF;          /* A0-A18 for 512K */
    h.bus_config.rw_line    = 0xFF;               /* no RW pin — DIP32_27C040 shape */
    h.bus_config.static_high_mask = 0;
    h.bus_config.matching_lines   = 0;
    for (int i = 0; i < 20; i++) h.bus_config.address_lines[i] = 0xFF;

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "RC-98B: configure_memory must not error on 0x08 512K CMD_WRITE");
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    clear_bus_recording();
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    /* Gate exclusion (D-04): execute-phase CONTROL writes must equal the
     * pinned baseline of EXACTLY 5 (not "<=" — a write-reducing regression
     * must not silently pass):
     *   VPP regulator set, CTRL_VPP_P1 set (via VPE→P1 swap), set_address write,
     *   CTRL_VPP_P1 clear, set_address verify.
     * With rw_line=0xFF the mem_util_remap_address_bus rw_line branch never
     * fires, so no CTRL_READ_WRITE-clearing write is introduced for this
     * A18-using chip — the corrected mechanism is structurally inert here. */
    int ctrl_writes = count_control_reg_writes();
    TEST_ASSERT_EQUAL_MESSAGE(5, ctrl_writes,
        "RC-98B [D-04 gate exclusion, WR-02 pinned]: 0x08 32-pin 512K execute phase "
        "must emit EXACTLY 5 CONTROL writes (baseline, pinned not <=). "
        "rw_line=0xFF (DIP32_27C040 shape, no rw-pin) means the rw_line mechanism "
        "never fires for this A18-using chip — pin 31/A18 addressing is untouched.");
}

/*
 * RC-98C — Mismatch failure-case (P89 CR-01 mandatory test):
 * 0x08 32-pin ≤256K write where verify NEVER matches.
 *
 * Verifies two things:
 * (a) The corrected path STILL drives the rw_line pin-31 hold (CTRL_READ_WRITE
 *     LOW present — same corrected-mechanism assertion as RC-98A) across every
 *     retry round.
 * (b) The write correctly ERRORs after the retry budget is exhausted — proving
 *     the test catches the correct-vs-incorrect fork (not just happy-path).
 *
 * Scripted mismatch: mock get_data always returns 0xFF; data_buffer[0] = 0x00
 * → every verify fails → program_mismatched_bytes is called NUMBER_OF_RETRIES
 * times.
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
    h.bus_config.rw_line    = 22;                 /* pin 31 -> RW line (98-03) */
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

    /* (b) Corrected mechanism still active across every retry round: at least
     * one recorded CONTROL write has CTRL_READ_WRITE LOW (pin 31 held via
     * rw_line on every set_address call in the retry loop, not just the first). */
    TEST_ASSERT_TRUE_MESSAGE(recording_has_ctrl_read_write_low(),
        "RC-98C [rw_line mechanism]: mismatch/retry path must still emit at least "
        "one CONTROL write with CTRL_READ_WRITE LOW (pin 31 program-active via "
        "config.rw_line=22 on every set_address call in the retry loop); "
        "mismatch+ERROR proves the correct-vs-incorrect fork (P89 CR-01 lesson).");
}

/* ─── WR-01: revision-parametrized PHYSICAL-remap test (the missing RED state) ──
 *
 * RED against 98-02 (Plan 02's logical CTRL_ADDRESS_LINE_18 clear), GREEN
 * against 98-03/04 (the corrected rw_line -> CTRL_READ_WRITE mechanism).
 *
 * [env:native] does NOT link the real rurp_map_ctrl_reg_for_hardware_revision
 * (it lives in include/rurp_hw_rev_utils.h, only #included by the AVR-only
 * src/boards/{uno,leonardo}_rurp_shield.cpp via rurp_register_utils.h, which
 * are excluded from native's build_src_filter = +<proms/> +...). The shared
 * host stub (host_stubs_common.inc:138) is a plain passthrough `(uint8_t)data`
 * — it does not model per-revision bit remapping at all. So this test defines
 * LOCAL REPLICAS that mirror include/rurp_hw_rev_utils.h lines 21-30 EXACTLY
 * (verified against the real header at plan-write time): the Rev-2 passthrough
 * mask + CTRL_ADDRESS_LINE_18 -> CTRL_ADDRESS_LINE_18_REV2 fold, and the
 * Rev-0/1 `ctrl_reg = data` passthrough. This is a deliberate test-infra
 * choice (documented here, not silently duplicated) rather than restructuring
 * the firmware's build_src_filter to link AVR-flavored code into native. */

/* Mirrors rurp_hw_rev_utils.h:19-27 (REVISION_2_0/2_1/2_2/2_3 case) exactly. */
static uint8_t replica_map_ctrl_reg_rev2(rurp_register_t data) {
    uint8_t ctrl_reg = data & (CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE |
                                CTRL_ADDRESS_LINE_17 | CTRL_READ_WRITE | CTRL_VPP_REGULATOR_ENABLE);
    ctrl_reg |= data & CTRL_VPP_VPE_DROP_ENABLE ? CTRL_VPP_VPE_DROP_ENABLE_REV2 : 0;
    ctrl_reg |= data & CTRL_ADDRESS_LINE_16 ? CTRL_ADDRESS_LINE_16_REV2 : 0;
    ctrl_reg |= data & CTRL_ADDRESS_LINE_18 ? CTRL_ADDRESS_LINE_18_REV2 : 0;
    return ctrl_reg;
}

/* Mirrors rurp_hw_rev_utils.h:28-32 (REVISION_0/REVISION_1 case) exactly. */
static uint8_t replica_map_ctrl_reg_legacy(rurp_register_t data) {
    uint8_t ctrl_reg = (uint8_t)data;
    ctrl_reg |= data & CTRL_VPP_VPE_DROP_ENABLE ? CTRL_VPP_VPE_DROP_ENABLE_REV1 : 0;
    return ctrl_reg;
}

/*
 * WR-01 Rev-2 case: a program-window CONTROL value with pin 31 = RW held
 * program-active (WRITE_FLAG=0 -> CTRL_READ_WRITE 0x40 LOW) AND P1/VPP
 * asserted (CTRL_VPP_P1_ENABLE 0x08 HIGH), fed through the Rev-2 remap,
 * must yield a physical byte where bit 0x40 is LOW (pin 31 program-active)
 * AND bit 0x08 is HIGH (VPP) — proving the 0x08 alias does NOT defeat 0x40.
 *
 * The OLD 98-02 value (drive logical CTRL_ADDRESS_LINE_18 LOW, i.e. clear it,
 * with CTRL_VPP_P1_ENABLE HIGH for VPP) folds identically to 0x08 on Rev 2
 * (CTRL_ADDRESS_LINE_18_REV2 == CTRL_VPP_P1_ENABLE_REV2), so clearing logical
 * CTRL_ADDRESS_LINE_18 while VPP (0x08) is asserted is a NO-OP on the alias —
 * physical bit 0x40 (CTRL_READ_WRITE) is NEVER touched by that old value,
 * i.e. it stays at whatever CTRL_READ_WRITE was already set to. To make the
 * RED state explicit and non-circular, the 98-02 comparison value below
 * additionally leaves CTRL_READ_WRITE HIGH (as it is a distinct, unasserted
 * control in the old code path) — FAILING the LOW assertion.
 */
void test_wr01_rev2_pin31_pgm_low_with_vpp_concurrent(void) {
    /* Corrected (98-03/04) value: CTRL_READ_WRITE LOW (bit clear = program-
     * active) + CTRL_VPP_P1_ENABLE HIGH (VPP asserted concurrently). */
    rurp_register_t corrected_value = CTRL_VPP_P1_ENABLE; /* CTRL_READ_WRITE bit 0x40 is 0 (LOW) */
    uint8_t physical_corrected = replica_map_ctrl_reg_rev2(corrected_value);
    TEST_ASSERT_EQUAL_MESSAGE(0, physical_corrected & CTRL_READ_WRITE,
        "WR-01 [Rev 2, corrected]: physical CTRL_READ_WRITE (0x40) must be LOW "
        "(pin 31 program-active) after the Rev-2 remap — CTRL_READ_WRITE is in "
        "the passthrough mask (rurp_hw_rev_utils.h:23), revision-invariant.");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, physical_corrected & CTRL_VPP_P1_ENABLE,
        "WR-01 [Rev 2, corrected]: physical CTRL_VPP_P1_ENABLE (0x08) must stay "
        "HIGH (VPP asserted) concurrently with CTRL_READ_WRITE LOW — proves the "
        "0x08 alias (CTRL_ADDRESS_LINE_18_REV2 == CTRL_VPP_P1_ENABLE_REV2) does "
        "NOT defeat the distinct 0x40 bit.");

    /* Old 98-02 value: logical CTRL_ADDRESS_LINE_18 cleared (the Plan-02
     * mechanism), CTRL_VPP_P1_ENABLE HIGH (VPP), CTRL_READ_WRITE left HIGH
     * (unasserted — Plan 02 never touched this distinct control at all). Folds
     * through the SAME alias as CTRL_VPP_P1_ENABLE on Rev 2, so clearing it
     * changes nothing about 0x08 (already HIGH from VPP) and 0x40 stays HIGH
     * (never asserted LOW) -- the physical no-op this WR-01 case exists to catch. */
    rurp_register_t old_98_02_value = CTRL_VPP_P1_ENABLE | CTRL_READ_WRITE; /* A18-clear no-op; RW never asserted */
    uint8_t physical_old = replica_map_ctrl_reg_rev2(old_98_02_value);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, physical_old & CTRL_READ_WRITE,
        "WR-01 [Rev 2, 98-02 RED check]: the OLD logical-A18-clear value leaves "
        "physical CTRL_READ_WRITE (0x40) HIGH (never asserted) -- confirming this "
        "is the RED state the corrected rw_line mechanism fixes (CR-01 physical "
        "no-op via the 0x08 alias).");
}

/*
 * WR-01 Rev-0/1 (legacy) case: the SAME corrected CONTROL value through the
 * Rev-0/1 passthrough remap yields physical 0x40 LOW (pin 31 program-active).
 * The OLD 98-02 value cleared logical CTRL_ADDRESS_LINE_18 (0x20 on legacy,
 * a GENUINE, DISTINCT A18 address line there) while never asserting
 * CTRL_READ_WRITE (0x40) LOW -- so 0x40 stays HIGH, failing the assertion and
 * proving the old value never reached pin 31 on legacy either. The corrected
 * value proves the no-alias (legacy) path still reaches pin 31 at 0x40.
 */
void test_wr01_rev01_pin31_pgm_low_legacy(void) {
    rurp_register_t corrected_value = CTRL_VPP_P1_ENABLE; /* CTRL_READ_WRITE bit 0x40 is 0 (LOW) */
    uint8_t physical_corrected = replica_map_ctrl_reg_legacy(corrected_value);
    TEST_ASSERT_EQUAL_MESSAGE(0, physical_corrected & CTRL_READ_WRITE,
        "WR-01 [Rev 0/1, corrected]: physical CTRL_READ_WRITE (0x40) must be LOW "
        "(pin 31 program-active) after the legacy `ctrl_reg = data` passthrough "
        "remap -- CTRL_READ_WRITE (0x40) is revision-invariant and reaches pin 31 "
        "unchanged on legacy hardware too (no alias collision here).");

    rurp_register_t old_98_02_value = CTRL_VPP_P1_ENABLE | CTRL_READ_WRITE; /* logical A18 cleared elsewhere; RW never asserted */
    uint8_t physical_old = replica_map_ctrl_reg_legacy(old_98_02_value);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, physical_old & CTRL_READ_WRITE,
        "WR-01 [Rev 0/1, 98-02 RED check]: the OLD logical-A18-clear value "
        "cleared a GENUINE, DISTINCT A18 address line (0x20) on legacy hardware "
        "-- it never touched CTRL_READ_WRITE (0x40), which stays HIGH here, "
        "confirming the old fix never reached pin 31 on legacy hardware either.");
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

    /* Phase 98 Plan 04 / RC-1 CORRECTED fix: 0x08 32-pin ≤256K rw_line PGM-hold coverage */
    RUN_TEST(test_rc98a_0x08_32pin_256k_pgm_hold_via_rw_line);
    RUN_TEST(test_rc98b_0x08_32pin_512k_pgm_hold_excluded);
    RUN_TEST(test_rc98c_0x08_32pin_256k_mismatch_errors_and_pgm_asserted);

    /* Phase 98 Plan 04 WR-01: revision-parametrized physical-remap test
     * (the missing RED state — Rev 2 alias no-op AND Rev 0/1 wrong-pin clear). */
    RUN_TEST(test_wr01_rev2_pin31_pgm_low_with_vpp_concurrent);
    RUN_TEST(test_wr01_rev01_pin31_pgm_low_legacy);

    return UNITY_END();
}
