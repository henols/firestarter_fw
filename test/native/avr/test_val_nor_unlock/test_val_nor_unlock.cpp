/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Proves configure_flash_nor_unlock is a 5V-only handler (no VPP regulator use).
 * BY SIDE-EFFECT via the recording bus stub:
 *
 *   For CMD_READ and CMD_WRITE (configure-only phase): configure_memory() writes
 *   only address bits to LSB/MSB/CONTROL registers via mem_util_set_address.
 *   configure_flash_nor_unlock sets function pointers but writes no VPP-enable CTL bits.
 *   CTRL_VPP_REGULATOR_ENABLE, CTRL_VPP_P1_ENABLE, and CTRL_VPP_VPE_DROP_ENABLE
 *   must NEVER appear set in any recorded CONTROL_REGISTER write.
 *
 *   This test can go RED if configure_flash_nor_unlock is accidentally wired to a
 *   VPP-enabling configure path (T-71-WIRED-WRONG).
 *
 * Protocol covered: 0x06 (FLASH_AMD_ALT / AMD unlock, sector erase).
 *
 * VPP: NONE — configure_flash_nor_unlock is a 5V AMD-style handler. The nor_unlock
 * erase path uses flash_execute_command which writes data bytes only, not VPP CTL bits.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <vector>
#include <cstdint>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "flash_utils.h"
#include "flash_nor_unlock.h"
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
void test_nor_unlock_read_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_READ");
    assert_no_vpp_in_recording(
        "configure_flash_nor_unlock CMD_READ must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_WRITE must record zero VPP-enable bits */
void test_nor_unlock_write_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_WRITE");
    assert_no_vpp_in_recording(
        "configure_flash_nor_unlock CMD_WRITE must NOT set any VPP-enable CTL bit");
}

/* configure-only: CMD_ERASE must record zero VPP-enable bits */
void test_nor_unlock_erase_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_ERASE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_ERASE");
    assert_no_vpp_in_recording(
        "configure_flash_nor_unlock CMD_ERASE must NOT set any VPP-enable CTL bit");
}

/* Phase 204 (FWCMD-01, Fork D): the CMD_BLANK_CHECK no-VPP case that used
 * to live here is deleted, not re-keyed. It asserted that configuring the
 * standalone blank-check command engaged no VPP; after this release that
 * ordinal reaches no configure handler at all (the arm is gone from
 * configure_flash_nor_unlock's switch), so the same claim would be true
 * for a reason unrelated to what this case was written to check. The
 * other three cases above already cover configure-only no-VPP for this
 * handler's surviving ordinals. */

/* Leg 1 (Dispatch): CMD_LOCK_STATUS must wire firestarter_operation_main to
 * flash_nor_unlock_read_protection_execute and null firestarter_operation_init
 * -- this file assigns flash_nor_unlock_generic_init before the switch, so
 * the CMD_LOCK_STATUS arm has to null it explicitly (unlike flash_5v_page,
 * which never assigns one). */
void test_nor_unlock_lock_status_dispatch(void) {
    firestarter_handle_t h = make_handle(CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_LOCK_STATUS");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)flash_nor_unlock_read_protection_execute,
        (const void*)h.firestarter_operation_main,
        "flash_nor_unlock: CMD_LOCK_STATUS must wire firestarter_operation_main to flash_nor_unlock_read_protection_execute");
    TEST_ASSERT_NULL_MESSAGE((const void*)h.firestarter_operation_init,
        "flash_nor_unlock: CMD_LOCK_STATUS must null firestarter_operation_init (this file assigns one before the switch)");
}

/* Leg 2 (Sequence pinning): asserts every {address, byte} pair and every
 * named constant Task 1 transcribed from 151-SEQUENCES.md, by symbol. This
 * is a CHANGE DETECTOR, NOT A CORRECTNESS PROOF -- infoic.xml's `config`
 * field is the literal string "NULL" on every 0x06 entry, so there is no
 * machine-readable upstream to diff this pinning against; it can only
 * prove the bytes committed today match the bytes committed yesterday. */
void test_nor_unlock_lock_status_pinned_sequence(void) {
    TEST_ASSERT_EQUAL_HEX32(0x0002UL, (uint32_t)FLASH_NOR_UNLOCK_PROTECT_VERIFY_ADDR);
    TEST_ASSERT_EQUAL_HEX8(0x00, FLASH_NOR_UNLOCK_PROTECT_UNPROTECTED);
    TEST_ASSERT_EQUAL_HEX8(0x01, FLASH_NOR_UNLOCK_PROTECT_PROTECTED);

    /* Mode entry/exit reuse FLASH_ENABLE_ID / FLASH_DISABLE_ID verbatim
     * (151-SEQUENCES.md: byte-identical, zero new bytes) -- pin every
     * {address, byte} pair, not just presence. */
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
void test_nor_unlock_lock_status_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_nor_unlock_read_protection_execute(&h);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "flash_nor_unlock_read_protection_execute must not error");
    assert_no_vpp_in_recording(
        "flash_nor_unlock_read_protection_execute must NOT set any VPP-enable CTL bit -- this is a 5V read");
}

void test_nor_unlock_lock_status_raw_byte_fidelity(void) {
    firestarter_handle_t h = make_handle(CMD_LOCK_STATUS);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x06 CMD_LOCK_STATUS");

    s_stub_raw_value = 0x37; /* matches neither _UNPROTECTED (0x00) nor _PROTECTED (0x01) */
    h.firestarter_get_data = stub_get_data_return_fixed;
    s_wire_bytes.clear();

    flash_nor_unlock_read_protection_execute(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "an unrecognised raw value must set RESPONSE_CODE_WARNING, never ERROR");
    /* Frame shape: 4 magic + 2 len + 1 id + 2 params + 1 crc + 1 anchor = 11 bytes. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE((size_t)11, s_wire_bytes.size(),
        "expected exactly one 2-param MSG_DATA_PROTECTION_STATUS id-frame");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_DATA_PROTECTION_STATUS, s_wire_bytes[6],
        "frame id must be MSG_DATA_PROTECTION_STATUS");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x37, s_wire_bytes[7],
        "byte 0 must equal the stubbed raw value EXACTLY -- never coerced to 0x00 or 0x01");
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
void test_nor_unlock_lock_status_mode_bracketing(void) {
    firestarter_handle_t h = make_handle(CMD_LOCK_STATUS);
    configure_memory(&h);
    clear_bus_recording();

    flash_nor_unlock_read_protection_execute(&h);

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

    /* 5V-only proof: no VPP-enable CTL bit for any command in the configure phase */
    RUN_TEST(test_nor_unlock_read_configure_no_vpp);
    RUN_TEST(test_nor_unlock_write_configure_no_vpp);
    RUN_TEST(test_nor_unlock_erase_configure_no_vpp);

    RUN_TEST(test_nor_unlock_lock_status_dispatch);
    RUN_TEST(test_nor_unlock_lock_status_pinned_sequence);
    RUN_TEST(test_nor_unlock_lock_status_no_vpp);
    RUN_TEST(test_nor_unlock_lock_status_raw_byte_fidelity);
    RUN_TEST(test_nor_unlock_lock_status_mode_bracketing);

    return UNITY_END();
}
