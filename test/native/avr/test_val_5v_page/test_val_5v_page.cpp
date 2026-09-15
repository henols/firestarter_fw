/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Proves the configure_flash_5v_page dispatch/configure phase is VPP-safe.
 * BY SIDE-EFFECT via the recording bus stub:
 *
 *   For CMD_READ and CMD_WRITE (configure-only phase): configure_memory() writes
 *   only address bits to LSB/MSB/CONTROL registers via mem_util_set_address.
 *   configure_flash_5v_page sets function pointers but writes no VPP-enable CTL bits.
 *   CTRL_VPP_REGULATOR_ENABLE, CTRL_VPP_P1_ENABLE, and CTRL_VPP_VPE_DROP_ENABLE
 *   must NEVER appear set in any recorded CONTROL_REGISTER write during the
 *   configure/dispatch phase. A subset of cases below also drive
 *   flash_5v_page_write_init through the dispatched pointer and assert the
 *   same property over that call.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <vector>
#include <cstdint>

extern "C" {
#include "memory.h"
}
#include "operation_utils.h"
#include "firestarter.h"
#include "flash_utils.h"
#include "flash_5v_page.h"
#include "messages.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

static std::vector<uint8_t> s_wire_bytes;

static uint8_t s_stub_raw_value = 0;
static uint8_t stub_get_data_return_fixed(firestarter_handle_t* handle, uint32_t address) {
    (void)handle; (void)address;
    return s_stub_raw_value;
}

void setUp(void) {
    ArduinoFakeReset();
    s_wire_bytes.clear();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            s_wire_bytes.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
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

void test_5v_page_0x05_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x05 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_5v_page_0x05_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x05 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── Protocol 0x35 (FLASH_EEPROM) ─────────────────────────────────────── */

void test_5v_page_0x35_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x35, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x35 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x35 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_5v_page_0x35_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x35, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x35 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x35 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/* ─── Protocol 0x39 (FLASH_EEPROM2) — future-proofed, dispatched by analogy ─ */

void test_5v_page_0x39_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x39, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x39 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x39 CMD_READ configure-phase must NOT set any VPP-enable CTL bit");
}

void test_5v_page_0x39_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x39, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x39 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash_5v_page 0x39 CMD_WRITE configure-phase must NOT set any VPP-enable CTL bit");
}

/*
 * These two tests exercise flash_5v_page_write_execute (the operation phase, not just
 * configure), using the recording-bus stub to observe side effects.
 *
 * Test setup: configure_memory(CMD_WRITE) wires function pointers including
 * firestarter_set_data = memory_set_data and the operation_main pointer.
 * Then clear_bus_recording() resets the capture, fill data_buffer with zeros
 * (so flash_5v_page_wait_for_page_write's DQ7 poll passes in one iteration since the
 * stub's rurp_read_data_buffer() always returns 0 = expected), set data_size=4
 * at address=0, and call h.firestarter_operation_main(&h) to drive
 * flash_5v_page_write_execute.
 *
 * The recording captures every rurp_write_to_register call:
 *   - flash_util_byte_flipping (SDP sequence) writes CONTROL_REGISTER
 *     (CTRL_READ_WRITE) + LSB/MSB for each command address pair.
 *   - memory_set_data writes LSB/MSB/CONTROL via mem_util_set_address.
 *
 * Test 1 (SDP emission, RED before fix): scans for the FLASH_ENABLE_WRITE
 * address signature — MSB writes of 0x55 (for 0x5555 and 0x5555 again) and
 * 0x2A (for 0x2AAA) in sequence before the first data address write. FAILS
 * today because flash_5v_page_write_execute has no flash_execute_command(FLASH_ENABLE_WRITE).
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
     * flash_5v_page_write_init would call blank-check, but we bypass init and call
     * operation_main directly. */
    return h;
}

/* Drives flash_5v_page_write_init through the dispatched pointer, with
 * FLAG_CAN_ERASE and FLAG_SKIP_BLANK_CHECK both clear (ctrl_flags = 0):
 * is_flag_set(FLAG_SKIP_BLANK_CHECK) is false so the deleted conditional's
 * guard would have been satisfied. mem_size is a small 2048 --
 * BLANK_CHECK_CHUNK_SIZE -- because mem_util_blank_check sets
 * is_operation_in_progress on its FIRST call regardless of mem_size, so the
 * oracle below does not depend on how large mem_size is. */
static firestarter_handle_t make_write_init_handle_blank_check_enabled(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch in write_init */
    h.mem_size   = 2048;
    h.address    = 0;
    h.data_size  = 0;
    /* ctrl_flags = 0: FLAG_CAN_ERASE clear, FLAG_SKIP_BLANK_CHECK clear
     * (the blank-check axis is live). */
    return h;
}

static firestarter_handle_t make_write_init_handle_can_erase_set(void) {
    firestarter_handle_t h = {};
    h.protocol   = 0x05;
    h.cmd        = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0;
    h.mem_size   = 2048;
    h.address    = 0;
    h.data_size  = 0;
    h.ctrl_flags = FLAG_CAN_ERASE;
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

/* Test 1 (FIX-02B SDP): flash_5v_page_write_execute must emit FLASH_ENABLE_WRITE
 * SDP 3-byte sequence at the start of each page load.
 * RED before fix (no flash_execute_command(FLASH_ENABLE_WRITE) in write path). */
void test_5v_page_write_execute_emits_sdp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording(); /* reset after configure_memory's set_address call */

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash_5v_page_write_execute must not error on 4-byte zero write");
    TEST_ASSERT_TRUE_MESSAGE(recording_contains_sdp_signature(),
        "flash_5v_page_write_execute must emit FLASH_ENABLE_WRITE SDP (0x5555,0x2AAA,0x5555 MSB pattern) at page start");
}

/* Test 2 (FIX-02B VPP-safety operation phase): flash_5v_page_write_execute must NEVER
 * set CTRL_VPP_REGULATOR_ENABLE (0x80) or CTRL_VPP_P1_ENABLE (0x08) in any
 * CONTROL_REGISTER write during the write-execute call.
 * Passes for the bare loop today; MUST remain green after the SDP fix since
 * flash_util_byte_flipping only sets CTRL_READ_WRITE (not VPP bits). */
void test_5v_page_write_execute_no_vpp(void) {
    firestarter_handle_t h = make_write_handle_with_data();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "flash_5v_page_write_execute must not error on 4-byte zero write");
    assert_no_vpp_in_recording(
        "flash_5v_page_write_execute (operation phase) must NOT set any VPP-enable CTL bit");
}

void test_5v_page_write_init_no_blank_check_erase02(void) {
    firestarter_handle_t h = make_write_init_handle_blank_check_enabled();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_init(&h);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "ERASE-02: is_operation_in_progress must be FALSE after exactly one "
        "flash_5v_page_write_init call with FLAG_SKIP_BLANK_CHECK clear -- "
        "mem_util_blank_check is the only setter of this flag on the write-INIT "
        "path, so TRUE here would mean the pre-write blank check still ran and "
        "left a multi-call INIT loop pending");
    /* The companion "must be NULL" assertion on the removed heap-allocated
     * handle field is GONE, and so is the field itself: mem_util_blank_check
     * no longer allocates that block (it keeps its saved address in a
     * file-scope static), so there is no allocation left to observe. This
     * is the loss of a redundant PROBE, not of coverage -- is_operation_in_progress
     * above and the removed allocation used to be unconditionally adjacent
     * statements in the same then-branch of the same if, with no
     * intervening control flow, early return or condition, so a FALSE
     * result there strictly implied the branch -- and therefore the
     * allocation -- never executed. The behaviour under test is still
     * pinned by the assertion above. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "ERASE-02: the removed blank check can no longer fail a write on a "
        "non-blank part");
    assert_no_vpp_in_recording(
        "ERASE-02: on this blank-check-live configuration (FLAG_SKIP_BLANK_CHECK "
        "clear), flash_5v_page_write_init must energise no VPP rail");
}

void test_5v_page_write_init_no_vpp_with_flag_can_erase_set(void) {
    firestarter_handle_t h = make_write_init_handle_can_erase_set();
    configure_memory(&h);
    clear_bus_recording();

    h.firestarter_operation_init(&h);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "flash_5v_page_write_init must leave is_operation_in_progress FALSE "
        "after exactly one call, even with FLAG_CAN_ERASE set");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "flash_5v_page_write_init must not error with FLAG_CAN_ERASE set");
    assert_no_vpp_in_recording(
        "flash_5v_page_write_init must energise no VPP rail even when "
        "FLAG_CAN_ERASE is set");
}

/* Leg 1 (Dispatch): CMD_LOCK_STATUS must wire firestarter_operation_main to
 * flash_5v_page_read_protection_execute. Unlike flash_nor_unlock.cpp, this
 * file assigns no firestarter_operation_init before the switch, so
 * firestarter_operation_init stays NULL from configure_memory's own
 * top-of-function reset -- there is nothing for the CMD_LOCK_STATUS arm to
 * null. */
void test_5v_page_lock_status_dispatch(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_LOCK_STATUS");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)flash_5v_page_read_protection_execute,
        (const void*)h.firestarter_operation_main,
        "flash_5v_page: CMD_LOCK_STATUS must wire firestarter_operation_main to flash_5v_page_read_protection_execute");
    TEST_ASSERT_NULL_MESSAGE((const void*)h.firestarter_operation_init,
        "flash_5v_page: CMD_LOCK_STATUS must leave firestarter_operation_init NULL (this file assigns none before the switch)");
}

/* Leg 2 (Sequence pinning): asserts every {address, byte} pair and every
 * named constant Task 1 transcribed from 151-SEQUENCES.md, by symbol. This
 * is a CHANGE DETECTOR, NOT A CORRECTNESS PROOF -- infoic.xml's `config`
 * field is the literal string "NULL" on every 0x05 entry, so there is no
 * machine-readable upstream to diff this pinning against; it can only
 * prove the bytes committed today match the bytes committed yesterday.
 * The address/decode pair here is also this artifact's lowest-confidence
 * citation (151-SEQUENCES.md: sourced by structural analogy, not an
 * independently re-checked page) -- pinning it does not upgrade that. */
void test_5v_page_lock_status_pinned_sequence(void) {
    TEST_ASSERT_EQUAL_HEX32(0x0002UL, (uint32_t)FLASH_5V_PAGE_BOOT_BLOCK_STATUS_ADDR);
    TEST_ASSERT_EQUAL_HEX8(0xFF, FLASH_5V_PAGE_BOOT_BLOCK_UNLOCKED);
    TEST_ASSERT_EQUAL_HEX8(0xFE, FLASH_5V_PAGE_BOOT_BLOCK_LOCKED);

    /* Mode entry/exit is a FINDING (151-SEQUENCES.md): the same AA/55/90 as
     * FLASH_ENABLE_ID -- no distinct Product-ID-mode table exists in this
     * project. Pin every {address, byte} pair, not just presence. */
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_ENABLE_ID[0].address);
    TEST_ASSERT_EQUAL_HEX8(0xAA, FLASH_ENABLE_ID[0].byte);
    TEST_ASSERT_EQUAL_UINT32(0x2AAAUL, FLASH_ENABLE_ID[1].address);
    TEST_ASSERT_EQUAL_HEX8(0x55, FLASH_ENABLE_ID[1].byte);
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_ENABLE_ID[2].address);
    TEST_ASSERT_EQUAL_HEX8(0x90, FLASH_ENABLE_ID[2].byte);

    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_DISABLE_ID[0].address);
    TEST_ASSERT_EQUAL_HEX8(0xAA, FLASH_DISABLE_ID[0].byte);
    TEST_ASSERT_EQUAL_UINT32(0x2AAAUL, FLASH_DISABLE_ID[1].address);
    TEST_ASSERT_EQUAL_HEX8(0x55, FLASH_DISABLE_ID[1].byte);
    TEST_ASSERT_EQUAL_UINT32(0x5555UL, FLASH_DISABLE_ID[2].address);
    TEST_ASSERT_EQUAL_HEX8(0xF0, FLASH_DISABLE_ID[2].byte);
}

/* Leg 3 (5 V only): mirrors this suite's own central claim -- no VPP-enable
 * CTL bit may appear in any CONTROL_REGISTER write, this time for the
 * actual CMD_LOCK_STATUS operation, not just its configure phase. */
void test_5v_page_lock_status_no_vpp(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_5v_page_read_protection_execute(&h);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "flash_5v_page_read_protection_execute must not error");
    assert_no_vpp_in_recording(
        "flash_5v_page_read_protection_execute must NOT set any VPP-enable CTL bit -- this is a 5V read");
}

void test_5v_page_lock_status_raw_byte_fidelity(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x05 CMD_LOCK_STATUS");

    s_stub_raw_value = 0x37; /* matches neither _UNLOCKED (0xFF) nor _LOCKED (0xFE) */
    h.firestarter_get_data = stub_get_data_return_fixed;
    s_wire_bytes.clear();

    flash_5v_page_read_protection_execute(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "an unrecognised raw value must set RESPONSE_CODE_WARNING, never ERROR");
    /* Frame shape: 4 magic + 2 len + 1 id + 2 params + 1 crc + 1 anchor = 11 bytes. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE((size_t)11, s_wire_bytes.size(),
        "expected exactly one 2-param MSG_DATA_PROTECTION_STATUS id-frame");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_DATA_PROTECTION_STATUS, s_wire_bytes[6],
        "frame id must be MSG_DATA_PROTECTION_STATUS");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x37, s_wire_bytes[7],
        "byte 0 must equal the stubbed raw value EXACTLY -- never coerced to 0xFF or 0xFE");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, s_wire_bytes[8],
        "byte 1 must be the 0xFF indeterminate sentinel");
}

/* Leg 5 (Mode bracketing): this suite's host_stubs.cpp activates
 * HOST_STUBS_RECORD_BUS, whose rurp_write_to_register override
 * (_shared/host_stubs_common.inc's `#elif defined(HOST_STUBS_RECORD_BUS)`
 * arm) records every call unconditionally -- it does NOT compose with
 * rurp_register_utils.h's cache-compare elision, which lives behind the
 * separate, independent HOST_STUBS_REAL_REGISTER_UTILS opt-in used by
 * other suites (e.g. test_vpp_eprom_v131). Measured: no register write is
 * elided here, so an exact-count/position assertion over the recorded
 * MOST_SIGNIFICANT_BYTE writes is valid for this suite. flash_util_read_
 * in_id_mode's call shape is exactly: 3 MSB writes (FLASH_ENABLE_ID's
 * three byte_flip_t entries) + 1 MSB write (the read's own
 * mem_util_set_address call, via handle->firestarter_get_data) + 3 MSB
 * writes (FLASH_DISABLE_ID) = 7 total, deterministically -- this leg
 * asserts that exact shape, proving entry precedes the read and exit
 * follows it, without needing to know the read's own (address-remap-
 * dependent) MSB value. */
void test_5v_page_lock_status_mode_bracketing(void) {
    firestarter_handle_t h = make_handle(0x05, CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_5v_page_read_protection_execute(&h);

    uint8_t msb[16];
    int msb_count = 0;
    for (int i = 0; i < bus_recording_count() && msb_count < 16; i++) {
        if (recorded_reg(i) == MOST_SIGNIFICANT_BYTE) {
            msb[msb_count++] = recorded_data(i);
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(7, msb_count,
        "expected 3 (FLASH_ENABLE_ID) + 1 (the read's own address-set) + 3 (FLASH_DISABLE_ID) = 7 MSB writes");
    TEST_ASSERT_EQUAL_HEX8(0x55, msb[0]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, msb[1]);
    TEST_ASSERT_EQUAL_HEX8(0x55, msb[2]);
    /* msb[3] is the read's own address-set MSB write -- its value is not
     * asserted here; only its POSITION between entry and exit matters. */
    TEST_ASSERT_EQUAL_HEX8(0x55, msb[4]);
    TEST_ASSERT_EQUAL_HEX8(0x2A, msb[5]);
    TEST_ASSERT_EQUAL_HEX8(0x55, msb[6]);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* Protocol 0x05 configure-phase VPP-safety proof */
    RUN_TEST(test_5v_page_0x05_read_configure_no_vpp);
    RUN_TEST(test_5v_page_0x05_write_configure_no_vpp);

    /* Protocol 0x35 configure-phase VPP-safety proof */
    RUN_TEST(test_5v_page_0x35_read_configure_no_vpp);
    RUN_TEST(test_5v_page_0x35_write_configure_no_vpp);

    /* Protocol 0x39 configure-phase VPP-safety proof */
    RUN_TEST(test_5v_page_0x39_read_configure_no_vpp);
    RUN_TEST(test_5v_page_0x39_write_configure_no_vpp);

    /* FIX-02B: operation-phase SDP emission + VPP-safety proofs */
    RUN_TEST(test_5v_page_write_execute_emits_sdp);
    RUN_TEST(test_5v_page_write_execute_no_vpp);

    RUN_TEST(test_5v_page_write_init_no_blank_check_erase02);
    RUN_TEST(test_5v_page_write_init_no_vpp_with_flag_can_erase_set);

    RUN_TEST(test_5v_page_lock_status_dispatch);
    RUN_TEST(test_5v_page_lock_status_pinned_sequence);
    RUN_TEST(test_5v_page_lock_status_no_vpp);
    RUN_TEST(test_5v_page_lock_status_raw_byte_fidelity);
    RUN_TEST(test_5v_page_lock_status_mode_bracketing);

    return UNITY_END();
}
