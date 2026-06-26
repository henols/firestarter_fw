/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the Intel Flash family.
 * HARN-01 / D-07 / D-08 (verify-can-fail posture).
 *
 * Proves configure_flash_intel behavior BY SIDE-EFFECT via the recording bus stub:
 *
 *   POSITIVE test (CMD_WRITE + init): configure_memory() + firestarter_operation_init()
 *     → flash_intel_write_init → sets CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE
 *     via firestarter_set_control_register → memory_set_control_register
 *     → rurp_write_to_register(CONTROL_REGISTER, value | CTRL_VPP_P1_ENABLE)
 *     The recording must contain at least one CONTROL_REGISTER write with
 *     CTRL_VPP_P1_ENABLE set.
 *
 *   NEGATIVE CONTROL (CMD_READ, configure-only phase): configure_memory() alone.
 *     configure_flash_intel sets firestarter_operation_init = NULL for CMD_READ,
 *     and mem_util_set_address writes only address bits to the CONTROL_REGISTER —
 *     no VPP-enable bits. Asserts CTRL_VPP_P1_ENABLE NEVER appears in the recording
 *     (D-08 verify-can-fail: goes RED if a regression puts VPP inside configure_memory).
 *
 * Protocol covered: 0x10 (FLASH_INTEL).
 *
 * VPP mechanism (from flash_intel.cpp source — not guessed):
 *   flash_intel_write_init line 107: CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE
 *
 * NOTE: delay() must be mocked; flash_intel_write_init calls delay(500).
 * VPP voltage stub returns 12000 mV (h.vpp_mv = 12000 setpoint matches → no error).
 * Hardware revision stub returns 1 (non-REVISION_0) so VPP path is reachable.
 *
 * Phase 88 Plan 02 extensions — PRIM-01 / SAFE-02:
 *   Golden register traces (D-01) for flash_intel 0x10 write path and chip-id (P3+P4).
 *   test_golden_flash_intel_write: pins VPP-gate + one-byte program sequence trace.
 *   test_golden_flash_intel_chip_id: pins CMD_CHECK_CHIP_ID autoselect trace.
 *   Both reuse the suite's existing setUp() and custom-voltage stub from host_stubs.cpp.
 *   Production flash_intel.cpp is NOT modified (D-08).
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

/* Golden-trace helper (assert_trace_eq + GOLDEN_BLESS print mode).
 * Included AFTER the extern "C" recording decls above (required include order). */
#include "../_shared/golden_trace.h"

/* Golden expected arrays for flash_intel paths.
 * Each .inc file is a comma-list of { reg, data } rows produced by GOLDEN_BLESS mode.
 * Header comment in each .inc names the producing input and documents the low-byte caveat. */
static const golden_entry_t golden_flash_intel_write[] = {
#include "golden_flash_intel_write.inc"
};
static const int golden_flash_intel_write_n =
    (int)(sizeof(golden_flash_intel_write) / sizeof(golden_flash_intel_write[0]));

static const golden_entry_t golden_flash_intel_chip_id[] = {
#include "golden_flash_intel_chip_id.inc"
};
static const int golden_flash_intel_chip_id_n =
    (int)(sizeof(golden_flash_intel_chip_id) / sizeof(golden_flash_intel_chip_id[0]));

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by flash_intel_write_init (delay(500) for regulator settle). */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    /* delayMicroseconds() is called by memory_set_data (3µs settle + pulse_delay).
     * Needed when operation_main (flash_intel_write_execute) is called. */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    /* millis() is called by flash_intel_poll_sr (SR poll timeout loop). Return 0
     * so deadline = 0 + timeout_ms; loop runs until get_data returns 0x80 (SR ready). */
    When(Method(ArduinoFake(Function), millis)).AlwaysReturn(0);
    clear_bus_recording();
}

void tearDown(void) {}

/* Build a handle pre-wired for the Intel-flash write path.
 * chip_id=0 skips the chip-id branch. FLAG_SKIP_BLANK_CHECK + FLAG_SKIP_ERASE
 * prevent blank-check and erase from running against the mock. */
static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x10;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv     = 12000; /* matches mock voltage 12V → no VPP warn/error */
    h.chip_id    = 0;     /* skip chip-id branch */
    h.mem_size   = 131072; /* 128 KB */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

/* ─── POSITIVE test: CMD_WRITE + init → CTRL_VPP_P1_ENABLE must fire ──────── */

void test_flash_intel_write_enables_vpp_p1(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    bool p1_seen = false;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & CTRL_VPP_P1_ENABLE)) {
            p1_seen = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(p1_seen,
        "configure_flash_intel write must record CTRL_VPP_P1_ENABLE in CTL register");
}

/* Also assert CTRL_VPP_REGULATOR_ENABLE is set (flash_intel_write_init sets both). */
void test_flash_intel_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    bool reg_seen = false;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & CTRL_VPP_REGULATOR_ENABLE)) {
            reg_seen = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reg_seen,
        "configure_flash_intel write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* ─── NEGATIVE CONTROL: CMD_READ, configure-only — VPP must NOT fire ─────── */

/* For CMD_READ, configure_flash_intel leaves firestarter_operation_init = NULL
 * (configure_memory sets it to NULL initially; configure_flash_intel does not
 * override it for CMD_READ). Only configure_memory is called. The address setup
 * writes to CONTROL_REGISTER carry only address bits — no VPP-enable bits.
 * This asserts the configure/dispatch phase alone never enables VPP (D-08). */
void test_flash_intel_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_READ");
    /* No init call — configure-phase only */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "configure_flash_intel CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_flash_intel CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

/* ─── Phase 88 Plan 02: Golden register traces (PRIM-01 / D-01..D-04) ─────── */

/*
 * Scripted-byte mock for flash_intel chip-id and SR-poll reads.
 * configure_memory() overwrites firestarter_get_data; re-assign AFTER configure_memory
 * (Pitfall 3). setUp() resets the mock state via ArduinoFakeReset.
 *
 * For the write golden trace:
 *   SR poll reads address 0 until bit7 set. Return 0x80 on first call → exits poll.
 *   (bit4=0: no VPP error; bit3=0: no program error → return true)
 *
 * For the chip-id golden trace (CMD_CHECK_CHIP_ID):
 *   flash_intel_check_chip_id reads 0x0000 and 0x0001.
 *   Return 0x89 for address 0 (mfr=0x89), 0xB4 for address 1 (dev=0xB4).
 *   → chip_id = 0x89B4 matches handle.chip_id → no error set.
 */
static uint8_t s_mock_bytes[8];
static int     s_mock_byte_idx;

static uint8_t mock_get_data_scripted(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    if (s_mock_byte_idx < (int)sizeof(s_mock_bytes))
        return s_mock_bytes[s_mock_byte_idx++];
    return 0xFF;
}

/*
 * test_golden_flash_intel_write — byte-exact golden trace for flash_intel 0x10 write.
 *
 * Input: chip_id=0 (skip chip-id in init), FLAG_SKIP_BLANK_CHECK|FLAG_SKIP_ERASE,
 * data_size=1 byte (minimal representative — D-04). Exercises:
 *   init: VPP-gate (P3) — CTRL_VPP_REGULATOR_ENABLE|CTRL_VPP_P1_ENABLE enable.
 *   execute: command-register write (0x40 setup + data byte) + SR poll (P5-analog).
 *
 * millis() mocked to 0 in setUp(); deadline = 0 + 150 = 150; loop runs until
 * get_data returns 0x80 (re-assigned AFTER configure_memory — Pitfall 3).
 *
 * clear_bus_recording() AFTER configure_memory() isolates the init+execute trace
 * from the configure-phase address writes. (D-04 discipline.)
 *
 * Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION defined —
 * invisible in the uint8_t recording buffer (Pitfall 1). Existing INV assertions
 * remain as complementary guard for VPP bits.
 *
 * Note: flash_intel_cleanup (firestarter_operation_end) is NOT called here.
 * The golden write trace pins only the init+execute phase (P3 VPP-gate + program).
 */
void test_golden_flash_intel_write(void) {
    s_mock_byte_idx = 0;
    memset(s_mock_bytes, 0xFF, sizeof(s_mock_bytes));
    s_mock_bytes[0] = 0x80;  /* SR ready: bit7 set, no error bits → poll exits */

    firestarter_handle_t h = make_handle(CMD_WRITE);
    h.data_size  = 1;  /* minimal representative input (D-04) */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash_intel write: configure_memory must not error");
    /* Re-assign AFTER configure_memory() overwrites the function pointer (Pitfall 3). */
    h.firestarter_get_data = mock_get_data_scripted;
    clear_bus_recording();  /* isolate init+execute from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash_intel write: operation must not error");
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_flash_intel_write, golden_flash_intel_write_n,
                    "golden trace drift: flash_intel 0x10 write (VPP-gate P3)");
#endif
}

/*
 * test_golden_flash_intel_chip_id — byte-exact golden trace for flash_intel chip-id
 * (P4) path via CMD_CHECK_CHIP_ID → flash_intel_check_chip_id.
 *
 * configure_flash_intel handles CMD_CHECK_CHIP_ID: sets operation_init=NULL,
 * operation_end=NULL, operation_main=flash_intel_check_chip_id.
 * flash_intel_check_chip_id: writes 0x90 (autoselect), reads 0x0000 + 0x0001,
 * writes 0xFF (reset). No VPP involved (P4 is a command-sequence path, not VPP-gated).
 *
 * Scripted bytes (re-assigned AFTER configure_memory — Pitfall 3):
 *   [0]=0x89 → mfr byte at 0x0000; [1]=0xB4 → device byte at 0x0001
 *   → chip_id=0x89B4 matches handle.chip_id → no error set.
 *
 * Trace captures: 3 firestarter_set_data calls (0x90 cmd, data write, 0xFF reset),
 * each writing 3 register entries via mem_util_set_address (LSB, MSB, CTL).
 */
void test_golden_flash_intel_chip_id(void) {
    s_mock_byte_idx = 0;
    memset(s_mock_bytes, 0xFF, sizeof(s_mock_bytes));
    s_mock_bytes[0] = 0x89;  /* mfr byte at 0x0000 */
    s_mock_bytes[1] = 0xB4;  /* device byte at 0x0001; chip_id=0x89B4 matches */

    firestarter_handle_t h = make_handle(CMD_CHECK_CHIP_ID);
    h.chip_id = 0x89B4;  /* non-zero: enables compare branch in check_chip_id (D-03 P4) */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash_intel chip_id: configure_memory must not error");
    /* Re-assign AFTER configure_memory() overwrites the function pointer (Pitfall 3). */
    h.firestarter_get_data = mock_get_data_scripted;
    clear_bus_recording();  /* isolate execute trace from configure-phase address writes */
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash_intel chip_id: operation_main must not error");
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_flash_intel_chip_id, golden_flash_intel_chip_id_n,
                    "golden trace drift: flash_intel chip-id (P4)");
#endif
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* POSITIVE: write + init path enables VPP_P1 and VPP_REGULATOR */
    RUN_TEST(test_flash_intel_write_enables_vpp_p1);
    RUN_TEST(test_flash_intel_write_enables_vpp_regulator);

    /* NEGATIVE CONTROL: configure-only (CMD_READ, no init) — no VPP bits set */
    RUN_TEST(test_flash_intel_read_configure_only_does_not_enable_vpp);

    /* Phase 88 Plan 02: golden register traces (PRIM-01 / SAFE-02) */
    RUN_TEST(test_golden_flash_intel_write);
    RUN_TEST(test_golden_flash_intel_chip_id);

    return UNITY_END();
}
