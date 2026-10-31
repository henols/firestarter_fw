/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Tests the 0x0D family through the recording bus stub: configure_memory
 * sends 0x0D to configure_eeprom28c (the operation pointers must be that
 * function's own), and the page write verifies every byte it wrote.
 *
 * Protocol covered: 0x0D (EEPROM_POLL / AT28C-series).
 *
 * VPP: NONE — configure_eeprom28c is a 5V page-write handler. The A9-12V chip-ID
 * path in eeprom28c_check_chip_id is gated by handle->chip_id > 0; we set chip_id=0
 * so that branch is never reached.
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

#include "../_shared/bus_recording.h"
#include "../_shared/dispatch_expect.h"
#include "eeprom_28c.h"

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

static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x0D;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id    = 0; /* skip chip-id branch */
    h.mem_size   = 32768; /* 32 KB (AT28C256) */
    return h;
}

/* configure_memory must send 0x0D to configure_eeprom28c: the operation
 * pointers must be that function's own. */
void test_eeprom28c_read_dispatches_to_eeprom28c(void) {
    assert_dispatches_to(make_handle(CMD_READ), configure_eeprom28c, "0x0D CMD_READ");
}

void test_eeprom28c_write_dispatches_to_eeprom28c(void) {
    assert_dispatches_to(make_handle(CMD_WRITE), configure_eeprom28c, "0x0D CMD_WRITE");
}

void test_eeprom28c_erase_dispatches_to_eeprom28c(void) {
    assert_dispatches_to(make_handle(CMD_ERASE), configure_eeprom28c, "0x0D CMD_ERASE");
}

/* Geometry: base address 0, data_size 8 (AT28C_PAGE_SIZE_FALLBACK 64, so this
 * is one flush on last_byte). Plant a stale 0xFF at address 0x0002 -- an
 * EARLIER byte, not the last -- so the page's last byte always reads back
 * correctly, the DQ7-complement completion arm reports done, and only the
 * read-back can catch the earlier stale byte. */
void test_fix06_planted_partial_write_fails(void) {
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
    RUN_TEST(test_eeprom28c_read_dispatches_to_eeprom28c);
    RUN_TEST(test_eeprom28c_write_dispatches_to_eeprom28c);
    RUN_TEST(test_eeprom28c_erase_dispatches_to_eeprom28c);

    RUN_TEST(test_fix06_planted_partial_write_fails);
    RUN_TEST(test_fix06_clean_page_write_succeeds_isolation_control);
    RUN_TEST(test_fix06_page_boundary_window_readback);

    RUN_TEST(test_pgsz_absent_field_reproduces_the_64_byte_cadence);
    RUN_TEST(test_pgsz_delivered_128_halves_the_flush_count);
    RUN_TEST(test_pgsz_explicit_64_matches_the_absent_cadence);
    RUN_TEST(test_pgsz_non_power_of_two_falls_back_silently);
    RUN_TEST(test_pgsz_out_of_range_falls_back_silently);

    return UNITY_END();
}
