/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the EEPROM 28C family.
 * HARN-01 / D-07 / T-71-WIRED-WRONG.
 *
 * Proves configure_eeprom28c is a 5V-only handler (no VPP regulator use).
 * BY SIDE-EFFECT via the recording bus stub:
 *
 *   For CMD_READ and CMD_WRITE (configure-only phase): configure_memory() is
 *   called. configure_eeprom28c sets function pointers and the pulse_delay but
 *   writes NO VPP-enable CTL bits. mem_util_set_address(handle, 0) writes LSB/
 *   MSB/CONTROL registers with address bits only — CTRL_VPP_REGULATOR_ENABLE,
 *   CTRL_VPP_P1_ENABLE, and CTRL_VPP_VPE_DROP_ENABLE must NEVER appear set in
 *   any recorded CONTROL_REGISTER write.
 *
 *   This test can go RED if configure_eeprom28c is accidentally wired to an
 *   EPROM-style configure path that enables the VPP regulator (T-71-WIRED-WRONG).
 *
 * Protocol covered: 0x0D (EEPROM_POLL / AT28C-series).
 *
 * VPP: NONE — configure_eeprom28c is a 5V page-write handler. The A9-12V chip-ID
 * path in eeprom28c_check_chip_id is gated by handle->chip_id > 0; we set chip_id=0
 * so that branch is never reached. Even if reached, it fires in the operation_init
 * phase (not the configure phase tested here).
 *
 * Phase 88 Plan 02 extensions — PRIM-01 / SAFE-02:
 *   Golden register traces (D-01) for eeprom28c 0x0D write path and chip-id (P7+P5+P4).
 *   test_golden_eeprom28c_write: pins SDP unlock + DQ7 poll + one-byte execute trace.
 *   test_golden_eeprom28c_chip_id: pins A9-12V chip-id check + SDP unlock init trace.
 *   Both reuse the suite's setUp() and the shared assert_trace_eq() helper (88-01).
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

/* Golden expected arrays for eeprom28c paths.
 * Each .inc file is a comma-list of { reg, data } rows produced by GOLDEN_BLESS mode.
 * Header comment in each .inc names the producing input and documents the low-byte caveat. */
static const golden_entry_t golden_eeprom28c_write[] = {
#include "golden_eeprom28c_write.inc"
};
static const int golden_eeprom28c_write_n =
    (int)(sizeof(golden_eeprom28c_write) / sizeof(golden_eeprom28c_write[0]));

static const golden_entry_t golden_eeprom28c_chip_id[] = {
#include "golden_eeprom28c_chip_id.inc"
};
static const int golden_eeprom28c_chip_id_n =
    (int)(sizeof(golden_eeprom28c_chip_id) / sizeof(golden_eeprom28c_chip_id[0]));

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by eeprom28c_check_chip_id (delay(50) + delay(100)) for
     * A9-12V regulator settle. delayMicroseconds() is called by eeprom28c_wait_for_write
     * (10µs poll loop). Both must be stubbed before any path that hits them. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x0D;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch */
    h.mem_size   = 32768; /* 32 KB (AT28C256) */
    return h;
}

/* ─── Helper: assert no VPP-enable bits in any CONTROL_REGISTER write ──────── */
/* Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION is defined —
 * it does not fit in the uint8_t recording buffer. Check only the 8-bit-fit
 * VPP-enable bits: CTRL_VPP_REGULATOR_ENABLE (0x80) and CTRL_VPP_P1_ENABLE (0x08). */
static void assert_no_vpp_in_recording(const char* ctx) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i), ctx);
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i), ctx);
        }
    }
}

/* configure-only: CMD_READ must record zero VPP-enable bits */
void test_eeprom28c_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x0D CMD_READ");
    assert_no_vpp_in_recording(
        "configure_eeprom28c CMD_READ must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_WRITE must record zero VPP-enable bits */
void test_eeprom28c_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x0D CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_eeprom28c CMD_WRITE must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_BLANK_CHECK must record zero VPP-enable bits */
void test_eeprom28c_blank_check_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_BLANK_CHECK);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x0D CMD_BLANK_CHECK");
    assert_no_vpp_in_recording(
        "configure_eeprom28c CMD_BLANK_CHECK must NOT set any VPP-enable CTL bit");
}

/* ─── Phase 88 Plan 02: Golden register traces (PRIM-01 / D-01..D-04) ─────── */

/*
 * Scripted-byte mock for the chip-id (P4) and write-wait (P5) poll reads.
 * configure_memory() overwrites firestarter_get_data; re-assign AFTER configure_memory
 * (Pitfall 3). setUp() resets s_mock_byte_idx to 0 and s_mock_bytes to 0xFF.
 *
 * For the write golden trace (chip_id=0):
 *   - eeprom28c_wait_for_write(0x5555, 0x20): first read must return 0x20 to succeed.
 *   - eeprom28c_wait_for_write(0, 0x00): write-execute polls address 0; data_buffer[0]=0x00,
 *     first read returns 0x00 → poll succeeds.
 *   Bytes: [0]=0x20 (SDP wait), [1]=0x00 (write-execute poll)
 *
 * For the chip-id golden trace (chip_id=0x1F08):
 *   - eeprom28c_check_chip_id reads mfr_addr=0x7FC0 → 0x1F, mfr_addr+1=0x7FC1 → 0x08
 *     → chip_id=0x1F08 matches handle.chip_id → no error.
 *   - eeprom28c_wait_for_write(0x5555, 0x20): first read must return 0x20 to succeed.
 *   Bytes: [0]=0x1F, [1]=0x08, [2]=0x20 (SDP wait)
 */
static uint8_t s_mock_bytes[16];
static int     s_mock_byte_idx;

static uint8_t mock_get_data_scripted(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    if (s_mock_byte_idx < (int)sizeof(s_mock_bytes))
        return s_mock_bytes[s_mock_byte_idx++];
    return 0xFF;
}

/*
 * test_golden_eeprom28c_write — byte-exact golden trace for eeprom28c 0x0D write.
 *
 * Input: chip_id=0 (skip chip-id branch), FLAG_SKIP_BLANK_CHECK, data_size=1 byte
 * (minimal representative — D-04). Exercises:
 *   init: SDP unlock (P7, 6-write sequence via flash_util_byte_flipping) +
 *         DQ7 wait (P5, eeprom28c_wait_for_write polls until 0x20 match).
 *   execute: one firestarter_set_data write (address 0, data 0x00) + DQ7 wait.
 *
 * clear_bus_recording() AFTER configure_memory() isolates the init+execute phase
 * from the configure-phase address writes (address 0 setup). (D-04 discipline.)
 *
 * Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION defined —
 * invisible in the uint8_t recording buffer (Pitfall 1). INV assertions above
 * remain as the complementary guard for VPP bits.
 */
void test_golden_eeprom28c_write(void) {
    s_mock_byte_idx = 0;
    memset(s_mock_bytes, 0xFF, sizeof(s_mock_bytes));
    s_mock_bytes[0] = 0x20;  /* satisfies eeprom28c_wait_for_write(0x5555, 0x20) on first poll */
    s_mock_bytes[1] = 0x00;  /* satisfies eeprom28c_wait_for_write(0, 0x00) on first poll */

    firestarter_handle_t h = make_handle(CMD_WRITE);
    h.data_size  = 1;                              /* minimal representative input (D-04) */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden eeprom28c write: configure_memory must not error");
    /* Re-assign AFTER configure_memory() overwrites the function pointer (Pitfall 3). */
    h.firestarter_get_data = mock_get_data_scripted;
    clear_bus_recording();  /* isolate init+execute from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden eeprom28c write: operation must not error");
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eeprom28c_write, golden_eeprom28c_write_n,
                    "golden trace drift: eeprom28c 0x0D write");
#endif
}

/*
 * test_golden_eeprom28c_chip_id — byte-exact golden trace for eeprom28c A9-12V
 * chip-id check (P4/P7 path) via eeprom28c_write_init with chip_id > 0.
 *
 * eeprom28c does not handle CMD_CHECK_CHIP_ID; the chip-id path runs as part of
 * eeprom28c_write_init when handle->chip_id > 0. We use CMD_WRITE + chip_id=0x1F08
 * and call operation_init only (not main) to isolate the chip-id + SDP unlock trace.
 *
 * Scripted bytes (re-assigned AFTER configure_memory — Pitfall 3):
 *   [0]=0x1F → mfr byte at mfr_addr=0x7FC0; [1]=0x08 → device byte at 0x7FC1
 *   → chip_id=0x1F08 matches handle.chip_id → no error set.
 *   [2]=0x20 → satisfies eeprom28c_wait_for_write(0x5555, 0x20) after SDP disable.
 *
 * Trace captures: chip-id VPP enable (CTRL_VPP_REGULATOR_ENABLE + CTRL_VPP_A9_ENABLE)
 * + SDP unlock sequence (P7) + SDP wait (P5).
 */
void test_golden_eeprom28c_chip_id(void) {
    s_mock_byte_idx = 0;
    memset(s_mock_bytes, 0xFF, sizeof(s_mock_bytes));
    s_mock_bytes[0] = 0x1F;  /* mfr byte → chip_id high byte */
    s_mock_bytes[1] = 0x08;  /* device byte → chip_id low byte; 0x1F08 matches */
    s_mock_bytes[2] = 0x20;  /* satisfies eeprom28c_wait_for_write(0x5555, 0x20) */

    firestarter_handle_t h = make_handle(CMD_WRITE);
    h.chip_id    = 0x1F08;  /* non-zero: enables chip-id check in write_init (D-03 P4) */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden eeprom28c chip_id: configure_memory must not error");
    /* Re-assign AFTER configure_memory() overwrites the function pointer (Pitfall 3). */
    h.firestarter_get_data = mock_get_data_scripted;
    clear_bus_recording();  /* isolate init trace from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden eeprom28c chip_id: operation_init must not error");
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_eeprom28c_chip_id, golden_eeprom28c_chip_id_n,
                    "golden trace drift: eeprom28c chip-id (P4/P7)");
#endif
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* 5V-only proof: no VPP-enable CTL bit for any command in the configure phase */
    RUN_TEST(test_eeprom28c_read_configure_no_vpp);
    RUN_TEST(test_eeprom28c_write_configure_no_vpp);
    RUN_TEST(test_eeprom28c_blank_check_configure_no_vpp);

    /* Phase 88 Plan 02: golden register traces (PRIM-01 / SAFE-02) */
    RUN_TEST(test_golden_eeprom28c_write);
    RUN_TEST(test_golden_eeprom28c_chip_id);

    return UNITY_END();
}
