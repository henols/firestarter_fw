/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "memory.h"
#include "eeprom_28c.h"
}
#include "firestarter.h"
#include "flash_utils.h"

extern const byte_flip_t EEPROM_SDP_DISABLE[6];

extern const byte_flip_t EEPROM_SDP_ENABLE[3];

#include "../_shared/sdp_bus_config.h"
#include "../_shared/sdp_expected.h"

using namespace fakeit;

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);

/* ─────────────────────────────────────────────────────────────────────────
 * setUp / tearDown
 * ───────────────────────────────────────────────────────────────────────── */

static uint32_t s_mfr_addr_keyed;
static uint8_t  s_mfr_hi_keyed;
static uint8_t  s_mfr_lo_keyed;
static int      s_reads_at_mfr_addr;
static int      s_reads_at_poll_addr;

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);

    clear_strobes();
    reset_register_cache(0x00, 0x00, 0x00);

    s_mfr_addr_keyed = 0;
    s_mfr_hi_keyed = 0xFF;
    s_mfr_lo_keyed = 0xFF;
    s_reads_at_mfr_addr = 0;
    s_reads_at_poll_addr = 0;
}

void tearDown(void) {}

/* ─────────────────────────────────────────────────────────────────────────
 * Handle + drive helpers
 * ───────────────────────────────────────────────────────────────────────── */

static firestarter_handle_t make_sdp_handle(const sdp_bus_config_row_t& row) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = row.mem_size;
    h.bus_config = row.bus_config;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK;
    return h;
}

/* Load-bearing order: configure_memory (which itself writes address 0) ->
 * reset_register_cache -> clear_strobes -> flash_util_byte_flipping. The
 * cache reset and strobe clear MUST both come after configure_memory
 * (test_val_5v_page.cpp:150-175 records this same hazard). ctrl_seed=0x00 is
 * what makes the 54-entry SHIPPED array correct: flash_util_byte_flipping's
 * two CTRL_READ_WRITE clears then produce no CONTROL entry because the
 * cached value is already 0x00 (unchanged). */
static void drive(firestarter_handle_t* h, const byte_flip_t* table, size_t len, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    flash_util_byte_flipping(h, table, len);
}

static void drive_reference_emitter(firestarter_handle_t* h, const byte_flip_t* table, size_t len, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    for (size_t i = 0; i < len; i++) {
        h->firestarter_set_data(h, table[i].address, table[i].byte);
    }
}

static bool sdp_tables_identical(const byte_flip_t* a, const byte_flip_t* b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (a[i].address != b[i].address || a[i].byte != b[i].byte) {
            return false;
        }
    }
    return true;
}

#ifdef SDP_TRACE_DUMP
static void dump_strobes(const char* tag) {
    printf("##### %s total=%d overflow=%d\n", tag, strobe_count(), strobe_overflowed());
    for (int i = 0; i < strobe_count(); i++) {
        if (strobe_kind(i) == STROBE_KIND_DATA) {
            printf("[%3d] DATA 0x%02X\n", i, strobe_value(i));
        } else {
            printf("[%3d] PIN  0x%02X -> %d\n", i, strobe_pin(i), strobe_value(i));
        }
    }
}
#endif

void test_case1_ordered_capture_dip28_28c256(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 / DIP28_28C256 */
    drive(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
#ifdef SDP_TRACE_DUMP
    dump_strobes("case1_dip28_28c256_shipped");
#endif
    sdp_assert_stream_equals(SDP_SHIPPED_DIP28_28C256, SDP_SHIPPED_DIP28_28C256_LEN,
        "Case 1: ordered capture, DIP28_28C256, FLASH_DISABLE_WRITE_PROTECTION (== EEPROM_SDP_DISABLE, 116-03 parity)");
}

void test_case2_elision_is_real(void) {
    /* A raw call-log golden would assert 6 phantom entries here (Pitfall 4) —
     * write #4 targets the same address as write #3, so LSB/MSB are cache-
     * elided and only the payload DATA + OE + CE edges remain. */
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 2: stream must not have overflowed");
    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_DATA, strobe_kind(30),
        "Case 2: index 30 must be a DATA entry -- write #4 emits no address latch (cache hit)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAA, strobe_value(30),
        "Case 2: index 30's payload must be 0xAA -- write #4's data byte");
}

void test_case3_ce_oe_edges_distinguishable(void) {
    /* Positional indices from the expected array (write #1's OE/CE triple),
     * not a scan -- distinguishes OE from CE by the `pin` field. */
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_PIN, strobe_kind(7), "Case 3: index 7 must be a PIN entry (/OE)");
    TEST_ASSERT_EQUAL_MESSAGE(OUTPUT_ENABLE, strobe_pin(7), "Case 3: index 7 must be OUTPUT_ENABLE");
    TEST_ASSERT_EQUAL_MESSAGE(1, strobe_value(7), "Case 3: /OE asserts high (rurp_chip_output enables output)");

    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_PIN, strobe_kind(8), "Case 3: index 8 must be a PIN entry (/CE low)");
    TEST_ASSERT_EQUAL_MESSAGE(CHIP_ENABLE, strobe_pin(8), "Case 3: index 8 must be CHIP_ENABLE");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_value(8), "Case 3: /CE asserts low (rurp_chip_enable)");

    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_PIN, strobe_kind(9), "Case 3: index 9 must be a PIN entry (/CE high)");
    TEST_ASSERT_EQUAL_MESSAGE(CHIP_ENABLE, strobe_pin(9), "Case 3: index 9 must be CHIP_ENABLE");
    TEST_ASSERT_EQUAL_MESSAGE(1, strobe_value(9), "Case 3: /CE deasserts high (rurp_chip_disable)");
}

static const byte_flip_t TEST_UNLOCK_MUTATED_TERMINAL[] = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x80},
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x10}, /* mutated: 0x20 (SDP-disable) -> 0x10 (chip-erase) */
};

void test_negativeA_unlock_mutated_diverges_and_matches_erase(void) {
    firestarter_handle_t h1 = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h1, TEST_UNLOCK_MUTATED_TERMINAL, 6, 0x00);

    int div = sdp_first_divergence(SDP_SHIPPED_DIP28_28C256, SDP_SHIPPED_DIP28_28C256_LEN);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, div,
        "Negative A (i): mutated-terminal-byte stream must diverge from the shipped SDP-disable stream");
    TEST_ASSERT_EQUAL_MESSAGE(50, div,
        "Negative A (i): divergence must be at index 50 -- write #6's payload byte (0x10 vs 0x20)");

    sdp_strobe_t mutated_snapshot[64];
    int mutated_len = sdp_snapshot(mutated_snapshot, 64);

    firestarter_handle_t h2 = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h2, FLASH_ERASE, sizeof(FLASH_ERASE) / sizeof(FLASH_ERASE[0]), 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(-1, sdp_first_divergence(mutated_snapshot, mutated_len),
        "Negative A (ii): mutated-unlock stream must be element-wise IDENTICAL to the real FLASH_ERASE stream");
    sdp_assert_stream_equals(mutated_snapshot, mutated_len,
        "Negative A (ii): mutated-unlock == real FLASH_ERASE (one-nibble hazard, machine-visible)");
}

/* TRACE-03b: drive the three-write FLASH_ENABLE_WRITE_PROTECTION (lock /
 * write-prefix) table where the six-write unlock stream is expected. */
void test_negativeB_lock_table_swapped_for_write_prefix(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h, FLASH_ENABLE_WRITE_PROTECTION,
        sizeof(FLASH_ENABLE_WRITE_PROTECTION) / sizeof(FLASH_ENABLE_WRITE_PROTECTION[0]), 0x00);

    int div = sdp_first_divergence(SDP_SHIPPED_DIP28_28C256, SDP_SHIPPED_DIP28_28C256_LEN);
    TEST_ASSERT_EQUAL_MESSAGE(26, div,
        "Negative B: three-write lock/write-prefix table must diverge from the six-write unlock "
        "stream at index 26 -- write #3's payload byte (0xA0 vs 0x80)");
}

void test_lock05_enable_write_and_write_protection_identical(void) {
    firestarter_handle_t h1 = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h1, FLASH_ENABLE_WRITE_PROTECTION,
        sizeof(FLASH_ENABLE_WRITE_PROTECTION) / sizeof(FLASH_ENABLE_WRITE_PROTECTION[0]), 0x00);
    sdp_strobe_t snap[32];
    int len = sdp_snapshot(snap, 32);

    firestarter_handle_t h2 = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive(&h2, FLASH_ENABLE_WRITE, sizeof(FLASH_ENABLE_WRITE) / sizeof(FLASH_ENABLE_WRITE[0]), 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(-1, sdp_first_divergence(snap, len),
        "LOCK-05: FLASH_ENABLE_WRITE_PROTECTION and FLASH_ENABLE_WRITE are byte-identical tables and "
        "must therefore produce element-wise identical streams");
    sdp_assert_stream_equals(snap, len, "LOCK-05: identity between the two 3-write tables");
}

/*
 * Two separately-messaged legs, not one transitive check, so a failure
 * names which pair diverged. All three entries' address/byte pairs are
 * also pinned directly, including the terminal {0x5555, 0xA0} -- three
 * writes fully pinned, mirroring test_fix05_terminal_byte_and_table_identity_guards'
 * six-write pinning for the unlock table above. */
void test_lock05_three_way_enable_table_identity(void) {
    TEST_ASSERT_TRUE_MESSAGE(
        sdp_tables_identical(EEPROM_SDP_ENABLE, FLASH_ENABLE_WRITE_PROTECTION, 3),
        "LOCK-05: EEPROM_SDP_ENABLE must be byte-identical to FLASH_ENABLE_WRITE_PROTECTION -- "
        "AA-55-A0 is datasheet-dual-purpose (Atmel doc0270 section 19 note 2) and this "
        "duplication must stay PRESERVED, not deduped");
    TEST_ASSERT_TRUE_MESSAGE(
        sdp_tables_identical(EEPROM_SDP_ENABLE, FLASH_ENABLE_WRITE, 3),
        "LOCK-05: EEPROM_SDP_ENABLE must ALSO be byte-identical to FLASH_ENABLE_WRITE -- the SAME "
        "three writes prefix any protected byte write; the name is the only discriminator");

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x5555, EEPROM_SDP_ENABLE[0].address,
        "LOCK-05: EEPROM_SDP_ENABLE entry 0 address must be 0x5555");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAA, EEPROM_SDP_ENABLE[0].byte,
        "LOCK-05: EEPROM_SDP_ENABLE entry 0 byte must be 0xAA");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x2AAA, EEPROM_SDP_ENABLE[1].address,
        "LOCK-05: EEPROM_SDP_ENABLE entry 1 address must be 0x2AAA");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x55, EEPROM_SDP_ENABLE[1].byte,
        "LOCK-05: EEPROM_SDP_ENABLE entry 1 byte must be 0x55");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x5555, EEPROM_SDP_ENABLE[2].address,
        "LOCK-05: EEPROM_SDP_ENABLE terminal entry (2) address must be 0x5555");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA0, EEPROM_SDP_ENABLE[2].byte,
        "LOCK-05: EEPROM_SDP_ENABLE terminal entry (2) byte must be 0xA0 -- if this fails, the "
        "SDP-enable body no longer matches the write-protect prefix");
}

/*
 * Do NOT add a trace-based negative between FLASH_ENABLE_WRITE_PROTECTION
 * and FLASH_ENABLE_WRITE here --
 * test_lock05_enable_write_and_write_protection_identical's comment above
 * states this is impossible by construction; a "negative" between two
 * tables already asserted identical would be a test that cannot fail. */
void test_lock05_enable_table_objects_distinct(void) {
    TEST_ASSERT_NOT_EQUAL_MESSAGE((const void*)EEPROM_SDP_ENABLE, (const void*)FLASH_ENABLE_WRITE_PROTECTION,
        "LOCK-05 distinctness: EEPROM_SDP_ENABLE and FLASH_ENABLE_WRITE_PROTECTION must be distinct "
        "array objects, not aliases");
    TEST_ASSERT_NOT_EQUAL_MESSAGE((const void*)EEPROM_SDP_ENABLE, (const void*)FLASH_ENABLE_WRITE,
        "LOCK-05 distinctness: EEPROM_SDP_ENABLE and FLASH_ENABLE_WRITE must be distinct array "
        "objects, not aliases");
    TEST_ASSERT_NOT_EQUAL_MESSAGE((const void*)FLASH_ENABLE_WRITE_PROTECTION, (const void*)FLASH_ENABLE_WRITE,
        "LOCK-05 distinctness: FLASH_ENABLE_WRITE_PROTECTION and FLASH_ENABLE_WRITE must be distinct "
        "array objects, not aliases");

    TEST_ASSERT_NOT_EQUAL_MESSAGE((const void*)EEPROM_SDP_ENABLE, (const void*)EEPROM_SDP_DISABLE,
        "LOCK-05 distinctness: EEPROM_SDP_ENABLE and EEPROM_SDP_DISABLE must be distinct array objects");
    TEST_ASSERT_EQUAL_MESSAGE(3, sizeof(EEPROM_SDP_ENABLE) / sizeof(EEPROM_SDP_ENABLE[0]),
        "LOCK-05: EEPROM_SDP_ENABLE must be a 3-element table");
    TEST_ASSERT_EQUAL_MESSAGE(6, sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]),
        "LOCK-05: EEPROM_SDP_DISABLE must be a 6-element table -- lock and unlock lengths differ (3 "
        "vs 6), complementing Plan 119-05's stream-level lock-vs-unlock divergence assertions");
}

void test_fix05_terminal_byte_and_table_identity_guards(void) {
    /* (1) Length sanity -- every table this guard reasons about is 6
     * elements; a length surprise would silently invalidate every index
     * below. */
    TEST_ASSERT_EQUAL_MESSAGE(6, sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]),
        "FIX-05: EEPROM_SDP_DISABLE must be a 6-element table");
    TEST_ASSERT_EQUAL_MESSAGE(6, sizeof(FLASH_ERASE) / sizeof(FLASH_ERASE[0]),
        "FIX-05: FLASH_ERASE must be a 6-element table");
    TEST_ASSERT_EQUAL_MESSAGE(6, sizeof(FLASH_DISABLE_WRITE_PROTECTION) / sizeof(FLASH_DISABLE_WRITE_PROTECTION[0]),
        "FIX-05: FLASH_DISABLE_WRITE_PROTECTION must be a 6-element table");

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x20, EEPROM_SDP_DISABLE[5].byte,
        "FIX-05: EEPROM_SDP_DISABLE's terminal byte must be 0x20 (SDP-disable) -- if this fails, "
        "the production 0x0D write path may now emit a chip-erase command");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x5555, EEPROM_SDP_DISABLE[5].address,
        "FIX-05: EEPROM_SDP_DISABLE's terminal address must be 0x5555");

    /* (3) FLASH_ERASE's terminal byte, the chip-erase value this guard
     * exists to distinguish EEPROM_SDP_DISABLE from. */
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x10, FLASH_ERASE[5].byte,
        "FIX-05: FLASH_ERASE's terminal byte must be 0x10 (chip-erase)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0x5555, FLASH_ERASE[5].address,
        "FIX-05: FLASH_ERASE's terminal address must be 0x5555");

    /* (4) Distinctness: the two terminal bytes must differ, AND the two
     * arrays must be distinct OBJECTS -- a future refactor that made
     * EEPROM_SDP_DISABLE an alias of FLASH_ERASE (or vice versa) would pass
     * every value-level assertion above while erasing every chip on write. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(FLASH_ERASE[5].byte, EEPROM_SDP_DISABLE[5].byte,
        "FIX-05: FLASH_ERASE and EEPROM_SDP_DISABLE terminal bytes must differ");
    TEST_ASSERT_NOT_EQUAL_MESSAGE((const void*)EEPROM_SDP_DISABLE, (const void*)FLASH_ERASE,
        "FIX-05: EEPROM_SDP_DISABLE and FLASH_ERASE must be distinct array objects, not aliases");

    /* (5) The one-nibble claim, made literal: elements 0-4 match on BOTH
     * address and byte; element 5's address matches while its byte differs.
     * So the two tables differ at exactly one field of exactly one element. */
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(FLASH_ERASE[i].address, EEPROM_SDP_DISABLE[i].address,
            "FIX-05: elements 0-4 must match address between FLASH_ERASE and EEPROM_SDP_DISABLE");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(FLASH_ERASE[i].byte, EEPROM_SDP_DISABLE[i].byte,
            "FIX-05: elements 0-4 must match byte between FLASH_ERASE and EEPROM_SDP_DISABLE");
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(FLASH_ERASE[5].address, EEPROM_SDP_DISABLE[5].address,
        "FIX-05: element 5's address must still match -- only the byte differs");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(FLASH_ERASE[5].byte, EEPROM_SDP_DISABLE[5].byte,
        "FIX-05: element 5's byte must differ -- the one-nibble hazard, machine-checked");

    TEST_ASSERT_TRUE_MESSAGE(sdp_tables_identical(EEPROM_SDP_DISABLE, FLASH_DISABLE_WRITE_PROTECTION, 6),
        "FIX-05/D-11: EEPROM_SDP_DISABLE must stay byte-identical to FLASH_DISABLE_WRITE_PROTECTION -- "
        "the production 0x0D-local table has diverged from the table this harness's goldens are driven "
        "from, and D-10's deliberate duplication is no longer safe");
}

void test_fix05_guard_rejects_planted_terminal_mutation(void) {
    TEST_ASSERT_FALSE_MESSAGE(sdp_tables_identical(TEST_UNLOCK_MUTATED_TERMINAL, EEPROM_SDP_DISABLE, 6),
        "FIX-05 anti-hollow: sdp_tables_identical must REJECT the planted terminal-byte mutation -- "
        "if this passes, the constant-level guard's D-11 cross-check clause is hollow");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TEST_UNLOCK_MUTATED_TERMINAL[5].address, EEPROM_SDP_DISABLE[5].address,
        "FIX-05 anti-hollow: the planted mutation must match address at element 5 -- only the byte differs");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(TEST_UNLOCK_MUTATED_TERMINAL[5].byte, EEPROM_SDP_DISABLE[5].byte,
        "FIX-05 anti-hollow: the planted mutation's element-5 byte must differ from EEPROM_SDP_DISABLE's");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(FLASH_ERASE[5].byte, TEST_UNLOCK_MUTATED_TERMINAL[5].byte,
        "FIX-05 anti-hollow: the planted one-nibble slip must turn the SDP-disable table into exactly "
        "the FLASH_ERASE terminal byte -- the constant-level twin of test_negativeA_...'s stream-level proof");
}

void test_fixed_guard_at28c256(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_reference_emitter(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN,
        "reference-emitter guard: AT28C256 / DIP28_28C256");
}

void test_fixed_guard_at28c64(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[1]); /* AT28C64 */
    drive_reference_emitter(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C64, SDP_FIXED_DIP28_28C64_LEN,
        "reference-emitter guard: AT28C64 / DIP28_28C64");
}

void test_fixed_guard_at28c16(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[2]); /* AT28C16 */
    drive_reference_emitter(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP24_2816, SDP_FIXED_DIP24_2816_LEN,
        "reference-emitter guard: AT28C16 / DIP24_2816");
}

void test_fixed_guard_at28c010(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[3]); /* AT28C010 */
    drive_reference_emitter(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP32_28C512_EEPROM, SDP_FIXED_DIP32_28C512_EEPROM_LEN,
        "reference-emitter guard: AT28C010 / DIP32_28C512_EEPROM");
}

void test_fixed_guard_at28c040(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[4]); /* AT28C040 */
    drive_reference_emitter(&h, FLASH_DISABLE_WRITE_PROTECTION, 6, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP32_28C512_EEPROM, SDP_FIXED_DIP32_28C512_EEPROM_LEN,
        "reference-emitter guard: AT28C040 / DIP32_28C512_EEPROM");
}

/* Pattern 3: dispatch on ADDRESS, not call order. Virgin 0xFF everywhere
 * except the two planted manufacturer/device identity bytes. Two per-address
 * read counters (mfr / SDP-completion-poll @ 0x5555) let any previously
 * call-ordinal assertion be re-expressed positionally. */
static void mock_set_data_keyed(firestarter_handle_t*, uint32_t, uint8_t) {}
static uint8_t mock_get_data_keyed(firestarter_handle_t*, uint32_t addr) {
    if (addr == s_mfr_addr_keyed) {
        s_reads_at_mfr_addr++;
        return s_mfr_hi_keyed;
    }
    if (addr == s_mfr_addr_keyed + 1) {
        s_reads_at_mfr_addr++;
        return s_mfr_lo_keyed;
    }
    if (addr == 0x5555) {
        s_reads_at_poll_addr++;
        return 0xFF;
    }
    return 0xFF;
}

static firestarter_handle_t make_identity_handle(uint16_t expected_chip_id, uint32_t ctrl_flags) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.mem_size = 32768; /* AT28C256 -- mfr_addr = mem_size - 64 = 0x7FC0 */
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = expected_chip_id;
    h.ctrl_flags = ctrl_flags | FLAG_SKIP_BLANK_CHECK;
    return h;
}

void test_migrated_mismatching_chip_id_errors(void) {
    s_mfr_addr_keyed = 32768 - 64; /* 0x7FC0 */
    s_mfr_hi_keyed = 0xDE;
    s_mfr_lo_keyed = 0xAD;
    firestarter_handle_t h = make_identity_handle(0x1F08, 0);
    configure_memory(&h);
    /* configure_memory() overwrites BOTH firestarter_get_data AND
     * firestarter_set_data (Pattern 3) -- the retired suite's own comment
     * covered only get_data. Re-assign both. */
    h.firestarter_get_data = mock_get_data_keyed;
    h.firestarter_set_data = mock_set_data_keyed;
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "migrated: mismatching identity must ERROR -- early-return before the SDP sequence, "
        "outcome-independent of the Phase 117 fix");
}

void test_migrated_zero_chip_id_skips_check(void) {
    s_mfr_addr_keyed = 32768 - 64;
    s_mfr_hi_keyed = 0xFF;
    s_mfr_lo_keyed = 0xFF;
    firestarter_handle_t h = make_identity_handle(0, 0);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_keyed;
    h.firestarter_set_data = mock_set_data_keyed;
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_reads_at_mfr_addr,
        "migrated: zero chip_id must skip eeprom28c_check_chip_id entirely -- re-expressed as a "
        "per-address read counter (Pattern 3), independent of the SDP outcome");
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_case1_ordered_capture_dip28_28c256);
    RUN_TEST(test_case2_elision_is_real);
    RUN_TEST(test_case3_ce_oe_edges_distinguishable);

    RUN_TEST(test_negativeA_unlock_mutated_diverges_and_matches_erase);
    RUN_TEST(test_negativeB_lock_table_swapped_for_write_prefix);
    RUN_TEST(test_lock05_enable_write_and_write_protection_identical);
    RUN_TEST(test_lock05_three_way_enable_table_identity);
    RUN_TEST(test_lock05_enable_table_objects_distinct);
    RUN_TEST(test_fix05_terminal_byte_and_table_identity_guards);
    RUN_TEST(test_fix05_guard_rejects_planted_terminal_mutation);
    RUN_TEST(test_fixed_guard_at28c256);
    RUN_TEST(test_fixed_guard_at28c64);
    RUN_TEST(test_fixed_guard_at28c16);
    RUN_TEST(test_fixed_guard_at28c010);
    RUN_TEST(test_fixed_guard_at28c040);

    RUN_TEST(test_migrated_mismatching_chip_id_errors);
    RUN_TEST(test_migrated_zero_chip_id_skips_check);

    return UNITY_END();
}
