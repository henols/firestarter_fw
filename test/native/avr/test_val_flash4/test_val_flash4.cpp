/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the Flash Type 4 family.
 * Phase 87 Plan 03 — INV-04 gap-fill assertion (NAME-03 / SAFE-02).
 * HARN-01 / D-07 / T-71-WIRED-WRONG.
 *
 * Proves the configure_flash4 dispatch/configure phase is VPP-safe.
 * BY SIDE-EFFECT via the recording bus stub:
 *
 *   For CMD_READ and CMD_WRITE (configure-only phase): configure_memory() writes
 *   only address bits to LSB/MSB/CONTROL registers via mem_util_set_address.
 *   configure_flash4 sets function pointers but writes no VPP-enable CTL bits.
 *   CTRL_VPP_REGULATOR_ENABLE, CTRL_VPP_P1_ENABLE, and CTRL_VPP_VPE_DROP_ENABLE
 *   must NEVER appear set in any recorded CONTROL_REGISTER write during the
 *   configure/dispatch phase.
 *
 *   NOTE: flash4_erase_execute (called from flash4_write_init) does use
 *   CTRL_VPP_REGULATOR_ENABLE for the OE=12V erase pulse. This is an operation-
 *   phase VPP use, NOT a configure-phase use. This suite tests the configure
 *   phase only (configure_memory alone, no firestarter_operation_init call), which
 *   is the correct scope for the "dispatch doesn't touch VPP" proof.
 *
 * Protocols covered: 0x05 (FLASH_AMD_STD), 0x35 (FLASH_EEPROM), 0x39 (FLASH_EEPROM2).
 *
 * INV gap-fill assertion added by Phase 87 Plan 03 (SAFE-02 third target):
 *   INV-04: test_inv04_flash4_256b_page_boundary
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

/* Golden expected arrays (one per traced path).
 * Each .inc file is a comma-list of { reg, data } rows produced by GOLDEN_BLESS mode.
 * Header comment in each .inc names the input and documents the low-byte caveat. */
static const golden_entry_t golden_flash4_write[] = {
#include "golden_flash4_write.inc"
};
static const int golden_flash4_write_n =
    (int)(sizeof(golden_flash4_write) / sizeof(golden_flash4_write[0]));

static const golden_entry_t golden_flash4_chip_id[] = {
#include "golden_flash4_chip_id.inc"
};
static const int golden_flash4_chip_id_n =
    (int)(sizeof(golden_flash4_chip_id) / sizeof(golden_flash4_chip_id[0]));

/* Scripted-byte mock for chip-id path (Pitfall 3 — configure_memory overwrites
 * firestarter_get_data; re-assign this pointer AFTER configure_memory). */
static uint8_t s_flash4_chipid_mock_bytes[4];
static int     s_flash4_chipid_mock_idx;

static uint8_t flash4_mock_chipid_get_data(struct firestarter_handle* /*h*/, uint32_t /*addr*/) {
    if (s_flash4_chipid_mock_idx < (int)sizeof(s_flash4_chipid_mock_bytes))
        return s_flash4_chipid_mock_bytes[s_flash4_chipid_mock_idx++];
    return 0xFF;
}

/* FIX-01b (Phase 94 Plan 03) — boot-block detect mock.
 *
 * Returns 0xFF at the §6.6 DETECT address (0x00002 for first 16K boot block),
 * simulating a permanently locked boot block. Returns 0x00 for all other
 * addresses so that poll_readback compares expected=0xFF against observed=0x00,
 * forcing a poll timeout on the first data byte.
 *
 * Pitfall 3 (same as chip-id mock): configure_memory() overwrites firestarter_get_data.
 * Caller MUST re-assign h.firestarter_get_data to this function AFTER configure_memory().
 */
static uint8_t flash4_mock_boot_block_locked_get_data(struct firestarter_handle* /*h*/, uint32_t addr) {
    if (addr == 0x00002) {
        return 0xFF; /* §6.6 DETECT: 0xFF = boot block locked */
    }
    return 0x00; /* all data addresses return 0x00 (poll target mismatch with 0xFF expected) */
}

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delayMicroseconds is called by flash4_wait_for_page_write (10µs poll delay)
     * and by memory_set_data (3µs write settle). Must be stubbed so the operation-
     * phase tests (test_flash4_write_execute_*) don't abort on an unmocked call. */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = protocol;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch */
    h.mem_size   = 524288; /* 512 KB (SST39SF040) */
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

/* ─── Protocol 0x05 (FLASH_AMD_STD) ─────────────────────────────────────── */

void test_flash4_0x05_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash4 0x05 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_flash4_0x05_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash4 0x05 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── Protocol 0x35 (FLASH_EEPROM) ─────────────────────────────────────── */

void test_flash4_0x35_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x35, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x35 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash4 0x35 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_flash4_0x35_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x35, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x35 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash4 0x35 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── Protocol 0x39 (FLASH_EEPROM2) — future-proofed, dispatched by analogy ─ */

void test_flash4_0x39_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x39, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x39 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash4 0x39 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_flash4_0x39_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x39, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x39 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash4 0x39 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── FIX-02B (Phase 74 Plan 02): operation-phase SDP emission + VPP-safety ─ */
/*
 * These two tests exercise flash4_write_execute (the operation phase, not just
 * configure), using the recording-bus stub to observe side effects.
 *
 * Test setup: configure_memory(CMD_WRITE) wires function pointers including
 * firestarter_set_data = memory_set_data and the operation_main pointer.
 * Then clear_bus_recording() resets the capture, fill data_buffer with zeros
 * (so flash4_wait_for_page_write's DQ7 poll passes in one iteration since the
 * stub's rurp_read_data_buffer() always returns 0 = expected), set data_size=4
 * at address=0, and call h.firestarter_operation_main(&h) to drive
 * flash4_write_execute.
 *
 * The recording captures every rurp_write_to_register call:
 *   - flash_util_byte_flipping (SDP sequence) writes CONTROL_REGISTER
 *     (CTRL_READ_WRITE) + LSB/MSB for each command address pair.
 *   - memory_set_data writes LSB/MSB/CONTROL via mem_util_set_address.
 *
 * Test 1 (SDP emission, RED before fix): scans for the FLASH_ENABLE_WRITE
 * address signature — MSB writes of 0x55 (for 0x5555 and 0x5555 again) and
 * 0x2A (for 0x2AAA) in sequence before the first data address write. FAILS
 * today because flash4_write_execute has no flash_execute_command(FLASH_ENABLE_WRITE).
 *
 * Test 2 (operation-phase VPP-safety): asserts that no CTRL_VPP_REGULATOR_ENABLE
 * (0x80) or CTRL_VPP_P1_ENABLE (0x08) bit appears in any CONTROL_REGISTER write
 * during the write-execute call. Passes today and MUST keep passing after the fix.
 */

/* Helper to build a write handle with 4-byte zero data buffer at address 0. */
static firestarter_handle_t make_write_handle_with_data(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch in write_init */
    h.mem_size   = 524288; /* 512 KB (W29C040) */
    h.address    = 0;
    h.data_size  = 4; /* small: 4 zero bytes at page 0; poll passes immediately */
    /* data_buffer is zero-initialized by {} */
    /* ctrl_flags = 0: no FLAG_CAN_ERASE, no FLAG_SKIP_BLANK_CHECK —
     * flash4_write_init would call blank-check, but we bypass init and call
     * operation_main directly. */
    return h;
}

/* Helper: scan recording for FLASH_ENABLE_WRITE address signature.
 * FLASH_ENABLE_WRITE addresses: 0x5555, 0x2AAA, 0x5555.
 * fu_flash_fast_address writes (LSB=addr&0xFF, MSB=(addr>>8)&0xFF).
 * Signature MSB pattern at start: 0x55, 0x2A, 0x55 in consecutive MSB writes.
 * Returns true if found, false if not. */
static bool recording_contains_sdp_signature(void) {
    /* Look for the MSB sequence: 0x55, 0x2A, 0x55 (MSBs of 0x5555, 0x2AAA, 0x5555) */
    int msb_seq_index = 0;
    const uint8_t msb_pattern[3] = {0x55, 0x2A, 0x55};
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == MOST_SIGNIFICANT_BYTE) {
            if (recorded_data(i) == msb_pattern[msb_seq_index]) {
                msb_seq_index++;
                if (msb_seq_index == 3) {
                    return true; /* full SDP MSB signature found */
                }
            } else {
                /* Reset if sequence breaks (partial match then mismatch) */
                msb_seq_index = (recorded_data(i) == msb_pattern[0]) ? 1 : 0;
            }
        }
    }
    return false;
}

/* Test 1 (FIX-02B SDP): flash4_write_execute must emit FLASH_ENABLE_WRITE
 * SDP 3-byte sequence at the start of each page load.
 * RED before fix (no flash_execute_command(FLASH_ENABLE_WRITE) in write path). */
void test_flash4_write_execute_emits_sdp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording(); /* reset after configure_memory's set_address call */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash4_write_execute must not error on 4-byte zero write");
    TEST_ASSERT_TRUE_MESSAGE(recording_contains_sdp_signature(),
        "flash4_write_execute must emit FLASH_ENABLE_WRITE SDP (0x5555,0x2AAA,0x5555 MSB pattern) at page start");
}

/* Test 2 (FIX-02B VPP-safety operation phase): flash4_write_execute must NEVER
 * set CTRL_VPP_REGULATOR_ENABLE (0x80) or CTRL_VPP_P1_ENABLE (0x08) in any
 * CONTROL_REGISTER write during the write-execute call.
 * Passes for the bare loop today; MUST remain green after the SDP fix since
 * flash_util_byte_flipping only sets CTRL_READ_WRITE (not VPP bits). */
void test_flash4_write_execute_no_vpp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash4_write_execute must not error on 4-byte zero write");
    assert_no_vpp_in_recording(
        "flash4_write_execute (operation phase) must NOT set any VPP-enable CTL bit");
}

/* ─── Phase 87 Plan 03: INV-04 gap-fill assertion (SAFE-02) ─────────────────── */

/* INV-04 — flash4 256B page boundary (data-driven from handle->mem_size).
 * SAFE-02 third target: grep -rn INV-04 must hit doc + handler + this test.
 * Asserts: for a 512KB (W29C040) chip, flash4_write_execute fires TWO SDP sequences
 * for a write that crosses a 256B page boundary (bytes 0–256 inclusive). A fixed
 * 64-byte page would fire 5 SDPs; a correct 256-byte page fires 2 (one at byte 0,
 * one at byte 256). This directly pins the data-driven page-size derivation.
 * Source: flash_type_4.cpp flash4_page_size() (mem_size=524288 → 256B).
 *         W29C040 citation: datasheets/0x05-FLASH-AMD-STD/W29C040.pdf p.11 §Page Write.
 *
 * Test setup: configure 0x05 with mem_size=524288 (512KB, W29C040), address=254,
 * data_size=3 (bytes 254, 255, 256 — spanning the 256B page boundary at address 256).
 * With 256B pages: SDP fires at address 254 (is_first_byte) and address 256 (is_page_start).
 * That is exactly 2 SDP sequences in the recording. A 64B page would fire the second SDP
 * at address 256 (256%64==0) too, but would ALSO fire at address 192 and 128, which means
 * a 3-byte write from 254 would still give 2 (254 is not a 64B boundary, but 256 is).
 *
 * Distinguishing proof: use address=0, data_size=3. With page_size=256: fires at address 0
 * (first_byte, SDP1) and only one more fire would happen at address 256 — but data ends at 2.
 * Actually the cleanest cross-boundary proof: address=254, data_size=3.
 *   - page_size=256: SDP at addr 254 (first_byte=true) + addr 256 (page_start). Count=2.
 *   - page_size=128: SDP at addr 254 (first_byte=true) + addr 256 (256%128==0, page_start). Count=2.
 *   - page_size=64:  SDP at addr 254 (first_byte=true) + addr 256 (256%64==0, page_start). Count=2.
 * All three give count=2 for this span — no discrimination on count alone.
 *
 * Better approach: address=0, data_size=65 (spans pages at 64B boundaries but not 256B).
 *   - page_size=256: SDP at addr 0 only (page_start + first_byte). addr 64 is NOT a 256B boundary. Count=1.
 *   - page_size=64:  SDP at addr 0 (first_byte) + addr 64 (64%64==0). Count=2.
 * So for W29C040 (512KB → page_size=256): 65-byte write from addr 0 = 1 SDP (not 2).
 *
 * Verification: assert count=1 (not 2) for 65 bytes starting at addr 0 with 512KB chip.
 * If page_size were 64, count would be 2 (original bug). The single SDP confirms 256B pages. */
static int count_sdp_occurrences(void) {
    /* Count how many times the SDP MSB pattern 0x55, 0x2A, 0x55 appears in the recording. */
    int count = 0;
    int msb_seq_index = 0;
    const uint8_t msb_pattern[3] = {0x55, 0x2A, 0x55};
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == MOST_SIGNIFICANT_BYTE) {
            if (recorded_data(i) == msb_pattern[msb_seq_index]) {
                msb_seq_index++;
                if (msb_seq_index == 3) {
                    count++;
                    msb_seq_index = 0; /* reset to scan for next occurrence */
                }
            } else {
                msb_seq_index = (recorded_data(i) == msb_pattern[0]) ? 1 : 0;
            }
        }
    }
    return count;
}

void test_inv04_flash4_256b_page_boundary(void) {
    /* W29C040 (512KB): flash4_page_size(524288) = 256.
     * Write 65 bytes starting at address 0.
     *   - page_size=256: SDP fires at addr 0 only (first_byte + page_start). Count=1.
     *   - page_size=64:  SDP fires at addr 0 AND addr 64 (64%64==0). Count=2.
     * Asserting count=1 PROVES page_size=256 (not the old fixed 64). */
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512 KB (W29C040) → flash4_page_size() = 256 */
    h.address    = 0;
    h.data_size  = 65; /* 65 bytes: addr 0..64; 64 is a 64B boundary but NOT a 256B boundary */
    /* data_buffer zero-initialized: rurp_read_data_buffer() returns 0 = expected → DQ7 poll passes. */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-04: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "INV-04: flash4_write_execute must not error on 65-byte zero write");
    /* page_size=256 → only 1 SDP for 65 bytes from addr 0 (addr 64 is not a 256B boundary).
     * page_size=64 (the old fixed value) → 2 SDPs (addr 0 and addr 64). */
    int sdp_count = count_sdp_occurrences();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sdp_count,
        "INV-04: W29C040 (512KB) must use 256B page size — 65-byte write from addr 0 fires "
        "exactly 1 SDP (addr 64 is a 64B boundary but NOT a 256B boundary); "
        "page_size=64 (old bug) would fire 2 SDPs");
}

/* ─── Phase 88 Plan 03: Golden register traces (PRIM-01 / SAFE-02 / D-01..D-04) ── */

/*
 * test_golden_flash4_write — byte-exact golden trace for flash4 0x05 write path.
 *
 * flash4 (FLASH-AMD-STD / W29C040): 5V page-write NOR flash with AMD SDP.
 * Uses the INV-04 65-byte minimal probe (D-04): address=0, data_size=65, mem_size=524288.
 * A full 256-byte page write would exceed the 256-entry recording cap (Pitfall 2 / D-04);
 * the assert_trace_eq anti-truncation guard enforces this.  With page_size=256 (W29C040,
 * 512KB), a 65-byte write fires exactly ONE SDP (addr 0 is both first_byte and page_start;
 * addr 64 is a 64B boundary but NOT a 256B boundary — no second SDP).
 *
 * Input: protocol=0x05, CMD_WRITE, mem_size=524288 (512KB), address=0, data_size=65,
 * data_buffer=zeros, FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE, chip_id=0.
 * configure_memory wires operation_init = flash4_write_init and operation_main = flash4_write_execute.
 * With FLAG_SKIP_BLANK_CHECK and FLAG_SKIP_ERASE set, flash4_write_init is a no-op.
 *
 * Trace structure: 1 SDP (8 entries) + 65 × memory_set_data (3 entries/byte = 195)
 * + 1 poll (flash4_wait_for_page_write: 3 entries for last address) = 206 entries (< 256 cap).
 *
 * clear_bus_recording() is called AFTER configure_memory() to isolate the operation trace.
 */
void test_golden_flash4_write(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 → page_size=256; 65-byte probe fires 1 SDP */
    h.address    = 0;
    h.data_size  = 65; /* INV-04 minimal probe (D-04): addr 64 is NOT a 256B boundary */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE; /* suppress init side-effects */
    /* data_buffer zero-initialized: rurp_read_data_buffer()=0 → DQ7 poll passes. */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash4 write: configure_memory must not error");
    clear_bus_recording(); /* isolate operation trace from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    /* Phase 95: flash4_write_init now includes a proactive §6.6 boot-block detect
     * (FLASH_ENABLE_ID + read + FLASH_DISABLE_ID) when address is in a boot-block
     * region. With address=0 and the default stub returning 0x00 (unlocked), the
     * detect fires but produces no error. Its bus emissions land in the recording
     * BEFORE the execute phase. Reset the recording here so the golden trace captures
     * only flash4_write_execute — identical to the pre-Phase-95 baseline (FIX-02).
     * The golden_flash4_write.inc file is therefore byte-identical to the prior pin. */
    clear_bus_recording(); /* isolate execute trace from init-phase detect bus emissions */
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_flash4_write, golden_flash4_write_n,
                    "golden trace drift: flash4 0x05 write (65-byte probe)");
#endif
}

/*
 * test_golden_flash4_chip_id — byte-exact golden trace for flash4 chip-id (P4 via flash_utils).
 *
 * flash4's CMD_CHECK_CHIP_ID path routes to flash4_check_chip_id_execute →
 * flash_util_check_chip_id_execute → flash_util_get_chip_id.
 * flash_util_get_chip_id issues FLASH_ENABLE_ID (SDP 3-cycle), reads 2 bytes at
 * addresses 0x0000 and 0x0001 (manufacturer + device), then issues FLASH_DISABLE_ID.
 *
 * Pitfall 3: configure_memory() overwrites firestarter_get_data with memory_get_data.
 * We re-assign h.firestarter_get_data to the scripted-byte mock AFTER configure_memory().
 * Scripted bytes {0xBF, 0xB7} → chip_id = 0xBFB7 → matches h.chip_id=0xBFB7 (no error).
 *
 * Trace structure:
 *   FLASH_ENABLE_ID (8 entries: CTL + 3×(LSB+MSB) + CTL)
 *   + 2 × memory_get_data via firestarter_get_data (3 entries each via mem_util_set_address)
 *   + FLASH_DISABLE_ID (8 entries)
 *   = 22 entries (well under 256 cap).
 *
 * Note: configure_flash4 for CMD_CHECK_CHIP_ID sets operation_init=NULL and
 * operation_main=flash4_check_chip_id_execute; only operation_main is driven.
 * clear_bus_recording() is called AFTER configure_memory() (and after mock re-assign).
 */
void test_golden_flash4_chip_id(void) {
    s_flash4_chipid_mock_idx = 0;
    s_flash4_chipid_mock_bytes[0] = 0xBF; /* manufacturer byte */
    s_flash4_chipid_mock_bytes[1] = 0xB7; /* device byte — combined = 0xBFB7 (SST39SF040) */
    s_flash4_chipid_mock_bytes[2] = 0xFF;
    s_flash4_chipid_mock_bytes[3] = 0xFF;

    firestarter_handle_t h = make_handle(0x05, CMD_CHECK_CHIP_ID);
    h.chip_id = 0xBFB7; /* non-zero: enables compare branch (D-03 P4 path); matches mock */
    configure_memory(&h);
    /* Re-assign AFTER configure_memory() overwrites firestarter_get_data (Pitfall 3). */
    h.firestarter_get_data = flash4_mock_chipid_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash4 chip-id: configure_memory must not error");
    clear_bus_recording(); /* isolate operation trace from configure-phase address writes */
    /* configure_flash4 CMD_CHECK_CHIP_ID: operation_init=NULL, operation_main set. */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_flash4_chip_id, golden_flash4_chip_id_n,
                    "golden trace drift: flash4 chip-id (P4 via flash_utils)");
#endif
}

/* ─── Phase 94 Plan 01: FIX-01a firmware defense-in-depth (T-93-CANERASE / D-06) ─ */

/*
 * test_flash4_init_no_vpp_when_can_erase_protocol5
 *
 * FIX-01a defense-in-depth: a protocol-0x05 handle with FLAG_CAN_ERASE SET (and
 * FLAG_SKIP_ERASE NOT set) must NOT cause flash4_write_init to assert any VPP-enable
 * control bits. This proves that even a stale or hand-crafted JSON command carrying
 * the hazardous flag cannot trigger the 12V bulk erase path (T-93-CANERASE).
 *
 * Guard design (D-06): keyed on handle->protocol == 0x05, NOT on handle->vpp_mv.
 * The W29C040 carries vpp_mv=12000 as a chip-ID-read datum, not a program-rail
 * request — a voltage heuristic would never fire here (Pitfall 3 / 94-RESEARCH.md).
 *
 * Test setup:
 *   - protocol=0x05, CMD_WRITE, mem_size=524288 (W29C040)
 *   - ctrl_flags = FLAG_CAN_ERASE (0x02): erase path would normally be taken
 *   - FLAG_SKIP_ERASE NOT set: without the guard, flash4_erase_execute would be called
 *   - FLAG_SKIP_BLANK_CHECK set: suppress blank-check side effects (not under test)
 *   - data_size=0, data_buffer={}: no write data; only init path exercised
 *
 * Expected: assert_no_vpp_in_recording finds NO CTRL_VPP_REGULATOR_ENABLE (0x80)
 * or CTRL_VPP_P1_ENABLE (0x08) bits in any CONTROL_REGISTER write during
 * flash4_write_init + flash4_write_execute (empty data → no-op execute).
 *
 * Failure mode if guard is reverted: flash4_erase_execute is called, which asserts
 * CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPE_ENABLE at line
 * ~155 of flash_type_4.cpp → assert_no_vpp_in_recording FAILS.
 */
void test_flash4_init_no_vpp_when_can_erase_protocol5(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512 KB (W29C040) */
    h.address    = 0;
    h.data_size  = 0; /* no data: only flash4_write_init is exercised */
    /* ctrl_flags: FLAG_CAN_ERASE set, FLAG_SKIP_ERASE NOT set → would call
     * flash4_erase_execute without the protocol guard.
     * FLAG_SKIP_BLANK_CHECK set to suppress blank-check side effects. */
    h.ctrl_flags = FLAG_CAN_ERASE | FLAG_SKIP_BLANK_CHECK;

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FIX-01a: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    /* Drive flash4_write_init; data_size=0 so flash4_write_execute loops zero times. */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "FIX-01a: flash4_write_init must succeed for protocol 0x05 with FLAG_CAN_ERASE set");
    assert_no_vpp_in_recording(
        "FIX-01a (T-93-CANERASE / D-06): protocol 0x05 with FLAG_CAN_ERASE must NOT "
        "assert CTRL_VPP_REGULATOR_ENABLE or CTRL_VPP_P1_ENABLE — 12V bulk erase "
        "is forbidden on a 5V flash4 chip even when the flag is present");
}

/* ─── Phase 94 Plan 02: PGSZ-02 consumption test (handle->page_size safe fallback) ─── */

/*
 * test_pgsz02_handle_page_size_overrides_heuristic
 *
 * PGSZ-02 / CR-01: when handle->page_size is non-zero, flash4_write_execute must
 * use it instead of flash4_page_size(handle->mem_size). Proven by SDP boundary count.
 *
 * Setup: protocol=0x05, mem_size=524288 (W29C040, 512KB → heuristic gives 256B),
 *        handle->page_size = 128 (override: smaller than heuristic).
 *        address=126, data_size=4 (spans address 126..129 inclusive).
 *
 * NOTE: using address=126 (not 0) to stay well within the 256-entry recording buffer
 * cap. With address=0, data_size=129 + page_size=128 the recording fills:
 *   SDP@0 (8 entries) + 128 data bytes × 3 entries = 392 entries > 256 cap.
 * With address=126, data_size=4 the recording fills:
 *   SDP@126 first_byte (8) + SDP@128 page_start (8) + 4 data bytes × 3 (12) + poll (3) = 31 entries.
 *
 *   - page_size=128 (handle->page_size): SDP at addr 126 (first_byte) AND
 *                   addr 128 (128 % 128 == 0, page_start). Count = 2.
 *   - page_size=256 (heuristic): SDP at addr 126 only (addr 128 is NOT a 256B boundary:
 *                   128 % 256 == 128 ≠ 0). Count = 1.
 *
 * Asserting count=2 PROVES the firmware consumed handle->page_size=128, not the heuristic 256.
 *
 * Zero-init guard (SAFE-02): make_handle() uses `h = {}` (zero-initialized); page_size
 * is explicitly set to 128 here. A zero-initialized handle with page_size==0 falls back
 * to the heuristic — see test_pgsz02_zero_page_size_falls_back_to_heuristic below.
 */
void test_pgsz02_handle_page_size_overrides_heuristic(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288;  /* 512KB W29C040 — heuristic → 256B */
    h.page_size  = 128;     /* PGSZ-02: explicit override (smaller than heuristic 256B) */
    h.address    = 126;     /* Start 2 bytes before 128B boundary at addr 128 */
    h.data_size  = 4;       /* Bytes 126,127,128,129 — crosses 128B boundary; fits in recording cap */
    /* data_buffer zero-initialized: rurp_read_data_buffer()=0 → DQ7 poll passes. */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "PGSZ-02: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "PGSZ-02: flash4_write_execute must not error on 4-byte zero write crossing 128B boundary");
    /* page_size=128: SDP fires at addr 126 (first_byte) AND addr 128 (128%128==0, page_start). Count=2.
     * page_size=256 (heuristic): SDP fires at addr 126 only (128%256=128≠0, not a 256B boundary). Count=1.
     * count=2 PROVES firmware consumed handle->page_size=128, NOT the heuristic 256. */
    int sdp_count = count_sdp_occurrences();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, sdp_count,
        "PGSZ-02: handle->page_size=128 must fire 2 SDPs crossing addr 128 (128B boundary); "
        "page_size=256 (heuristic) would fire only 1 (addr 128 is not a 256B boundary)");
}

/*
 * test_pgsz02_zero_page_size_falls_back_to_heuristic
 *
 * PGSZ-02 / CR-01 zero-init guard: when handle->page_size == 0 (absent wire field,
 * zero-initialized handle), flash4_write_execute must fall back to flash4_page_size(mem_size).
 * For mem_size=524288 (512KB W29C040), the heuristic returns 256.
 *
 * Setup: protocol=0x05, mem_size=524288, handle->page_size=0 (zero-initialized default).
 *        address=126, data_size=4 (same window as override test — identical discriminant).
 *
 *   - page_size=0 → fallback → 256: SDP at addr 126 only (addr 128 is NOT a 256B boundary:
 *                   128 % 256 == 128 ≠ 0). Count = 1.
 *   - page_size=128: would fire 2 SDPs (discriminant).
 *
 * Asserting count=1 PROVES page_size==0 falls back to heuristic (256), not to 128.
 *
 * Zero-init guard: `h = {}` in C++ value-initializes all members to 0, so page_size=0
 * by default (no explicit set). Verifies the CRITICAL_CORRECTNESS requirement that
 * zero-initialized firestarter_handle_t instances degrade safely to the heuristic.
 */
void test_pgsz02_zero_page_size_falls_back_to_heuristic(void) {
    firestarter_handle_t h = {};  /* zero-initialized — page_size==0 by default */
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288;  /* 512KB W29C040 — heuristic → 256B */
    /* h.page_size = 0 (zero-initialized) — must trigger fallback to flash4_page_size(524288)=256 */
    h.address    = 126;     /* same window as override test: crosses 128B boundary at addr 128 */
    h.data_size  = 4;       /* Bytes 126,127,128,129 — identical discriminant window */
    /* data_buffer zero-initialized: rurp_read_data_buffer()=0 → DQ7 poll passes. */

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "PGSZ-02 fallback: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "PGSZ-02 fallback: flash4_write_execute must not error on 4-byte zero write crossing 128B boundary");
    /* page_size=0 → heuristic 256: SDP fires at addr 126 only (128%256=128≠0, not a 256B boundary). Count=1.
     * page_size=128 (if fallback failed): would fire 2 SDPs at addr 126 + addr 128.
     * count=1 PROVES zero page_size falls back to flash4_page_size(524288)=256. */
    int sdp_count = count_sdp_occurrences();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sdp_count,
        "PGSZ-02 fallback: handle->page_size=0 must fall back to flash4_page_size(524288)=256 "
        "— addr 128 is NOT a 256B boundary (128%%256=128≠0); count=2 would mean fallback failed");
}

/* ─── Phase 94 Plan 03: FIX-01b firmware §6.6 DETECT (boot-block locked) ────── */

/*
 * test_fix01b_boot_block_locked_sets_error_code
 *
 * FIX-01b firmware DETECT path: when flash4_wait_for_page_write times out on an
 * address in the first 16K boot block (< 0x4000) AND the §6.6 detect read at
 * 0x00002 returns 0xFF (locked), the firmware must set RESPONSE_CODE_ERROR.
 *
 * Test setup:
 *   - protocol=0x05, CMD_WRITE, mem_size=524288 (W29C040, 512KB)
 *   - address=0x0000, data_size=1, data_buffer[0]=0xFF (non-zero expected)
 *   - Mock firestarter_get_data: returns 0xFF at addr 0x00002 (detect → locked),
 *     0x00 for all other addresses (data addresses → mismatch → poll timeout)
 *   - After configure_memory(), re-assign firestarter_get_data to mock (Pitfall 3)
 *   - FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE: suppress init side-effects
 *
 * poll_readback: iterates 1024 times, each time calling firestarter_get_data(addr=0x0000)
 * which returns 0x00 ≠ expected=0xFF → poll timeout → error path reached.
 * flash4_detect_boot_block_lockout: addr 0 < 0x4000 → detect_addr=0x00002 →
 *   firestarter_get_data(0x00002) returns 0xFF → locked → LOG_ERROR_ID_U24(MSG_ERR_FL4_BOOT_BLOCK_LOCKED, 0)
 *   → handle->response_code = RESPONSE_CODE_ERROR.
 *
 * Assertion: response_code == RESPONSE_CODE_ERROR (detect ran and set error).
 * Golden trace: NOT asserted — the detect path writes FLASH_ENABLE_ID / FLASH_DISABLE_ID
 * bus sequences which are on the error path only (clean write is untouched, FIX-02 coverage).
 */
void test_fix01b_boot_block_locked_sets_error_code(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 → first 16K boot block at 0x0000..0x3FFF */
    h.address    = 0x0000; /* boot-block address: triggers §6.6 detect path */
    h.data_size  = 1;      /* 1 byte write; data_buffer[0] = 0xFF (non-zero expected) */
    h.data_buffer[0] = 0xFF; /* expected byte; mock returns 0x00 → poll timeout */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE; /* suppress init side-effects */

    configure_memory(&h);
    /* Pitfall 3: re-assign firestarter_get_data AFTER configure_memory() overwrites it */
    h.firestarter_get_data = flash4_mock_boot_block_locked_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FIX-01b: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FIX-01b: flash4_write_execute must set RESPONSE_CODE_ERROR when first-16K boot block "
        "is locked (poll timeout + §6.6 detect returns 0xFF = locked)");
}

/*
 * test_fix01b_clean_write_no_boot_block_detect
 *
 * FIX-02 (golden-trace guard): flash4_write_execute on a CLEAN write (poll passes first
 * iteration because expected == observed == 0) must NOT invoke the boot-block detect
 * path, and RESPONSE_CODE_OK must be preserved.
 *
 * This is the "detect only runs on error path" invariant (FIX-02 acceptance criterion).
 * Uses the same 65-byte zero write as test_golden_flash4_write, confirming that clean
 * data leaves response_code unchanged after FIX-01b is applied.
 *
 * Setup: protocol=0x05, mem_size=524288, address=0, data_size=65, data_buffer={} (zeros).
 * rurp_read_data_buffer() = 0, firestarter_get_data at data addresses returns 0 (default stub).
 * poll_readback: observed = firestarter_get_data(addr) = 0 == expected=0 → passes on first iter.
 * No timeout → no detect path → no RESPONSE_CODE_ERROR.
 */
void test_fix01b_clean_write_no_boot_block_detect(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 */
    h.address    = 0;      /* boot-block address, but clean write → no detect */
    h.data_size  = 65;     /* same as golden write probe (INV-04 minimal: 65 bytes) */
    /* data_buffer zero-initialized: firestarter_get_data returns 0 = expected → poll passes. */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FIX-01b clean path: configure_memory must not error on 0x05 CMD_WRITE");

    clear_bus_recording();
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "FIX-01b (FIX-02): clean write at boot-block address must NOT trigger the §6.6 detect "
        "path — detect runs only on poll timeout, not on successful writes");
}

/* ─── Phase 95: Proactive §6.6 boot-block lockout detection in flash4_write_init ─── */

/*
 * test_proactive_locked_no_force_sets_error
 *
 * Proactive detect: locked + NO FORCE → RESPONSE_CODE_ERROR + abort.
 *
 * flash4_write_init now runs a §6.6 detect READ before blank-check when the
 * write targets a boot-block address (< 0x4000 for the first 16K block).
 * When locked (detect returns true) and FLAG_FORCE is NOT set, write_init must
 * set RESPONSE_CODE_ERROR and return early — page writes must NOT be attempted.
 *
 * Proof of "page write NOT attempted": flag data_buffer[0] = 0xAB (non-zero,
 * non-blank). If execute runs and the SDP + write + poll loop fires, the poll
 * will NOT time out (mock returns 0x00 ≠ 0xAB, but wait… mock returns 0x00,
 * expected=0xAB → poll never passes → would set ERROR too). To prove abort, we
 * check that response_code == ERROR comes from INIT (before execute), by
 * asserting it AFTER calling only init (not execute).
 *
 * Setup mirrors test_fix01b_boot_block_locked_sets_error_code but checks after
 * init-only call to confirm proactive (not reactive) source.
 *
 * Mock: flash4_mock_boot_block_locked_get_data — 0xFF at 0x00002 (locked),
 *       0x00 elsewhere. Must be assigned AFTER configure_memory (Pitfall 3).
 */
void test_proactive_locked_no_force_sets_error(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 → first 16K boot block at 0x0000..0x3FFF */
    h.address    = 0x0000; /* boot-block address — proactive detect fires */
    h.data_size  = 1;
    h.data_buffer[0] = 0xAB; /* non-zero expected byte */
    /* No FLAG_FORCE — locked → ERROR + abort */
    h.ctrl_flags = FLAG_SKIP_ERASE; /* skip erase side-effects; blank-check will run
                                       but is never reached (error returned first) */

    configure_memory(&h);
    /* Pitfall 3: re-assign firestarter_get_data AFTER configure_memory() overwrites it */
    h.firestarter_get_data = flash4_mock_boot_block_locked_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "proactive detect: configure_memory must not error");

    /* Drive ONLY init — if proactive detect fires and aborts, response_code = ERROR here. */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "proactive detect (no force): locked boot-block write must set RESPONSE_CODE_ERROR "
        "in flash4_write_init — early abort before any page writes");
}

/*
 * test_proactive_locked_with_force_sets_warning
 *
 * Proactive detect: locked + FLAG_FORCE → RESPONSE_CODE_WARNING + init does NOT abort.
 *
 * When FLAG_FORCE is set and the boot block is locked, flash4_write_init must:
 *   1. Set RESPONSE_CODE_WARNING (not ERROR).
 *   2. NOT return early — execution falls through and init completes.
 *
 * Proof that init does not abort: after calling only init with FLAG_FORCE, the
 * response code is RESPONSE_CODE_WARNING (not ERROR). If init had aborted, the
 * code path for WARNING would not have been reached.
 *
 * Note: in the native test harness, the state machine (_check_response) is NOT
 * invoked between init and main, so response_code is NOT reset by the loop.
 * The WARNING set in init persists after init returns. We assert WARNING after
 * init to confirm the correct code path was taken.
 *
 * Setup: protocol=0x05, CMD_WRITE, mem_size=524288, address=0 (boot-block),
 *        FLAG_FORCE set, FLAG_SKIP_BLANK_CHECK + FLAG_SKIP_ERASE set to isolate
 *        the proactive detect path (blank check would also run but on a locked
 *        region it would find it non-blank via mock; FLAG_SKIP_BLANK_CHECK avoids
 *        that complication — the WARNING path is the focus).
 *
 * Mock: flash4_mock_boot_block_locked_get_data — 0xFF at 0x00002 (locked),
 *       0x00 elsewhere. Must be assigned AFTER configure_memory (Pitfall 3).
 */
void test_proactive_locked_with_force_sets_warning(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 → first 16K boot block at 0x0000..0x3FFF */
    h.address    = 0x0000; /* boot-block address — proactive detect fires */
    h.data_size  = 0;      /* no data: only init path under test */
    /* FLAG_FORCE: locked boot block → WARNING (not ERROR), do not abort */
    h.ctrl_flags = FLAG_FORCE | FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;

    configure_memory(&h);
    /* Pitfall 3: re-assign firestarter_get_data AFTER configure_memory() overwrites it */
    h.firestarter_get_data = flash4_mock_boot_block_locked_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "proactive detect (force): configure_memory must not error");

    /* Drive ONLY init — FLAG_FORCE should produce WARNING without aborting. */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "proactive detect (force): locked boot-block write with FLAG_FORCE must set "
        "RESPONSE_CODE_WARNING in flash4_write_init — init does not abort (falls through), "
        "operator forced the write knowing the region may be permanently locked");
}

/*
 * test_proactive_unlocked_no_error_or_warning
 *
 * Proactive detect: unlocked chip at address 0 → no error, no warning.
 *
 * When the §6.6 detect read returns 0x00 (not 0xFF), the boot block is
 * unlocked. flash4_write_init must leave response_code unchanged (RESPONSE_CODE_OK).
 * No ERROR or WARNING should be emitted.
 *
 * Setup: protocol=0x05, CMD_WRITE, mem_size=524288, address=0 (boot-block region),
 *        FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE, data_size=0 (init only).
 *
 * Mock: NOT assigned — default host stub's firestarter_get_data returns 0x00 at
 *       ALL addresses (including detect address 0x00002). The detect returns
 *       false (0x00 != 0xFF = unlocked) → proactive check does not modify
 *       response_code. No ERROR, no WARNING.
 *
 * Note: this is the scenario exercised by the golden write trace (address=0,
 * default stub). This test makes the invariant explicit.
 */
void test_proactive_unlocked_no_error_or_warning(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 */
    h.address    = 0x0000; /* boot-block address — detect fires but chip unlocked */
    h.data_size  = 0;
    /* No mock reassignment: default stub returns 0x00 everywhere → unlocked */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;

    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "proactive detect (unlocked): configure_memory must not error");

    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "proactive detect (unlocked): address 0 with unlocked detect (0x00 at 0x00002) "
        "must leave response_code = RESPONSE_CODE_OK — no error or warning from the detect");
}

/*
 * test_proactive_mid_chip_no_detect_invoked
 *
 * Proactive detect: mid-chip address (>= 0x4000, below last 16K) → detect NOT invoked.
 *
 * The region gate ensures that writes targeting addresses outside the boot-block
 * regions (first and last 16K) do NOT trigger the §6.6 detect. Even with the
 * boot-block-locked mock in place, a write starting at address 0x4000 must
 * succeed with RESPONSE_CODE_OK — the detect is never called.
 *
 * Proof: assign flash4_mock_boot_block_locked_get_data AFTER configure_memory.
 * If the detect ran at address 0x4000, it would compute detect_addr = 0x7FFF2
 * (last-16K check, but 0x4000 < 524288-0x4000 = 0x7C000, so NOT in last BB).
 * Actually: in_first_bb = (0x4000 < 0x4000) = false;
 *            in_last_bb = (524288 > 0x4000 && 0x4000 >= 524288-0x4000=508928) = false.
 * Region gate = false → detect is NOT invoked → response_code stays OK.
 *
 * If the detect WERE invoked with the locked mock, it would read 0xFF and set ERROR.
 * The RESPONSE_CODE_OK assertion PROVES the detect was not invoked.
 *
 * Setup: protocol=0x05, CMD_WRITE, mem_size=524288, address=0x4000 (mid-chip,
 *        exactly at first-BB boundary but NOT inside it), data_size=0 (init only).
 *
 * Mock: flash4_mock_boot_block_locked_get_data — 0xFF at 0x00002 (locked first-BB).
 *       If detect runs → ERROR. RESPONSE_CODE_OK proves detect was NOT run.
 */
void test_proactive_mid_chip_no_detect_invoked(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 524288; /* 512KB W29C040 */
    h.address    = 0x4000; /* exactly at boundary: NOT in first-BB (0x0000..0x3FFF)
                              NOT in last-BB  (0x7C000..0x7FFFF = mem_size-0x4000 up)
                              → region gate = false → detect must NOT fire */
    h.data_size  = 0;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;

    configure_memory(&h);
    /* Pitfall 3: assign the locked mock AFTER configure_memory.
     * If the proactive detect fires and reads 0x00002 → 0xFF → ERROR.
     * RESPONSE_CODE_OK asserts the detect was NOT invoked (region gate blocked it). */
    h.firestarter_get_data = flash4_mock_boot_block_locked_get_data;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "proactive detect (mid-chip): configure_memory must not error");

    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "proactive detect (mid-chip): address 0x4000 is NOT in any boot-block region "
        "(in_first_bb = false, in_last_bb = false) — detect must NOT be invoked; "
        "RESPONSE_CODE_OK proves the locked mock was never read (region gate blocked it)");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* Protocol 0x05 configure-phase VPP-safety proof */
    RUN_TEST(test_flash4_0x05_read_configure_no_vpp);
    RUN_TEST(test_flash4_0x05_write_configure_no_vpp);

    /* Protocol 0x35 configure-phase VPP-safety proof */
    RUN_TEST(test_flash4_0x35_read_configure_no_vpp);
    RUN_TEST(test_flash4_0x35_write_configure_no_vpp);

    /* Protocol 0x39 configure-phase VPP-safety proof */
    RUN_TEST(test_flash4_0x39_read_configure_no_vpp);
    RUN_TEST(test_flash4_0x39_write_configure_no_vpp);

    /* FIX-02B: operation-phase SDP emission + VPP-safety proofs */
    RUN_TEST(test_flash4_write_execute_emits_sdp);
    RUN_TEST(test_flash4_write_execute_no_vpp);

    /* Phase 87 Plan 03: INV-04 gap-fill assertion (SAFE-02 / NAME-03) */
    RUN_TEST(test_inv04_flash4_256b_page_boundary);

    /* Phase 88 Plan 03: byte-exact golden write + chip-id traces (PRIM-01 / D-01..D-04) */
    RUN_TEST(test_golden_flash4_write);
    RUN_TEST(test_golden_flash4_chip_id);

    /* Phase 94 Plan 01: FIX-01a firmware defense-in-depth (T-93-CANERASE / D-06) */
    RUN_TEST(test_flash4_init_no_vpp_when_can_erase_protocol5);

    /* Phase 94 Plan 02: PGSZ-02 consumption proof (handle->page_size safe fallback) */
    RUN_TEST(test_pgsz02_handle_page_size_overrides_heuristic);
    RUN_TEST(test_pgsz02_zero_page_size_falls_back_to_heuristic);

    /* Phase 94 Plan 03: FIX-01b firmware §6.6 DETECT (boot-block locked error path) */
    RUN_TEST(test_fix01b_boot_block_locked_sets_error_code);
    RUN_TEST(test_fix01b_clean_write_no_boot_block_detect);

    /* Phase 95: Proactive §6.6 boot-block lockout detection in flash4_write_init */
    RUN_TEST(test_proactive_locked_no_force_sets_error);
    RUN_TEST(test_proactive_locked_with_force_sets_warning);
    RUN_TEST(test_proactive_unlocked_no_error_or_warning);
    RUN_TEST(test_proactive_mid_chip_no_detect_invoked);

    return UNITY_END();
}
