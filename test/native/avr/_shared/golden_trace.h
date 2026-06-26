/*
 * golden_trace.h — Shared golden-trace assert/bless helper.
 *
 * Phase 88 Plan 01 (PRIM-01 / SAFE-02).
 *
 * Provides assert_trace_eq() for byte-exact equality comparison of a captured
 * recording-bus array against a pinned expected array (count + every (reg,data)
 * element, D-01), and a GOLDEN_BLESS print mode for one-step fixture re-bless
 * (D-02).
 *
 * INCLUDE ORDER: This header MUST be included AFTER the suite's extern "C"
 * recording-API declarations:
 *
 *   extern "C" void    clear_bus_recording();
 *   extern "C" int     bus_recording_count();
 *   extern "C" uint8_t recorded_reg(int i);
 *   extern "C" uint8_t recorded_data(int i);
 *
 * LOW-BYTE-ONLY CAVEAT (Pitfall 1):
 *   rurp_write_to_register() stores (uint8_t)data — the 0x100 bit
 *   (CTRL_VPP_VPE_DROP_ENABLE on Rev2) is INVISIBLE in the 8-bit trace.
 *   Golden traces pin only the low 8 bits of every register write.
 *   The existing INV-01/INV-03 bit-level assertions in each test_val_*
 *   suite remain as the complementary guard for that bit.
 *
 * RECORDING-CAP GUARD (Pitfall 2):
 *   assert_trace_eq() first checks bus_recording_count() < 256 to detect
 *   silent truncation at the HOST_STUBS_MAX_RECORDING boundary (256 entries).
 *   A fixture whose write sequence exceeds the cap would otherwise silently
 *   produce a false-passing golden array authored from a truncated capture.
 *   Size every fixture to a minimal representative input that stays under
 *   the cap while exercising every algorithm branch (D-04).
 */

#ifndef GOLDEN_TRACE_H
#define GOLDEN_TRACE_H

#include <stdint.h>
#include <unity.h>

#ifdef GOLDEN_BLESS
#include <stdio.h>
#endif

/* ---------------------------------------------------------------------------
 * golden_entry_t — plain struct matching the recording-bus entry shape.
 * Uses uint8_t to match the (uint8_t)data low-byte store semantics.
 * ---------------------------------------------------------------------------*/
struct golden_entry_t {
    uint8_t reg;
    uint8_t data;
};

/* ---------------------------------------------------------------------------
 * assert_trace_eq — byte-exact equality: count first (D-01), then every element.
 *
 *   exp  — pointer to the golden expected-array (from the .inc fixture)
 *   n    — number of entries in exp
 *   ctx  — label for failure messages (identifies which path drifted)
 *
 * Assertion order (D-01):
 *   1. bus_recording_count() < 256       — anti-truncation guard (Pitfall 2)
 *   2. n == bus_recording_count()        — count equality
 *   3. per-element: exp[i].reg  == recorded_reg(i)
 *                   exp[i].data == recorded_data(i)
 * ---------------------------------------------------------------------------*/
static inline void assert_trace_eq(const golden_entry_t* exp, int n, const char* ctx) {
    TEST_ASSERT_TRUE_MESSAGE(
        bus_recording_count() < 256,
        "golden trace truncated at recording cap (256 entries) — "
        "resize fixture to a minimal representative input (D-04, Pitfall 2)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(n, bus_recording_count(), ctx);  /* count first (D-01) */
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(exp[i].reg,  recorded_reg(i),  ctx);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(exp[i].data, recorded_data(i), ctx);
    }
}

/* ---------------------------------------------------------------------------
 * print_trace_inc — bless-mode: print the captured recording as .inc-ready
 * rows to stdout.  Compile with -D GOLDEN_BLESS to activate; redirect the
 * output into the fixture .inc file.  Each row matches the form:
 *     { 0xRR, 0xDD },
 * (D-02 re-bless: re-run suite with GOLDEN_BLESS, redirect, review git diff.)
 * ---------------------------------------------------------------------------*/
#ifdef GOLDEN_BLESS
static inline void print_trace_inc(void) {
    for (int i = 0; i < bus_recording_count(); i++) {
        printf("    { 0x%02X, 0x%02X },\n", recorded_reg(i), recorded_data(i));
    }
}
#endif /* GOLDEN_BLESS */

#endif /* GOLDEN_TRACE_H */
