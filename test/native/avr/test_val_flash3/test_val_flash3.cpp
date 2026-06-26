/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 71 Plan 04 — Tier-1 validation suite for the Flash Type 3 family.
 * Phase 87 Plan 03 — INV-09 gap-fill assertion (NAME-03 / SAFE-02).
 * HARN-01 / D-07 / T-71-WIRED-WRONG.
 *
 * Proves configure_flash3 is a 5V-only handler (no VPP regulator use).
 * BY SIDE-EFFECT via the recording bus stub:
 *
 *   For CMD_READ and CMD_WRITE (configure-only phase): configure_memory() writes
 *   only address bits to LSB/MSB/CONTROL registers via mem_util_set_address.
 *   configure_flash3 sets function pointers but writes no VPP-enable CTL bits.
 *   CTRL_VPP_REGULATOR_ENABLE, CTRL_VPP_P1_ENABLE, and CTRL_VPP_VPE_DROP_ENABLE
 *   must NEVER appear set in any recorded CONTROL_REGISTER write.
 *
 *   This test can go RED if configure_flash3 is accidentally wired to a
 *   VPP-enabling configure path (T-71-WIRED-WRONG).
 *
 * Protocol covered: 0x06 (FLASH_AMD_ALT / AMD unlock, sector erase).
 *
 * VPP: NONE — configure_flash3 is a 5V AMD-style handler. The flash3 erase path
 * uses flash_execute_command which writes data bytes only, not VPP CTL bits.
 *
 * INV gap-fill assertion added by Phase 87 Plan 03 (SAFE-02 third target):
 *   INV-09: test_inv09_flash3_sst39sf040_keep_flash_eeprom
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

/* Golden expected arrays.
 * Each .inc file is a comma-list of { reg, data } rows produced by GOLDEN_BLESS mode. */
static const golden_entry_t golden_flash3_write[] = {
#include "golden_flash3_write.inc"
};
static const int golden_flash3_write_n =
    (int)(sizeof(golden_flash3_write) / sizeof(golden_flash3_write[0]));

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by flash3_write_init (flash3_erase_execute + FLASH_ERASE_DELAY_MS).
     * delayMicroseconds() is called by memory_set_data (3µs settle).
     * millis() is called by flash_util_verify_operation (DQ7 poll timeout loop).
     * Stub all three so the operation-phase golden tests don't abort on an
     * unmocked ArduinoFake virtual (Pitfall 4). */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    clear_bus_recording();
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x06;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch */
    h.mem_size   = 524288; /* 512 KB (AM29F040) */
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
void test_flash3_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash3 CMD_READ must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_WRITE must record zero VPP-enable bits */
void test_flash3_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash3 CMD_WRITE must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_ERASE must record zero VPP-enable bits */
void test_flash3_erase_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_ERASE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_ERASE");
    assert_no_vpp_in_recording(
        "configure_flash3 CMD_ERASE must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_BLANK_CHECK must record zero VPP-enable bits */
void test_flash3_blank_check_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_BLANK_CHECK);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_BLANK_CHECK");
    assert_no_vpp_in_recording(
        "configure_flash3 CMD_BLANK_CHECK must NOT set any VPP-enable CTL bit");
}

/* ─── Phase 87 Plan 03: INV-09 gap-fill assertion (SAFE-02) ─────────────────── */

/* INV-09 — SST39SF040 (0x06) retains electrical.type=Flash/EEPROM classification.
 * SAFE-02 third target: grep -rn INV-09 must hit doc + handler + this test.
 * Asserts: protocol 0x06 dispatches to configure_flash3() — a 5V AMD-style handler
 * with no VPP regulator involvement — and NOT to configure_eprom() which would
 * enable the 12V VPP boost regulator on a 5V-only part (BLOCKER-2 violation).
 *
 * The SST39SF040 classification invariant: electrical.type="Flash/EEPROM" (FLAG_CAN_ERASE
 * set) must not be confused with the UV-EPROM path (configure_eprom, no erase, VPP boost).
 * Phase-86 variant decode preserves this by routing 0x06 chips exclusively to configure_flash3().
 *
 * Two observable behaviors that collectively confirm the invariant:
 *   1. configure_memory with 0x06 CMD_WRITE succeeds and wires firestarter_operation_main
 *      (non-NULL, confirming configure_flash3's function-pointer wiring).
 *   2. No CTRL_VPP_REGULATOR_ENABLE or CTRL_VPP_P1_ENABLE bit appears in the recording
 *      (5V-only part — configure_flash3 never touches the VPP regulator).
 * Source: flash_type_3.cpp INV-09 header block; PROTOCOLS.md §1.2 and §3 INV-09 row. */
void test_inv09_flash3_sst39sf040_keep_flash_eeprom(void) {
    /* SST39SF040 (0x06): routes to configure_flash3 (5V AMD NOR flash, NOT configure_eprom). */
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "INV-09: configure_memory must not error on 0x06 CMD_WRITE (SST39SF040 Flash/EEPROM)");
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "INV-09: 0x06 CMD_WRITE must wire firestarter_operation_main (configure_flash3, not configure_eprom)");
    /* No VPP-enable bits: configure_flash3 is 5V-only (BLOCKER-2 — must never reach VPP regulator). */
    assert_no_vpp_in_recording(
        "INV-09: 0x06 (SST39SF040 Flash/EEPROM) configure-phase must NOT set any VPP-enable CTL bit");
    /* Repeat for CMD_ERASE to confirm the full 0x06 dispatch stays in flash3. */
    clear_bus_recording();
    firestarter_handle_t he = make_handle(CMD_ERASE);
    configure_memory(&he);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, he.response_code,
        "INV-09: configure_memory must not error on 0x06 CMD_ERASE (SST39SF040)");
    assert_no_vpp_in_recording(
        "INV-09: 0x06 CMD_ERASE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── Phase 88 Plan 03: Golden register trace (PRIM-01 / SAFE-02 / D-01..D-04) ── */

/*
 * test_golden_flash3_write — byte-exact golden trace for the flash3 0x06 write path.
 *
 * flash3 (FLASH-AMD-ALT / SST39SF040) is a 5V-only AMD-unlock NOR flash; it has
 * NO chip-id P4 site (D-03 coverage map: write only, no chip-id fixture).
 *
 * Input: minimal representative data_size=1, address=0, FLAG_SKIP_BLANK_CHECK |
 * FLAG_SKIP_ERASE (D-04).  chip_id=0 so the chip-id check inside flash3_write_init
 * is suppressed.  configure_memory wires operation_init = flash3_write_init and
 * operation_main = flash3_write_execute.
 *
 * With FLAG_SKIP_BLANK_CHECK and FLAG_SKIP_ERASE set, flash3_write_init is a
 * no-op (both the erase and blank-check branches are suppressed).  The trace is
 * therefore entirely from flash3_write_execute:
 *   - flash_execute_command(FLASH_ENABLE_WRITE): 3-cycle SDP unlock at 0x5555/0x2AAA/0x5555
 *     → flash_util_byte_flipping writes CTRL (CTRL_READ_WRITE=0), then LSB+MSB for each
 *     address in the unlock sequence, then CTRL again.
 *   - memory_set_data (h.firestarter_set_data): writes LSB+MSB of the data address,
 *     then chip-enable pulse (no CTL register writes visible in the 8-bit recording
 *     for rurp_chip_enable/disable — those are rurp_set_control_pin calls, not
 *     rurp_write_to_register calls, so they do NOT appear in the recording).
 *   - flash_util_verify_operation: DQ7 poll; rurp_read_data_buffer() stub returns 0;
 *     data_buffer[0]=0x00 so DQ7 match on first poll (0 & 0x80 == 0 & 0x80 = 0).
 *     CTL register is written to go back to read mode (CTRL_READ_WRITE=1 then cleanup).
 *
 * Note: clear_bus_recording() is called AFTER configure_memory() to isolate the
 * operation trace from the configure-phase address writes (mem_util_set_address).
 *
 * Low-byte-only semantics (Pitfall 1): CTRL_VPP_VPE_DROP_ENABLE = 0x100 on
 * HARDWARE_REVISION builds is NOT captured.  The existing INV-09 assertion
 * (which scans for VPP-enable bits) is the complementary guard for 8-bit-fit
 * VPP bits; this golden trace adds the full ordered sequence.
 */
void test_golden_flash3_write(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    h.data_size   = 1; /* minimal representative input (D-04): 1 SDP unlock + 1 byte program */
    h.ctrl_flags  = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE; /* suppress init side-effects */
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "golden flash3 write: configure_memory must not error");
    clear_bus_recording(); /* isolate operation trace from configure-phase address writes */
    if (h.firestarter_operation_init) h.firestarter_operation_init(&h);
    if (h.firestarter_operation_main) h.firestarter_operation_main(&h);
#ifdef GOLDEN_BLESS
    print_trace_inc();
#else
    assert_trace_eq(golden_flash3_write, golden_flash3_write_n,
                    "golden trace drift: flash3 0x06 write");
#endif
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* 5V-only proof: no VPP-enable CTL bit for any command in the configure phase */
    RUN_TEST(test_flash3_read_configure_no_vpp);
    RUN_TEST(test_flash3_write_configure_no_vpp);
    RUN_TEST(test_flash3_erase_configure_no_vpp);
    RUN_TEST(test_flash3_blank_check_configure_no_vpp);

    /* Phase 87 Plan 03: INV-09 gap-fill assertion (SAFE-02 / NAME-03) */
    RUN_TEST(test_inv09_flash3_sst39sf040_keep_flash_eeprom);

    /* Phase 88 Plan 03: byte-exact golden write trace (PRIM-01 / D-01..D-04) */
    RUN_TEST(test_golden_flash3_write);

    return UNITY_END();
}
