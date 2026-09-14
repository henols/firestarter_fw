/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
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
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"
#include "../_shared/sdp_bus_config.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

#define EEPROM28C_PLANTED_SENTINEL 0xFFFFFFFFUL
static uint32_t s_planted_base_address;
static uint32_t s_planted_stale_address;
static uint8_t  s_planted_stale_value;

static uint32_t s_get_data_calls;

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
    clear_bus_recording();

    s_planted_base_address = 0;
    s_planted_stale_address = EEPROM28C_PLANTED_SENTINEL;
    s_planted_stale_value = 0;
    s_get_data_calls = 0;
}

void tearDown(void) {}

/* Address-keyed planted get_data mock. Returns the planted stale value when
 * the queried address equals s_planted_stale_address (unless that field
 * still holds the sentinel, meaning "nothing planted" -- the isolation
 * control's clean-source case); otherwise returns the byte the write
 * intended for that address, derived from s_planted_base_address and the
 * same 0x10 + k pattern make_write_handle() below fills data_buffer with.
 * Dispatch is on ADDRESS only, never on call order. */
static uint8_t mock_get_data_planted(firestarter_handle_t*, uint32_t address) {
    s_get_data_calls++;
    if (s_planted_stale_address != EEPROM28C_PLANTED_SENTINEL && address == s_planted_stale_address) {
        return s_planted_stale_value;
    }
    return (uint8_t)(0x10 + (address - s_planted_base_address));
}

/* Write-path handle factory. Row 0 of SDP_BUS_CONFIGS is AT28C256 /
 * DIP28_28C256, whose mem_size (32768) matches the existing make_handle()
 * above; its address_mask (0x0000BFFF) does not disturb any address used by
 * the cases below. data_buffer[k] is filled with 0x10 + k so no written
 * byte is 0xFF -- the planted stale value (0xFF) is unambiguously
 * distinguishable from a correctly-written one. */
static firestarter_handle_t make_write_handle(uint32_t address, uint32_t data_size) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = SDP_BUS_CONFIGS[0].mem_size;
    h.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h.address = address;
    h.data_size = data_size;
    for (uint32_t k = 0; k < data_size; k++) {
        h.data_buffer[k] = (char)(0x10 + k);
    }
    return h;
}

static bool legacy_last_byte_equality_poll(firestarter_handle_t* h, uint32_t address, uint8_t expected) {
    for (uint16_t j = 0; j < 2000; j++) {
        delayMicroseconds(10);
        uint8_t observed = h->firestarter_get_data(h, address);
        if (observed == expected) {
            return true;
        }
    }
    return false;
}

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

void test_eeprom28c_erase_configure_no_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_ERASE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x0D CMD_ERASE");
    assert_no_vpp_in_recording(
        "configure_eeprom28c CMD_ERASE must NOT set any VPP-enable CTL bit");
}

/* The side-by-side contrast, both halves in one test function. Geometry:
 * base address 0, data_size 8 (AT28C_PAGE_SIZE_FALLBACK 64, so this is one flush on
 * last_byte). Plant a stale 0xFF at address 0x0002 -- an EARLIER byte, not
 * the last -- so the page's last byte always reads back correctly, the
 * DQ7-complement completion arm reports done, and only the read-back can
 * catch the earlier stale byte. HOST_STUBS_RECORD_BUS caps recording at 256
 * entries; an 8-byte write stays far below it. */
void test_fix06_planted_partial_write_fails_fixed_path_and_passes_legacy_poll(void) {
    s_planted_base_address = 0;
    s_planted_stale_address = 0x0002;
    s_planted_stale_value = 0xFF;

    firestarter_handle_t h = make_write_handle(0, 8);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FIX-06 fixed path must report ERROR: the planted stale byte at "
        "address 0x0002 never landed, even though the page's last byte did");

    /* Same planted mock, same page's last byte (address 7). The deleted
     * whole-byte equality poll only ever looked at THIS address, so it
     * reports success for the very same partial write the fixed path
     * above correctly rejects. */
    bool legacy_would_have_reported_success =
        legacy_last_byte_equality_poll(&h, 7, (uint8_t)(0x10 + 7));
    TEST_ASSERT_TRUE_MESSAGE(legacy_would_have_reported_success,
        "the deleted last-byte-equality poll would have reported SUCCESS "
        "for the exact same partial write the fixed path above rejects "
        "(FIX-06's conflation, gh#11's shape)");
}

void test_fix06_clean_page_write_succeeds_isolation_control(void) {
    s_planted_base_address = 0;
    s_planted_stale_address = EEPROM28C_PLANTED_SENTINEL;
    s_planted_stale_value = 0;

    firestarter_handle_t h = make_write_handle(0, 8);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "a clean page write with nothing planted must not report ERROR -- "
        "proves the planted case's ERROR is caused by the plant, not the seam");
}

/* Page-boundary window reset. Geometry: base address 56, data_size 16, so
 * with AT28C_PAGE_SIZE_FALLBACK 64 the write flushes twice -- once at address 63 on
 * page_end (buffer window 0..7, addresses 56..63) and once at the last
 * byte (window 8..15, addresses 64..71). Driven three times with a fresh
 * handle and freshly reset mock state each time. */
void test_fix06_page_boundary_window_readback(void) {
    /* Drive 1: plant a stale byte inside the FIRST window (address 59). */
    s_planted_base_address = 56;
    s_planted_stale_address = 59;
    s_planted_stale_value = 0xFF;
    {
        firestarter_handle_t h = make_write_handle(56, 16);
        configure_memory(&h);
        h.firestarter_get_data = mock_get_data_planted;
        h.firestarter_operation_main(&h);
        TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "a stale byte inside the first flush window (address 59) must be caught");
    }

    /* Drive 2: plant a stale byte inside the SECOND window (address 65) --
     * this is the assertion that fails if the window-start index does not
     * advance with the flush. */
    s_planted_base_address = 56;
    s_planted_stale_address = 65;
    s_planted_stale_value = 0xFF;
    {
        firestarter_handle_t h = make_write_handle(56, 16);
        configure_memory(&h);
        h.firestarter_get_data = mock_get_data_planted;
        h.firestarter_operation_main(&h);
        TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "a stale byte inside the second flush window (address 65) must be caught");
    }

    /* Drive 3: plant nothing, so the two-window geometry itself is shown
     * not to be the cause of the ERROR asserted above. */
    s_planted_base_address = 56;
    s_planted_stale_address = EEPROM28C_PLANTED_SENTINEL;
    s_planted_stale_value = 0;
    {
        firestarter_handle_t h = make_write_handle(56, 16);
        configure_memory(&h);
        h.firestarter_get_data = mock_get_data_planted;
        h.firestarter_operation_main(&h);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
            "a clean two-window write must not report ERROR");
    }
}

/*
 * Geometry, every case below: make_write_handle(0, 128) -- base 0,
 * data_size 128, planted base address 0 and the stale-address sentinel
 * cleared, so the write is CLEAN. The arithmetic, derived from the two
 * production readers: eeprom28c_wait_for_page_write does a DOUBLE read per
 * flush on a clean poll and eeprom28c_verify_page_readback does exactly one
 * read per buffer byte over disjoint windows, so for a clean write
 * s_get_data_calls == 2 * flushes + data_size. Page 64 -> 2 flushes -> 132;
 * page 128 -> 1 flush -> 130. Criterion 1's OBSERVATION that a delivered
 * 128 changes flush cadence rests on these two counts differing.
 *
 * Every case drives configure_memory(&h) then h.firestarter_operation_main(&h)
 * -- never firestarter_operation_init -- so the mask, resolved in
 * eeprom28c_write_execute itself, is reached the same way every existing
 * case in this suite already is. */

void test_pgsz_absent_field_reproduces_the_64_byte_cadence(void) {
    firestarter_handle_t h = make_write_handle(0, 128);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a clean 128-byte write with no page-size delivered must not report ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(132, s_get_data_calls,
        "absent page-size must reproduce the 64-byte flush cadence (2 flushes -> 132 calls)");
}

void test_pgsz_delivered_128_halves_the_flush_count(void) {
    firestarter_handle_t h = make_write_handle(0, 128);
    h.page_size = 128;
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a clean 128-byte write at a delivered 128-byte page must not report ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(130, s_get_data_calls,
        "a delivered 128 must be OBSERVED to halve the flush count (1 flush -> 130 calls)");
}

void test_pgsz_explicit_64_matches_the_absent_cadence(void) {
    firestarter_handle_t h = make_write_handle(0, 128);
    h.page_size = 64;
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a clean 128-byte write at an explicit 64-byte page must not report ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(132, s_get_data_calls,
        "an explicit 64 must agree with the absent-field fallback cadence (D-03)");
}

void test_pgsz_non_power_of_two_falls_back_silently(void) {
    firestarter_handle_t h = make_write_handle(0, 128);
    h.page_size = 96;
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a clean 128-byte write at a non-power-of-two page-size must not report ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(132, s_get_data_calls,
        "a non-power-of-two page-size (96) must fall back to the 64-byte cadence silently");
}

void test_pgsz_out_of_range_falls_back_silently(void) {
    firestarter_handle_t h = make_write_handle(0, 128);
    h.page_size = 2048;
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_planted;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "a clean 128-byte write at an out-of-range page-size must not report ERROR");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(132, s_get_data_calls,
        "an out-of-range page-size (2048, above AT28C_PAGE_SIZE_MAX) must fall back to the "
        "64-byte cadence silently");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* 5V-only proof: no VPP-enable CTL bit for any command in the configure phase */
    RUN_TEST(test_eeprom28c_read_configure_no_vpp);
    RUN_TEST(test_eeprom28c_write_configure_no_vpp);
    RUN_TEST(test_eeprom28c_blank_check_configure_no_vpp);
    RUN_TEST(test_eeprom28c_erase_configure_no_vpp);

    RUN_TEST(test_fix06_planted_partial_write_fails_fixed_path_and_passes_legacy_poll);
    RUN_TEST(test_fix06_clean_page_write_succeeds_isolation_control);
    RUN_TEST(test_fix06_page_boundary_window_readback);

    RUN_TEST(test_pgsz_absent_field_reproduces_the_64_byte_cadence);
    RUN_TEST(test_pgsz_delivered_128_halves_the_flush_count);
    RUN_TEST(test_pgsz_explicit_64_matches_the_absent_cadence);
    RUN_TEST(test_pgsz_non_power_of_two_falls_back_silently);
    RUN_TEST(test_pgsz_out_of_range_falls_back_silently);

    return UNITY_END();
}
