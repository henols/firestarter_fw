/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Two Unity RUN_TEST cases under the include-as-source pattern (RESEARCH.md
 * Q2 Option D + PATTERNS.md Excerpt 2):
 *
 *   2. test_rurp_read_data_buffer_reassembles_data_bus
 *      Regression guard around rurp_read_data_buffer()'s shift-and-mask
 *      reassembly logic. PASSES on pre-fix code (the bit-mapping logic is
 *      unchanged by either Wave B fix commit). Guards against accidentally
 *      breaking the bit map while inserting _NOP() settling delays in
 *      Wave B Commit 2.
 *
 * Native-test integration uses the include-as-source pattern: this TU
 * #defines ARDUINO_AVR_LEONARDO and then #includes the production Leonardo
 * board source directly (FOUR dot-dot segments from this file's location;
 * see PATTERNS.md Excerpt 2 lines 89-93). The function compiles inside the
 * test TU only, without polluting sibling suites' build_src_filter. The
 * shared host stubs file is NOT included; see host_stubs.cpp for the
 * rationale.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <stdint.h>

/* --- _BV() shim. avr-libc defines `_BV(n) ((1) << (n))` in <avr/io.h>.
 * ArduinoFake does NOT provide _BV (RESEARCH.md Q6 verified — only
 * `bit()` is provided). The included leonardo_rurp_shield.cpp uses _BV
 * extensively at lines 96-104 (rurp_write_data_buffer) and 119-126
 * (rurp_read_data_buffer), so we must define it before the source-include.
 * The 1U cast keeps the expression unsigned per the avr-libc convention. */
#ifndef _BV
#define _BV(n) (1U << (n))
#endif

/* --- Host-side AVR register shim. MUST be BEFORE leonardo_rurp_shield.cpp
 * is included so the included source sees these as plain uint8_t globals. */
static uint8_t PORTD = 0, PORTC = 0, PORTE = 0;
static uint8_t DDRD  = 0, DDRC  = 0, DDRE  = 0;
static uint8_t PIND  = 0, PINC  = 0, PINE  = 0;
/* PORTB/DDRB are referenced by rurp_board_setup + rurp_set_control_pin
 * (never called by these tests, but the linker still resolves them). */
static uint8_t PORTB = 0, DDRB = 0;

/* --- Enable the Leonardo board guard, then pull the source into THIS TU
 * so rurp_set_data_input / rurp_read_data_buffer are exposed to the tests.
 *
 * LANDMINE — path depth FOUR dot-dot segments:
 *   test/native/avr/test_data_input/test_rurp_set_data_input.cpp
 *   one  -> test/native/avr/
 *   two  -> test/native/
 *   three -> test/
 *   four -> repo root
 *   then src/boards/leonardo_rurp_shield.cpp (the file being included).
 */
#define ARDUINO_AVR_LEONARDO
#include "../../../../src/boards/leonardo_rurp_shield.cpp"

void setUp(void) {
    ArduinoFakeReset();
    /* Reset all host-shim register globals to a known state before each test.
     * The "residual pullups" pre-state is set inside each test body. */
    PORTD = 0; PORTC = 0; PORTE = 0;
    DDRD  = 0; DDRC  = 0; DDRE  = 0;
    PIND  = 0; PINC  = 0; PINE  = 0;
}

void tearDown(void) {
}

/* ---------------------------------------------------------------------------
 * Test 2 — Regression guard for rurp_read_data_buffer bit-mapping.
 *
 * PASSES on pre-fix code (the bit-mapping logic at lines 119-126 is unchanged
 * by either Wave B fix commit). Wave B Commit 2 inserts _NOP() settling
 * delays between the three PINx reads; this case guards against accidentally
 * breaking the shift-and-mask reassembly while editing the read function.
 *
 * Bit map (per leonardo_rurp_shield.cpp:119-126):
 *   D0 = (PIND >> 2) & 1     // PD2
 *   D1 = (PIND >> 2) & 2     // PD3
 *   D2 = (PIND << 1) & 4     // PD1
 *   D3 = (PIND << 3) & 8     // PD0
 *   D4 =  PIND       & 16    // PD4
 *   D5 = (PINC >> 1) & 32    // PC6
 *   D6 = (PIND >> 1) & 64    // PD7
 *   D7 = (PINE << 1) & 128   // PE6
 * --------------------------------------------------------------------------- */
void test_rurp_read_data_buffer_reassembles_data_bus(void) {
    /* All-high data bus: PIND data bits + PINC bit 6 + PINE bit 6 all set.
     * Should reassemble to 0xFF. */
    PIND = PORTD_DATA_MASK;  /* 0x9F: bits 0,1,2,3,4,7 set */
    PINC = PORTC_DATA_MASK;  /* 0x40: bit 6 */
    PINE = PORTE_DATA_MASK;  /* 0x40: bit 6 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, rurp_read_data_buffer());

    /* All-low: returns 0x00 */
    PIND = 0; PINC = 0; PINE = 0;
    TEST_ASSERT_EQUAL_HEX8(0x00, rurp_read_data_buffer());

    /* Single-bit walks: D0 only set -> PD2 = 0x04 (bit 2 of PIND) */
    PIND = _BV(2); PINC = 0; PINE = 0;
    TEST_ASSERT_EQUAL_HEX8(0x01, rurp_read_data_buffer());

    /* D5 only -> PC6 = 0x40 (bit 6 of PINC) */
    PIND = 0; PINC = _BV(6); PINE = 0;
    TEST_ASSERT_EQUAL_HEX8(0x20, rurp_read_data_buffer());

    /* D7 only -> PE6 = 0x40 (bit 6 of PINE) */
    PIND = 0; PINC = 0; PINE = _BV(6);
    TEST_ASSERT_EQUAL_HEX8(0x80, rurp_read_data_buffer());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_rurp_read_data_buffer_reassembles_data_bus);

    return UNITY_END();
}
