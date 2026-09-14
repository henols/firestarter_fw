/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * RE-CAPTURED A THIRD TIME, and the totals below rewritten with it, by debug
 * session w27c512-write-slow-3x (2026-08-22). WHAT THESE ARRAYS NOW ENCODE:
 * **one program-voltage route assert and one settle PER PASS**, not per byte.
 *
 * HOW TO READ A FUTURE DIFF OF THIS FILE. The totals grew (121 -> 131,
 * 148 -> 149, 92 -> 101) because a pass-batched loop reads the block again
 * to decide the next pass, where the per-byte loop interleaved its verify
 * read with each pulse. A future capture that DROPS these totals back
 * toward the old values -- and in particular a control-register stream that
 * shows CTRL_VPE_ENABLE (0x04) / CTRL_VPP_P1_ENABLE (0x08) rising once per
 * PROGRAMMED BYTE rather than once per PASS -- means the ~3.7x write-speed
 * regression has returned. Do not re-freeze such a capture without
 * re-measuring a real 64 KiB write first.
 *
 * This suite is NOT run by CI (.github/workflows/build.yml and
 * beta-build.yml run only `pio test -e native` and `-e native_nodevtools`),
 * so it cannot be the only guard. The per-pass cadence is additionally
 * pinned, as an exact route-assert count with a demonstrated red/green, by
 * test_writeperf_route_is_asserted_once_per_pass_not_once_per_byte and
 * test_writeperf_route_assert_count_tracks_passes_not_pulses in
 * test/native/avr/test_val_eprom/test_val_eprom.cpp, which IS in both
 * pinned envs' test_filter.
 *
 *   cd /workspaces/firestarter && PLATFORMIO_BUILD_FLAGS="-D EPROM_V131_TRACE_DUMP" \
 *     pio test -e native_trace_v131 --without-testing
 *   cd /workspaces/firestarter && .pio/build/native_trace_v131/firestarter_native \
 *     > /tmp/gsd-144/trace_dump.txt
 *
 * The trace records EVERY timing entry UNFILTERED — including the 1 µs
 * latch delay that rurp_internal_write_to_register emits after each
 * non-elided register latch. That entry looks like noise, but filtering it
 * out would hide the exact interleaving (strobe, then delay, then strobe)
 * this fixture exists to prove is captured at all. Filtering is a decision
 * this fixture deliberately declines to make.
 *
 * Unlike sdp_expected.h (which compares ONE stream — ordered strobes only),
 * this header compares a MERGED stream: strobes interleaved with the timing
 * entries that occurred between them, spliced positionally by the sequence
 * key each timing_entry_t carries (its `seq`, the strobe_count() value at
 * push time). See v131_merged_at() below for the exact splice rule.
 *
 * This fixture now arms the identity gate for v1.32 drift detection
 * (tests/golden/eprom_v131_trace_inventory.json's meta.frozen_for): a future
 * divergence from the three arrays below is a regression to investigate,
 * not expected work — the inverse framing of what this file's banner said
 * before this capture replaced it.
 */

#ifndef __EPROM_V131_EXPECTED_H__
#define __EPROM_V131_EXPECTED_H__

#include <stdint.h>
#include <unity.h>
#include "firestarter.h"  /* pulls in rurp_shield.h -> LEAST_SIGNIFICANT_BYTE / MOST_SIGNIFICANT_BYTE / OUTPUT_ENABLE / CHIP_ENABLE, used only in provenance comments here, not by this header's own code */

extern "C" void    clear_strobes();
extern "C" int     strobe_count();
extern "C" int     strobe_overflowed();
extern "C" uint8_t strobe_kind(int i);
extern "C" uint8_t strobe_pin(int i);
extern "C" uint8_t strobe_value(int i);

extern "C" void     clear_timings();
extern "C" int      timing_count();
extern "C" int      timing_overflowed();
extern "C" uint8_t  timing_kind(int i);
extern "C" uint32_t timing_us(int i);
extern "C" int      timing_after_strobe(int i);

/* A MERGED element. TU-local by design (the recorders' own structs are
 * TU-local too, defined inside host_stubs.cpp's .inc-include) — given its
 * own name here, exactly the rationale sdp_expected.h's sdp_strobe_t states
 * for the strobe-only case:
 *   - for a STROBE entry, `kind` is STROBE_KIND_DATA or STROBE_KIND_PIN,
 *     `pin`/`value` carry the strobe's pin+value, and `us` is 0.
 *   - for a TIMING entry, `kind` is TIMING_KIND_DELAY_US or
 *     TIMING_KIND_DELAY_MS, `us` carries the delay value, and `pin`/`value`
 *     are 0.
 * The two kind namespaces are disjoint by construction (1/2 vs 3/4), so a
 * single `kind` field unambiguously tells a reader which of the two shapes
 * applies without a separate discriminant. */
typedef struct {
    uint8_t  kind;
    uint8_t  pin;
    uint8_t  value;
    uint32_t us;
} v131_trace_entry_t;

/* Mirrors host_stubs_common.inc's two TU-local enums by value (both enums
 * are TU-local to host_stubs.cpp, not exported) — named constants here
 * rather than magic numbers in every literal array entry, exactly
 * sdp_expected.h's convention for STROBE_KIND_DATA/STROBE_KIND_PIN. */
#define STROBE_KIND_DATA     1
#define STROBE_KIND_PIN      2
#define TIMING_KIND_DELAY_US 3
#define TIMING_KIND_DELAY_MS 4

/* Total length of the merged stream: every strobe plus every timing. */
static int v131_merged_length() {
    return strobe_count() + timing_count();
}

/* Splice rule: every timing whose timing_after_strobe() equals i is emitted
 * IMMEDIATELY BEFORE strobe i (in push order among ties — the underlying
 * timing log is append-only, so increasing timing index IS push order);
 * timings whose key equals strobe_count() (i.e. they were pushed after the
 * last strobe, or no strobe exists at all) are emitted after the last
 * strobe. This is a two-pointer merge of two already-sorted sequences (the
 * strobe index 0..strobe_count() ascending, and timing_after_strobe(t)
 * ascending as t ascends, since s_strobe_count never decreases within one
 * drive) — O(merged length) per call, which is more than fast enough for
 * these small fixtures.
 *
 * Returns true and fills *out when k is a valid position; returns false
 * (leaving *out untouched) when k is out of range. */
static bool v131_merged_at(int k, v131_trace_entry_t* out) {
    int sc = strobe_count();
    int tc = timing_count();
    int pos = 0;
    int t = 0;
    for (int i = 0; i <= sc; i++) {
        while (t < tc && timing_after_strobe(t) == i) {
            if (pos == k) {
                out->kind  = timing_kind(t);
                out->pin   = 0;
                out->value = 0;
                out->us    = timing_us(t);
                return true;
            }
            pos++;
            t++;
        }
        if (i < sc) {
            if (pos == k) {
                out->kind  = strobe_kind(i);
                out->pin   = strobe_pin(i);
                out->value = strobe_value(i);
                out->us    = 0;
                return true;
            }
            pos++;
        }
    }
    return false;
}

/* Returns the index of the first element where the LIVE merged stream
 * differs from `expected` (treating a length mismatch as a divergence at
 * the shorter length), or -1 when they are equal. Never counts anything —
 * mirrors sdp_first_divergence's contract exactly: every comparison is
 * positional, over all four fields. */
static int v131_first_divergence(const v131_trace_entry_t* expected, int expected_len) {
    int recorded_len = v131_merged_length();
    int n = (recorded_len < expected_len) ? recorded_len : expected_len;
    for (int i = 0; i < n; i++) {
        v131_trace_entry_t rec;
        v131_merged_at(i, &rec);
        if (rec.kind != expected[i].kind ||
            rec.pin != expected[i].pin ||
            rec.value != expected[i].value ||
            rec.us != expected[i].us) {
            return i;
        }
    }
    if (recorded_len != expected_len) {
        return n; /* length mismatch, no earlier element difference: diverge at the shorter length */
    }
    return -1;
}

static void v131_assert_stream_equals(const v131_trace_entry_t* expected, int expected_len, const char* ctx) {
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(expected_len, v131_merged_length(), ctx);

    int div = v131_first_divergence(expected, expected_len);
    if (div != -1) {
        char msg[400];
        int rec_len = v131_merged_length();
        if (div < rec_len && div < expected_len) {
            v131_trace_entry_t rec;
            v131_merged_at(div, &rec);
            snprintf(msg, sizeof(msg),
                "%s: diverges at index %d -- expected {kind=%u pin=%u value=0x%02X us=%lu}, recorded {kind=%u pin=%u value=0x%02X us=%lu}",
                ctx, div,
                (unsigned)expected[div].kind, (unsigned)expected[div].pin, (unsigned)expected[div].value, (unsigned long)expected[div].us,
                (unsigned)rec.kind, (unsigned)rec.pin, (unsigned)rec.value, (unsigned long)rec.us);
        } else {
            snprintf(msg, sizeof(msg),
                "%s: diverges at index %d (length mismatch -- expected_len=%d recorded_len=%d)",
                ctx, div, expected_len, rec_len);
        }
        TEST_FAIL_MESSAGE(msg);
    }
}

/* Snapshot the LIVE merged stream into caller-provided storage (used for
 * stream-versus-stream comparison — e.g. the determinism proof, which drives
 * the same protocol twice and compares two snapshots positionally, since
 * clear_strobes()/clear_timings() wipe the recorders between drives).
 * Returns the number of entries copied (capped at max_len). */
static int v131_snapshot(v131_trace_entry_t* out, int max_len) {
    int n = v131_merged_length();
    if (n > max_len) n = max_len;
    for (int i = 0; i < n; i++) {
        v131_merged_at(i, &out[i]);
    }
    return n;
}

/* ─── EPROM_V131_TRACE_PROTO_07 -- AM27C512, protocol 0x07, DIP28_27512 ─────
 * This is the POST-v1.31, post-route-assert-fix cadence, now frozen for
 * v1.32 drift detection
 * (tests/golden/eprom_v131_trace_inventory.json's meta.frozen_for) -- a
 * future divergence from this array is a regression to investigate, not
 * expected work.
 */
static const v131_trace_entry_t EPROM_V131_TRACE_PROTO_07[] = {
    {1, 0x00, 0x81, 0UL}, /* 0 */
    {2, 0x08, 0x01, 0UL}, /* 1 */
    {3, 0x00, 0x00, 1UL}, /* 2 */
    {2, 0x08, 0x00, 0UL}, /* 3 */
    {4, 0x00, 0x00, 500UL}, /* 4 */
    {2, 0x04, 0x00, 0UL}, /* 5 */
    {1, 0x00, 0x91, 0UL}, /* 6 */
    {2, 0x08, 0x01, 0UL}, /* 7 */
    {3, 0x00, 0x00, 1UL}, /* 8 */
    {2, 0x08, 0x00, 0UL}, /* 9 */
    {2, 0x20, 0x00, 0UL}, /* 10 */
    {3, 0x00, 0x00, 3UL}, /* 11 */
    {2, 0x20, 0x01, 0UL}, /* 12 */
    {2, 0x04, 0x00, 0UL}, /* 13 */
    {1, 0x00, 0x02, 0UL}, /* 14 */
    {2, 0x01, 0x01, 0UL}, /* 15 */
    {3, 0x00, 0x00, 1UL}, /* 16 */
    {2, 0x01, 0x00, 0UL}, /* 17 */
    {2, 0x20, 0x00, 0UL}, /* 18 */
    {3, 0x00, 0x00, 3UL}, /* 19 */
    {2, 0x20, 0x01, 0UL}, /* 20 */
    {2, 0x04, 0x00, 0UL}, /* 21 */
    {1, 0x00, 0x03, 0UL}, /* 22 */
    {2, 0x01, 0x01, 0UL}, /* 23 */
    {3, 0x00, 0x00, 1UL}, /* 24 */
    {2, 0x01, 0x00, 0UL}, /* 25 */
    {2, 0x20, 0x00, 0UL}, /* 26 */
    {3, 0x00, 0x00, 3UL}, /* 27 */
    {2, 0x20, 0x01, 0UL}, /* 28 */
    {1, 0x00, 0x95, 0UL}, /* 29 */
    {2, 0x08, 0x01, 0UL}, /* 30 */
    {3, 0x00, 0x00, 1UL}, /* 31 */
    {2, 0x08, 0x00, 0UL}, /* 32 */
    {3, 0x00, 0x00, 1000UL}, /* 33 */
    {2, 0x04, 0x01, 0UL}, /* 34 */
    {1, 0x00, 0x02, 0UL}, /* 35 */
    {2, 0x01, 0x01, 0UL}, /* 36 */
    {3, 0x00, 0x00, 1UL}, /* 37 */
    {2, 0x01, 0x00, 0UL}, /* 38 */
    {1, 0x00, 0x55, 0UL}, /* 39 */
    {3, 0x00, 0x00, 3UL}, /* 40 */
    {2, 0x20, 0x00, 0UL}, /* 41 */
    {3, 0x00, 0x00, 100UL}, /* 42 */
    {2, 0x20, 0x01, 0UL}, /* 43 */
    {2, 0x04, 0x01, 0UL}, /* 44 */
    {1, 0x00, 0x03, 0UL}, /* 45 */
    {2, 0x01, 0x01, 0UL}, /* 46 */
    {3, 0x00, 0x00, 1UL}, /* 47 */
    {2, 0x01, 0x00, 0UL}, /* 48 */
    {1, 0x00, 0xAA, 0UL}, /* 49 */
    {3, 0x00, 0x00, 3UL}, /* 50 */
    {2, 0x20, 0x00, 0UL}, /* 51 */
    {3, 0x00, 0x00, 100UL}, /* 52 */
    {2, 0x20, 0x01, 0UL}, /* 53 */
    {3, 0x00, 0x00, 100UL}, /* 54 */
    {1, 0x00, 0x91, 0UL}, /* 55 */
    {2, 0x08, 0x01, 0UL}, /* 56 */
    {3, 0x00, 0x00, 1UL}, /* 57 */
    {2, 0x08, 0x00, 0UL}, /* 58 */
    {2, 0x04, 0x00, 0UL}, /* 59 */
    {1, 0x00, 0x02, 0UL}, /* 60 */
    {2, 0x01, 0x01, 0UL}, /* 61 */
    {3, 0x00, 0x00, 1UL}, /* 62 */
    {2, 0x01, 0x00, 0UL}, /* 63 */
    {2, 0x20, 0x00, 0UL}, /* 64 */
    {3, 0x00, 0x00, 3UL}, /* 65 */
    {2, 0x20, 0x01, 0UL}, /* 66 */
    {2, 0x04, 0x00, 0UL}, /* 67 */
    {1, 0x00, 0x03, 0UL}, /* 68 */
    {2, 0x01, 0x01, 0UL}, /* 69 */
    {3, 0x00, 0x00, 1UL}, /* 70 */
    {2, 0x01, 0x00, 0UL}, /* 71 */
    {2, 0x20, 0x00, 0UL}, /* 72 */
    {3, 0x00, 0x00, 3UL}, /* 73 */
    {2, 0x20, 0x01, 0UL}, /* 74 */
    {1, 0x00, 0x95, 0UL}, /* 75 */
    {2, 0x08, 0x01, 0UL}, /* 76 */
    {3, 0x00, 0x00, 1UL}, /* 77 */
    {2, 0x08, 0x00, 0UL}, /* 78 */
    {3, 0x00, 0x00, 1000UL}, /* 79 */
    {2, 0x04, 0x01, 0UL}, /* 80 */
    {1, 0x00, 0x02, 0UL}, /* 81 */
    {2, 0x01, 0x01, 0UL}, /* 82 */
    {3, 0x00, 0x00, 1UL}, /* 83 */
    {2, 0x01, 0x00, 0UL}, /* 84 */
    {1, 0x00, 0x55, 0UL}, /* 85 */
    {3, 0x00, 0x00, 3UL}, /* 86 */
    {2, 0x20, 0x00, 0UL}, /* 87 */
    {3, 0x00, 0x00, 100UL}, /* 88 */
    {2, 0x20, 0x01, 0UL}, /* 89 */
    {3, 0x00, 0x00, 100UL}, /* 90 */
    {1, 0x00, 0x91, 0UL}, /* 91 */
    {2, 0x08, 0x01, 0UL}, /* 92 */
    {3, 0x00, 0x00, 1UL}, /* 93 */
    {2, 0x08, 0x00, 0UL}, /* 94 */
    {2, 0x04, 0x00, 0UL}, /* 95 */
    {2, 0x20, 0x00, 0UL}, /* 96 */
    {3, 0x00, 0x00, 3UL}, /* 97 */
    {2, 0x20, 0x01, 0UL}, /* 98 */
    {2, 0x04, 0x00, 0UL}, /* 99 */
    {1, 0x00, 0x00, 0UL}, /* 100 */
    {2, 0x01, 0x01, 0UL}, /* 101 */
    {3, 0x00, 0x00, 1UL}, /* 102 */
    {2, 0x01, 0x00, 0UL}, /* 103 */
    {2, 0x20, 0x00, 0UL}, /* 104 */
    {3, 0x00, 0x00, 3UL}, /* 105 */
    {2, 0x20, 0x01, 0UL}, /* 106 */
    {2, 0x04, 0x00, 0UL}, /* 107 */
    {1, 0x00, 0x01, 0UL}, /* 108 */
    {2, 0x01, 0x01, 0UL}, /* 109 */
    {3, 0x00, 0x00, 1UL}, /* 110 */
    {2, 0x01, 0x00, 0UL}, /* 111 */
    {2, 0x20, 0x00, 0UL}, /* 112 */
    {3, 0x00, 0x00, 3UL}, /* 113 */
    {2, 0x20, 0x01, 0UL}, /* 114 */
    {2, 0x04, 0x00, 0UL}, /* 115 */
    {1, 0x00, 0x02, 0UL}, /* 116 */
    {2, 0x01, 0x01, 0UL}, /* 117 */
    {3, 0x00, 0x00, 1UL}, /* 118 */
    {2, 0x01, 0x00, 0UL}, /* 119 */
    {2, 0x20, 0x00, 0UL}, /* 120 */
    {3, 0x00, 0x00, 3UL}, /* 121 */
    {2, 0x20, 0x01, 0UL}, /* 122 */
    {2, 0x04, 0x00, 0UL}, /* 123 */
    {1, 0x00, 0x03, 0UL}, /* 124 */
    {2, 0x01, 0x01, 0UL}, /* 125 */
    {3, 0x00, 0x00, 1UL}, /* 126 */
    {2, 0x01, 0x00, 0UL}, /* 127 */
    {2, 0x20, 0x00, 0UL}, /* 128 */
    {3, 0x00, 0x00, 3UL}, /* 129 */
    {2, 0x20, 0x01, 0UL}, /* 130 */
};
#define EPROM_V131_TRACE_PROTO_07_LEN (int)(sizeof(EPROM_V131_TRACE_PROTO_07) / sizeof(EPROM_V131_TRACE_PROTO_07[0]))

/* ─── EPROM_V131_TRACE_PROTO_08 -- AM27C020, protocol 0x08, DIP32_27C020 ────
 * This is the POST-v1.31, post-route-assert-fix cadence, now frozen for
 * v1.32 drift detection
 * (tests/golden/eprom_v131_trace_inventory.json's meta.frozen_for) -- a
 * future divergence from this array is a regression to investigate, not
 * expected work.
 */
static const v131_trace_entry_t EPROM_V131_TRACE_PROTO_08[] = {
    {1, 0x00, 0x81, 0UL}, /* 0 */
    {2, 0x08, 0x01, 0UL}, /* 1 */
    {3, 0x00, 0x00, 1UL}, /* 2 */
    {2, 0x08, 0x00, 0UL}, /* 3 */
    {4, 0x00, 0x00, 500UL}, /* 4 */
    {2, 0x04, 0x00, 0UL}, /* 5 */
    {1, 0x00, 0xC0, 0UL}, /* 6 */
    {2, 0x08, 0x01, 0UL}, /* 7 */
    {3, 0x00, 0x00, 1UL}, /* 8 */
    {2, 0x08, 0x00, 0UL}, /* 9 */
    {2, 0x20, 0x00, 0UL}, /* 10 */
    {3, 0x00, 0x00, 3UL}, /* 11 */
    {2, 0x20, 0x01, 0UL}, /* 12 */
    {2, 0x04, 0x00, 0UL}, /* 13 */
    {1, 0x00, 0x02, 0UL}, /* 14 */
    {2, 0x01, 0x01, 0UL}, /* 15 */
    {3, 0x00, 0x00, 1UL}, /* 16 */
    {2, 0x01, 0x00, 0UL}, /* 17 */
    {2, 0x20, 0x00, 0UL}, /* 18 */
    {3, 0x00, 0x00, 3UL}, /* 19 */
    {2, 0x20, 0x01, 0UL}, /* 20 */
    {2, 0x04, 0x00, 0UL}, /* 21 */
    {1, 0x00, 0x03, 0UL}, /* 22 */
    {2, 0x01, 0x01, 0UL}, /* 23 */
    {3, 0x00, 0x00, 1UL}, /* 24 */
    {2, 0x01, 0x00, 0UL}, /* 25 */
    {2, 0x20, 0x00, 0UL}, /* 26 */
    {3, 0x00, 0x00, 3UL}, /* 27 */
    {2, 0x20, 0x01, 0UL}, /* 28 */
    {1, 0x00, 0xC8, 0UL}, /* 29 */
    {2, 0x08, 0x01, 0UL}, /* 30 */
    {3, 0x00, 0x00, 1UL}, /* 31 */
    {2, 0x08, 0x00, 0UL}, /* 32 */
    {3, 0x00, 0x00, 1000UL}, /* 33 */
    {2, 0x04, 0x01, 0UL}, /* 34 */
    {1, 0x00, 0x02, 0UL}, /* 35 */
    {2, 0x01, 0x01, 0UL}, /* 36 */
    {3, 0x00, 0x00, 1UL}, /* 37 */
    {2, 0x01, 0x00, 0UL}, /* 38 */
    {1, 0x00, 0x88, 0UL}, /* 39 */
    {2, 0x08, 0x01, 0UL}, /* 40 */
    {3, 0x00, 0x00, 1UL}, /* 41 */
    {2, 0x08, 0x00, 0UL}, /* 42 */
    {1, 0x00, 0x55, 0UL}, /* 43 */
    {3, 0x00, 0x00, 3UL}, /* 44 */
    {2, 0x20, 0x00, 0UL}, /* 45 */
    {3, 0x00, 0x00, 100UL}, /* 46 */
    {2, 0x20, 0x01, 0UL}, /* 47 */
    {2, 0x04, 0x01, 0UL}, /* 48 */
    {1, 0x00, 0x03, 0UL}, /* 49 */
    {2, 0x01, 0x01, 0UL}, /* 50 */
    {3, 0x00, 0x00, 1UL}, /* 51 */
    {2, 0x01, 0x00, 0UL}, /* 52 */
    {1, 0x00, 0xAA, 0UL}, /* 53 */
    {3, 0x00, 0x00, 3UL}, /* 54 */
    {2, 0x20, 0x00, 0UL}, /* 55 */
    {3, 0x00, 0x00, 100UL}, /* 56 */
    {2, 0x20, 0x01, 0UL}, /* 57 */
    {3, 0x00, 0x00, 100UL}, /* 58 */
    {1, 0x00, 0x80, 0UL}, /* 59 */
    {2, 0x08, 0x01, 0UL}, /* 60 */
    {3, 0x00, 0x00, 1UL}, /* 61 */
    {2, 0x08, 0x00, 0UL}, /* 62 */
    {3, 0x00, 0x00, 4UL}, /* 63 */
    {2, 0x04, 0x00, 0UL}, /* 64 */
    {1, 0x00, 0x02, 0UL}, /* 65 */
    {2, 0x01, 0x01, 0UL}, /* 66 */
    {3, 0x00, 0x00, 1UL}, /* 67 */
    {2, 0x01, 0x00, 0UL}, /* 68 */
    {1, 0x00, 0xC0, 0UL}, /* 69 */
    {2, 0x08, 0x01, 0UL}, /* 70 */
    {3, 0x00, 0x00, 1UL}, /* 71 */
    {2, 0x08, 0x00, 0UL}, /* 72 */
    {2, 0x20, 0x00, 0UL}, /* 73 */
    {3, 0x00, 0x00, 3UL}, /* 74 */
    {2, 0x20, 0x01, 0UL}, /* 75 */
    {2, 0x04, 0x00, 0UL}, /* 76 */
    {1, 0x00, 0x03, 0UL}, /* 77 */
    {2, 0x01, 0x01, 0UL}, /* 78 */
    {3, 0x00, 0x00, 1UL}, /* 79 */
    {2, 0x01, 0x00, 0UL}, /* 80 */
    {2, 0x20, 0x00, 0UL}, /* 81 */
    {3, 0x00, 0x00, 3UL}, /* 82 */
    {2, 0x20, 0x01, 0UL}, /* 83 */
    {1, 0x00, 0xC8, 0UL}, /* 84 */
    {2, 0x08, 0x01, 0UL}, /* 85 */
    {3, 0x00, 0x00, 1UL}, /* 86 */
    {2, 0x08, 0x00, 0UL}, /* 87 */
    {3, 0x00, 0x00, 1000UL}, /* 88 */
    {2, 0x04, 0x01, 0UL}, /* 89 */
    {1, 0x00, 0x02, 0UL}, /* 90 */
    {2, 0x01, 0x01, 0UL}, /* 91 */
    {3, 0x00, 0x00, 1UL}, /* 92 */
    {2, 0x01, 0x00, 0UL}, /* 93 */
    {1, 0x00, 0x88, 0UL}, /* 94 */
    {2, 0x08, 0x01, 0UL}, /* 95 */
    {3, 0x00, 0x00, 1UL}, /* 96 */
    {2, 0x08, 0x00, 0UL}, /* 97 */
    {1, 0x00, 0x55, 0UL}, /* 98 */
    {3, 0x00, 0x00, 3UL}, /* 99 */
    {2, 0x20, 0x00, 0UL}, /* 100 */
    {3, 0x00, 0x00, 100UL}, /* 101 */
    {2, 0x20, 0x01, 0UL}, /* 102 */
    {3, 0x00, 0x00, 100UL}, /* 103 */
    {1, 0x00, 0x80, 0UL}, /* 104 */
    {2, 0x08, 0x01, 0UL}, /* 105 */
    {3, 0x00, 0x00, 1UL}, /* 106 */
    {2, 0x08, 0x00, 0UL}, /* 107 */
    {3, 0x00, 0x00, 4UL}, /* 108 */
    {2, 0x04, 0x00, 0UL}, /* 109 */
    {1, 0x00, 0xC0, 0UL}, /* 110 */
    {2, 0x08, 0x01, 0UL}, /* 111 */
    {3, 0x00, 0x00, 1UL}, /* 112 */
    {2, 0x08, 0x00, 0UL}, /* 113 */
    {2, 0x20, 0x00, 0UL}, /* 114 */
    {3, 0x00, 0x00, 3UL}, /* 115 */
    {2, 0x20, 0x01, 0UL}, /* 116 */
    {2, 0x04, 0x00, 0UL}, /* 117 */
    {1, 0x00, 0x00, 0UL}, /* 118 */
    {2, 0x01, 0x01, 0UL}, /* 119 */
    {3, 0x00, 0x00, 1UL}, /* 120 */
    {2, 0x01, 0x00, 0UL}, /* 121 */
    {2, 0x20, 0x00, 0UL}, /* 122 */
    {3, 0x00, 0x00, 3UL}, /* 123 */
    {2, 0x20, 0x01, 0UL}, /* 124 */
    {2, 0x04, 0x00, 0UL}, /* 125 */
    {1, 0x00, 0x01, 0UL}, /* 126 */
    {2, 0x01, 0x01, 0UL}, /* 127 */
    {3, 0x00, 0x00, 1UL}, /* 128 */
    {2, 0x01, 0x00, 0UL}, /* 129 */
    {2, 0x20, 0x00, 0UL}, /* 130 */
    {3, 0x00, 0x00, 3UL}, /* 131 */
    {2, 0x20, 0x01, 0UL}, /* 132 */
    {2, 0x04, 0x00, 0UL}, /* 133 */
    {1, 0x00, 0x02, 0UL}, /* 134 */
    {2, 0x01, 0x01, 0UL}, /* 135 */
    {3, 0x00, 0x00, 1UL}, /* 136 */
    {2, 0x01, 0x00, 0UL}, /* 137 */
    {2, 0x20, 0x00, 0UL}, /* 138 */
    {3, 0x00, 0x00, 3UL}, /* 139 */
    {2, 0x20, 0x01, 0UL}, /* 140 */
    {2, 0x04, 0x00, 0UL}, /* 141 */
    {1, 0x00, 0x03, 0UL}, /* 142 */
    {2, 0x01, 0x01, 0UL}, /* 143 */
    {3, 0x00, 0x00, 1UL}, /* 144 */
    {2, 0x01, 0x00, 0UL}, /* 145 */
    {2, 0x20, 0x00, 0UL}, /* 146 */
    {3, 0x00, 0x00, 3UL}, /* 147 */
    {2, 0x20, 0x01, 0UL}, /* 148 */
};
#define EPROM_V131_TRACE_PROTO_08_LEN (int)(sizeof(EPROM_V131_TRACE_PROTO_08) / sizeof(EPROM_V131_TRACE_PROTO_08[0]))

/* ─── EPROM_V131_TRACE_PROTO_0B -- AM2716, protocol 0x0B, DIP24_2716 ────────
 * This is the POST-v1.31, post-route-assert-fix cadence, now frozen for
 * v1.32 drift detection
 * (tests/golden/eprom_v131_trace_inventory.json's meta.frozen_for) -- a
 * future divergence from this array is a regression to investigate, not
 * expected work.
 */
static const v131_trace_entry_t EPROM_V131_TRACE_PROTO_0B[] = {
    {1, 0x00, 0x80, 0UL}, /* 0 */
    {2, 0x08, 0x01, 0UL}, /* 1 */
    {3, 0x00, 0x00, 1UL}, /* 2 */
    {2, 0x08, 0x00, 0UL}, /* 3 */
    {4, 0x00, 0x00, 500UL}, /* 4 */
    {2, 0x04, 0x00, 0UL}, /* 5 */
    {1, 0x00, 0x20, 0UL}, /* 6 */
    {2, 0x02, 0x01, 0UL}, /* 7 */
    {3, 0x00, 0x00, 1UL}, /* 8 */
    {2, 0x02, 0x00, 0UL}, /* 9 */
    {2, 0x20, 0x00, 0UL}, /* 10 */
    {3, 0x00, 0x00, 3UL}, /* 11 */
    {2, 0x20, 0x01, 0UL}, /* 12 */
    {2, 0x04, 0x00, 0UL}, /* 13 */
    {1, 0x00, 0x02, 0UL}, /* 14 */
    {2, 0x01, 0x01, 0UL}, /* 15 */
    {3, 0x00, 0x00, 1UL}, /* 16 */
    {2, 0x01, 0x00, 0UL}, /* 17 */
    {2, 0x20, 0x00, 0UL}, /* 18 */
    {3, 0x00, 0x00, 3UL}, /* 19 */
    {2, 0x20, 0x01, 0UL}, /* 20 */
    {2, 0x04, 0x00, 0UL}, /* 21 */
    {1, 0x00, 0x03, 0UL}, /* 22 */
    {2, 0x01, 0x01, 0UL}, /* 23 */
    {3, 0x00, 0x00, 1UL}, /* 24 */
    {2, 0x01, 0x00, 0UL}, /* 25 */
    {2, 0x20, 0x00, 0UL}, /* 26 */
    {3, 0x00, 0x00, 3UL}, /* 27 */
    {2, 0x20, 0x01, 0UL}, /* 28 */
    {1, 0x00, 0x88, 0UL}, /* 29 */
    {2, 0x08, 0x01, 0UL}, /* 30 */
    {3, 0x00, 0x00, 1UL}, /* 31 */
    {2, 0x08, 0x00, 0UL}, /* 32 */
    {3, 0x00, 0x00, 1000UL}, /* 33 */
    {2, 0x04, 0x01, 0UL}, /* 34 */
    {1, 0x00, 0x02, 0UL}, /* 35 */
    {2, 0x01, 0x01, 0UL}, /* 36 */
    {3, 0x00, 0x00, 1UL}, /* 37 */
    {2, 0x01, 0x00, 0UL}, /* 38 */
    {1, 0x00, 0x55, 0UL}, /* 39 */
    {3, 0x00, 0x00, 3UL}, /* 40 */
    {2, 0x20, 0x00, 0UL}, /* 41 */
    {3, 0x00, 0x00, 500UL}, /* 42 */
    {2, 0x20, 0x01, 0UL}, /* 43 */
    {2, 0x04, 0x01, 0UL}, /* 44 */
    {1, 0x00, 0x03, 0UL}, /* 45 */
    {2, 0x01, 0x01, 0UL}, /* 46 */
    {3, 0x00, 0x00, 1UL}, /* 47 */
    {2, 0x01, 0x00, 0UL}, /* 48 */
    {1, 0x00, 0xAA, 0UL}, /* 49 */
    {3, 0x00, 0x00, 3UL}, /* 50 */
    {2, 0x20, 0x00, 0UL}, /* 51 */
    {3, 0x00, 0x00, 500UL}, /* 52 */
    {2, 0x20, 0x01, 0UL}, /* 53 */
    {3, 0x00, 0x00, 100UL}, /* 54 */
    {1, 0x00, 0x80, 0UL}, /* 55 */
    {2, 0x08, 0x01, 0UL}, /* 56 */
    {3, 0x00, 0x00, 1UL}, /* 57 */
    {2, 0x08, 0x00, 0UL}, /* 58 */
    {3, 0x00, 0x00, 4UL}, /* 59 */
    {2, 0x04, 0x00, 0UL}, /* 60 */
    {1, 0x00, 0x02, 0UL}, /* 61 */
    {2, 0x01, 0x01, 0UL}, /* 62 */
    {3, 0x00, 0x00, 1UL}, /* 63 */
    {2, 0x01, 0x00, 0UL}, /* 64 */
    {2, 0x20, 0x00, 0UL}, /* 65 */
    {3, 0x00, 0x00, 3UL}, /* 66 */
    {2, 0x20, 0x01, 0UL}, /* 67 */
    {2, 0x04, 0x00, 0UL}, /* 68 */
    {1, 0x00, 0x03, 0UL}, /* 69 */
    {2, 0x01, 0x01, 0UL}, /* 70 */
    {3, 0x00, 0x00, 1UL}, /* 71 */
    {2, 0x01, 0x00, 0UL}, /* 72 */
    {2, 0x20, 0x00, 0UL}, /* 73 */
    {3, 0x00, 0x00, 3UL}, /* 74 */
    {2, 0x20, 0x01, 0UL}, /* 75 */
    {1, 0x00, 0x88, 0UL}, /* 76 */
    {2, 0x08, 0x01, 0UL}, /* 77 */
    {3, 0x00, 0x00, 1UL}, /* 78 */
    {2, 0x08, 0x00, 0UL}, /* 79 */
    {3, 0x00, 0x00, 1000UL}, /* 80 */
    {2, 0x04, 0x01, 0UL}, /* 81 */
    {1, 0x00, 0x02, 0UL}, /* 82 */
    {2, 0x01, 0x01, 0UL}, /* 83 */
    {3, 0x00, 0x00, 1UL}, /* 84 */
    {2, 0x01, 0x00, 0UL}, /* 85 */
    {1, 0x00, 0x55, 0UL}, /* 86 */
    {3, 0x00, 0x00, 3UL}, /* 87 */
    {2, 0x20, 0x00, 0UL}, /* 88 */
    {3, 0x00, 0x00, 500UL}, /* 89 */
    {2, 0x20, 0x01, 0UL}, /* 90 */
    {3, 0x00, 0x00, 100UL}, /* 91 */
    {1, 0x00, 0x80, 0UL}, /* 92 */
    {2, 0x08, 0x01, 0UL}, /* 93 */
    {3, 0x00, 0x00, 1UL}, /* 94 */
    {2, 0x08, 0x00, 0UL}, /* 95 */
    {3, 0x00, 0x00, 4UL}, /* 96 */
    {2, 0x04, 0x00, 0UL}, /* 97 */
    {2, 0x20, 0x00, 0UL}, /* 98 */
    {3, 0x00, 0x00, 3UL}, /* 99 */
    {2, 0x20, 0x01, 0UL}, /* 100 */
};
#define EPROM_V131_TRACE_PROTO_0B_LEN (int)(sizeof(EPROM_V131_TRACE_PROTO_0B) / sizeof(EPROM_V131_TRACE_PROTO_0B[0]))

#endif /* __EPROM_V131_EXPECTED_H__ */
