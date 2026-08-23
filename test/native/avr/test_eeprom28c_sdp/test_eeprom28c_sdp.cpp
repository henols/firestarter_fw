/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 116 Plan 06 authored this suite PARKED and RED-by-design
 * (v1.22 Phase 116 Plan 06, TRACE-02/TRACE-04/TRACE-06). As of v1.22 Phase
 * 117 commit 1 (D-03), the suite is ENABLED in platformio.ini's
 * [env:native] test_filter and runs under `pio test -e native`.
 *
 * This suite pins the exact ordered (LSB, MSB, data, CE) stream
 * eeprom28c_write_init emits, for each of the four 0x0D pinouts plus a
 * second DIP32 size band, and asserts it against the FIXED (post-Phase-117)
 * target — TRACE-02. At Phase 117 commit 1 it is RED against the
 * still-unfixed production tree; the verbatim capture is committed at
 * RED-BASELINE.md under "## Post-suite-edit RED baseline (Phase 117 commit
 * 1 — D-03)". It is GREEN from commit 2 onward, once plan 117-02 lands the
 * production fix.
 *
 * Phase 116's D-01 claimed that this suite's one-line test_filter addition
 * would itself BE the whole RED-to-GREEN proof. That did not hold
 * (117-CONTEXT.md D-01/D-02/D-03 supersede it): two structural conflicts
 * would have kept the suite RED post-fix for reasons unrelated to the fix —
 * the suite's own no-op `set_data` mock (the exact pointer FIX-01's emitter
 * is built on) and five assertions that encoded today's INIT-abort as the
 * expected outcome. The real flip required four edits: this test_filter
 * line, un-mocking `set_data` (D-01), flipping five response-code
 * assertions plus adding one new permanent severity-preservation regression
 * case (D-02), and this file's own suite-header rewrite.
 *
 * Cases 6-7 additionally carry TRACE-06's re-runnable evidence: with
 * CORRECTION 2's fix applied (D-12 originally mis-routed one of these two
 * into the always-green harness), both migrated identity-gate cases assert
 * the post-fix expectation — a matching identity must proceed, and a
 * FLAG_FORCE mismatch's WARNING severity must survive the completion path
 * (D-05: the completion poll never writes response_code). Case 8 is new at
 * Phase 117 commit 1: a permanent regression case proving the completion
 * poll can never destroy a prior WARNING, even when it never settles.
 *
 * Cases 9-12 are new at Phase 118 Plan 05 (D-08, OBS-02/OBS-03/OBS-05),
 * driving PRODUCTION eeprom28c_write_init via configure_memory dispatch
 * (never the harness's drive_reference_emitter):
 *  - Case 9: FLAG_SKIP_SDP_UNLOCK set -- the unlock sequence is provably
 *    absent from the recorded BUS stream, asserted on content and position
 *    (exact divergence index 0, plus an explicit walk over recorded
 *    STROBE_KIND_DATA values for EEPROM_SDP_DISABLE's own payload bytes),
 *    never on a bare strobe count.
 *  - Case 10: the flag-absent counterpart, from the SAME handle factory as
 *    Case 9, asserting the full SDP_FIXED_DIP28_28C256 stream -- the pair
 *    ships together so the contrast is one executable comparison.
 *  - Case 11: the t_BLC runtime budget WARN is observed to actually FIRE
 *    (an over-budget synthesised elapsed value via Plan 118-03's
 *    s_micros_ticks seam) and observed to NOT fire under the default
 *    elapsed value -- the anti-hollow pair for a check that would otherwise
 *    be indistinguishable from a dead branch.
 *  - Case 12: the exactly-two-new-serial-frames enumeration (and its
 *    exactly-one-WARN skip-path mirror), via a per-case Serial-frame
 *    capture reusing test_rurp_log_id.cpp's existing AlwaysDo idiom -- NOT
 *    a new general-purpose recorder (D-07 explicitly declined building
 *    one). This strengthens, but never replaces, D-07's PRIMARY assertion:
 *    the recorded BUS stream's byte-identity (Cases 1-3/10 here, and the
 *    _shared/ blob-SHA check in RED-BASELINE.md).
 *
 * Validation ceiling (RED-BASELINE.md carries this in full): every claim
 * this suite embodies is software-layer — code emits a sequence, code
 * asserts on it. No AT28C part was ever on the bench, and nothing here is
 * evidence about silicon state.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <vector>

extern "C" {
#include "memory.h"
#include "eeprom_28c.h"
#include "messages.h"
}
#include "firestarter.h"
#include "flash_utils.h"
/* v1.22 Phase 119 D-06/D-07 (119-07 Task 3): op_execute_stateful_operation /
 * op_execute_simple_operation are now natively linkable (Task 1 widened
 * build_src_filter with operation_utils.cpp), so cases 24/25 below call
 * them directly -- the same op-layer functions every eprom_* entry point
 * delegates to. */
#include "operation_utils.h"
#include "../_shared/sdp_bus_config.h"
#include "../_shared/sdp_expected.h"

using namespace fakeit;

/* host_stubs.cpp's reset seam (D-05) — must run after configure_memory, which
 * itself writes address 0 (mem_util_set_address(handle, 0), memory.cpp:68). */
extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);

/* Plan 118-05 (D-08 constraint 1): EEPROM_SDP_DISABLE is the PRODUCTION
 * command table (external linkage granted at eeprom_28c.cpp:122, FIX-05
 * precedent -- test_sdp_harness.cpp:48 declares the identical extern).
 * Case 9's payload-byte-absence walk reads this exact array, never a
 * transcribed copy, so it stays byte-locked to whatever
 * eeprom28c_emit_command_sequence actually drives. */
extern const byte_flip_t EEPROM_SDP_DISABLE[6];

/* ─────────────────────────────────────────────────────────────────────────
 * setUp / tearDown
 * ───────────────────────────────────────────────────────────────────────── */

/* Address-keyed mock state (Pattern 3, migrated from the retired
 * test_eeprom28c_chip_id via test_sdp_harness), reset per-case in setUp().
 * Reused across ALL seven cases here: for cases 1-5 the identity axis is
 * irrelevant (chip_id == 0 skips eeprom28c_check_chip_id entirely) and the
 * mock's virgin 0xFF at 0x5555 is exactly what makes eeprom28c_wait_for_write
 * time out without contributing any further strobes to the recorded stream
 * (Task 1 acceptance criterion: strobe_overflowed() == 0, 2000 poll
 * iterations contribute zero strobes). */
static uint32_t s_mfr_addr_keyed;
static uint8_t  s_mfr_hi_keyed;
static uint8_t  s_mfr_lo_keyed;
static int      s_reads_at_mfr_addr;
static int      s_reads_at_poll_addr;
/* Case 8 (D-02, Phase 117 commit 1): when set, the 0x5555 poll address
 * toggles bit 0x40 on every read instead of returning a constant, so the
 * completion poll can never conclude. Reset false in setUp(). */
static bool     s_poll_addr_toggles;

/* Plan 119-05 Task 1: the tick source is now a SCRIPTED QUEUE, replacing the
 * two-slot parity alternator (indexed by call count modulo 2) that served
 * through Plan 118-05. RETIREMENT REASON: D-16's per-byte page-load tracker
 * (Plan 119-08) adds micros() calls INSIDE eeprom28c_write_execute, so any
 * case that drives both write_init and write_execute would call micros()
 * more than twice -- under the old modulo-2 model, every interval past the
 * second call would alternate between 0 and a constant, which is
 * meaningless (and silently so: the numbers still "look like" ticks). A
 * monotonic cursor into an explicit script has no such failure mode: it
 * returns exactly what was scripted, in order, for as many calls as the
 * script has entries.
 *
 * s_micros_script holds the scripted tick sequence; s_micros_cursor is the
 * monotonic read position (never wraps, never resets except via
 * sdp_script_micros() or setUp()'s file-static reset block below).
 * s_micros_tail is the TAIL VALUE: once the cursor reaches the end of the
 * script, every subsequent micros() call returns s_micros_tail UNCHANGED
 * and the cursor stops advancing. This is exactly the part that can
 * silently corrupt a measurement -- a case that scripts fewer ticks than
 * the code path actually calls micros() will get the tail value for every
 * read past the script's end, which reads as "a real measurement" but is
 * actually just the tail repeated. A case that needs a BOUNDED number of
 * reads (e.g. asserting an exact elapsed value) must therefore script
 * enough entries to cover every micros() call the driven path makes, and
 * must never assume the script was exhausted -- assert on the REPORTED
 * value instead. All three reset to {} / 0 / 0 in setUp() below. */
static std::vector<uint32_t> s_micros_script;
static size_t   s_micros_cursor;
static uint32_t s_micros_tail;

/* Installs `ticks` as the scripted sequence and resets the cursor to 0, so a
 * case reads as "script these ticks, then drive". `tail` defaults to 0 --
 * once the script is exhausted, subsequent micros() calls return 0 (the
 * same "elapsed always looks like 0 past the scripted ticks" default the
 * old alternator provided for every case that never opted into a non-zero
 * second tick). */
static void sdp_script_micros(const std::vector<uint32_t>& ticks, uint32_t tail = 0) {
    s_micros_script = ticks;
    s_micros_cursor = 0;
    s_micros_tail = tail;
}

/* Plan 118-05 Task 2 (D-07 scope discipline): a PER-CASE Serial-frame
 * capture, reusing test_rurp_log_id.cpp:59-63's existing AlwaysDo idiom
 * verbatim (accumulate every Serial.write(uint8_t) byte into a host
 * std::vector). This is NOT the general-purpose serial-frame baseline
 * recorder D-07 explicitly declined building -- it lives local to THIS
 * suite, is cleared per-case in setUp() below alongside the other
 * file-static resets, and nothing here is added to test/native/avr/_shared/.
 * D-07's PRIMARY assertion stays the recorded BUS stream's byte-identity
 * (sdp_assert_stream_equals / the RED-BASELINE.md blob-SHA record); this
 * capture only strengthens the serial-channel half of OBS-05's claim. */
static std::vector<uint8_t> captured_frames;

/* Walks captured_frames using rurp_log_id()'s fixed, documented wire layout
 * (test_rurp_log_id.cpp's own comment: 4-byte magic, 2-byte big-endian
 * length, 1 id byte, params, 1 crc byte, 1 anchor byte) and appends each
 * frame's id byte, IN ORDER, to *out_ids. Never reads param count from a
 * message-id lookup table -- the length field alone is sufficient to find
 * the next frame's start, so this stays correct regardless of how many
 * params any given id carries. */
static void sdp_captured_frame_ids(std::vector<uint8_t>* out_ids) {
    size_t offset = 0;
    while (offset + 7 <= captured_frames.size()) {
        uint16_t len_value = (uint16_t)(((uint16_t)captured_frames[offset + 4] << 8) | captured_frames[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len_value + 1;
        if (offset + frame_size > captured_frames.size()) {
            break; /* incomplete trailing frame -- not expected in these cases */
        }
        out_ids->push_back(captured_frames[offset + 6]);
        offset += frame_size;
    }
}

/* Content-order membership check over an already-enumerated id list --
 * never a count. */
static bool sdp_ids_contains(const std::vector<uint8_t>& ids, uint8_t id) {
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

/* Plan 119-08 Task 2: decodes the u32 parameter of the FIRST captured frame
 * whose id byte matches `target_id`, walking captured_frames with the SAME
 * documented wire layout sdp_captured_frame_ids uses above (4-byte magic,
 * 2-byte big-endian length, 1 id byte, params, 1 crc byte, 1 anchor byte).
 * len_value = 1 (id) + param_count + 1 (crc) (test_rurp_log_id.cpp's own
 * comment), so a *_U32 id -- exactly 4 param bytes, big-endian
 * (rurp_log_id_u32, rurp_serial_utils.cpp) -- always carries len_value == 6.
 * Returns true and writes *out_value on a match; false if the id never
 * appears. Membership alone (sdp_ids_contains) is NOT enough to prove a
 * tracker reports the right number -- a tracker that always reported zero,
 * or the LAST interval instead of the worst, would still pass a
 * membership-only check. */
static bool sdp_decode_u32_param_for_id(uint8_t target_id, uint32_t* out_value) {
    size_t offset = 0;
    while (offset + 7 <= captured_frames.size()) {
        uint16_t len_value = (uint16_t)(((uint16_t)captured_frames[offset + 4] << 8) | captured_frames[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len_value + 1;
        if (offset + frame_size > captured_frames.size()) {
            break; /* incomplete trailing frame -- not expected in these cases */
        }
        uint8_t id = captured_frames[offset + 6];
        if (id == target_id) {
            TEST_ASSERT_EQUAL_MESSAGE(6, (int)len_value,
                "sdp_decode_u32_param_for_id: matched id's length field is not exactly "
                "1(id)+4(u32 params)+1(crc) -- this helper only decodes a single u32 parameter");
            *out_value = ((uint32_t)captured_frames[offset + 7] << 24) |
                         ((uint32_t)captured_frames[offset + 8] << 16) |
                         ((uint32_t)captured_frames[offset + 9] << 8) |
                         (uint32_t)captured_frames[offset + 10];
            return true;
        }
        offset += frame_size;
    }
    return false;
}

void setUp(void) {
    ArduinoFakeReset();
    /* Plan 118-05 Task 2: was AlwaysReturn(1) through Plan 118-04. Switched to
     * AlwaysDo so every byte is ALSO captured into captured_frames -- this is
     * additive/behaviourally-transparent to every existing case (none of
     * cases 1-8 ever inspects captured_frames), confirmed by re-running all
     * ten cases (1-8 plus the two Task-1 cases) at 10/10 before Task 2's new
     * cases were added (see 118-05-SUMMARY.md). */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured_frames.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* REQUIRED (mirrors test_sdp_harness.cpp / D-05): the real
     * rurp_register_utils.h calls delayMicroseconds, and
     * eeprom28c_check_chip_id / eeprom28c_wait_for_write call delay() /
     * delayMicroseconds() too. ArduinoFake ABORTS (SIGABRT) on any unmocked
     * virtual. Do not remove these as "unused" — they are load-bearing. Plan
     * 118-04's OBS-04 duration bracket calls micros() twice per write_init
     * drive (immediately before and after
     * eeprom28c_emit_command_sequence's call); removing this mock produces
     * a SIGABRT indistinguishable from the deferred Unity-teardown flake
     * (D-13), not a compile error, so it is exactly as load-bearing as the
     * three mocks above it. */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysDo([]() -> unsigned long {
        unsigned long v;
        if (s_micros_cursor < s_micros_script.size()) {
            v = s_micros_script[s_micros_cursor];
            s_micros_cursor++;
        } else {
            v = s_micros_tail; /* script exhausted -- see the tail-value comment above */
        }
        return v;
    });

    clear_strobes();
    reset_register_cache(0x00, 0x00, 0x00);

    s_mfr_addr_keyed = 0;
    s_mfr_hi_keyed = 0xFF;
    s_mfr_lo_keyed = 0xFF;
    s_reads_at_mfr_addr = 0;
    s_reads_at_poll_addr = 0;
    s_poll_addr_toggles = false;
    s_micros_script.clear();
    s_micros_cursor = 0;
    s_micros_tail = 0;
    captured_frames.clear();
}

void tearDown(void) {}

/* ─────────────────────────────────────────────────────────────────────────
 * Handle + drive helpers
 * ───────────────────────────────────────────────────────────────────────── */

/* Plan 118-05 (D-08): extra_flags defaults to 0, so every one of cases 1-8's
 * existing make_sdp_handle(row) call sites is byte-for-byte unaffected --
 * no signature churn at those eight sites. Cases 9 and 10 pass
 * FLAG_SKIP_SDP_UNLOCK or 0 respectively, from this SAME factory and the
 * SAME row, so the only difference between the two cases is the flag bit
 * (mirrors make_identity_handle's existing ctrl_flags-parameter shape at
 * lines 163-178 below). FLAG_SKIP_BLANK_CHECK stays unconditionally set in
 * both, exactly as every other case here, so the blank-check axis
 * contributes no strobes to either stream. */
static firestarter_handle_t make_sdp_handle(const sdp_bus_config_row_t& row, uint32_t extra_flags = 0) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0; /* skip chip-id branch for cases 1-5 */
    h.mem_size = row.mem_size;
    h.bus_config = row.bus_config;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | extra_flags;
    return h;
}

/* ERASE-01 / 152-CONTEXT.md D-07: the ONLY factory in this suite that leaves
 * the blank-check axis LIVE (ctrl_flags = 0, not FLAG_SKIP_BLANK_CHECK).
 * Every other factory here ORs FLAG_SKIP_BLANK_CHECK in unconditionally, so
 * all 29 existing cases already exercise the no-blank-check path and are
 * unaffected by this factory's existence. This factory's whole purpose is
 * to prove that the blank-check axis no longer changes anything on 0x0D --
 * with the skip flag CLEAR, write-INIT must still be single-shot and must
 * still emit the exact golden stream, because the pre-write blank check is
 * deleted outright, not merely gated. */
static firestarter_handle_t make_sdp_handle_blank_check_enabled(const sdp_bus_config_row_t& row) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = row.mem_size;
    h.bus_config = row.bus_config;
    h.ctrl_flags = 0;
    return h;
}

static firestarter_handle_t make_identity_handle(uint16_t expected_chip_id, uint32_t ctrl_flags) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.mem_size = 32768; /* AT28C256 -- mfr_addr = mem_size - 64 = 0x7FC0 */
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = expected_chip_id;
    /* D-01 (Phase 117): dropping the set_data no-op routes cases 6-7 through
     * the real memory_set_data / mem_util_remap_address_bus for the first
     * time, so this factory now needs a real bus_config -- row 0 is
     * AT28C256 / DIP28_28C256, whose mem_size 32768 already matches this
     * factory's own h.mem_size above (and its derived mfr_addr 0x7FC0). */
    h.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h.ctrl_flags = ctrl_flags | FLAG_SKIP_BLANK_CHECK;
    return h;
}

/* Pattern 3: dispatch on ADDRESS, not call order. Virgin 0xFF everywhere
 * except the two planted manufacturer/device identity bytes; the SDP
 * completion-poll address (0x5555) is deliberately never satisfied. Migrated
 * from test_sdp_harness.cpp (116-05).
 *
 * D-01 (Phase 117 commit 1): only get_data is mocked here. The suite no
 * longer reassigns firestarter_set_data -- FIX-01 builds the emitter on
 * exactly that pointer, so a no-op there would make the post-fix recorded
 * stream empty. get_data stays mocked because it is what collapses the
 * completion poll's 2000-iteration loop to zero strobes, which is the sole
 * reason full-stream equality is possible at all. */
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
        /* D-02 (Phase 117 commit 1, Case 8): post-fix the completion poll is
         * a bounded DQ6 toggle-bit poll, not an equality compare against a
         * value that is never written. A constant return means "settled
         * immediately"; s_poll_addr_toggles flips bit 0x40 on every read at
         * THIS address so it means "never settles" -- dispatch stays keyed
         * on ADDRESS, never on call order, per the rule above. The fixed
         * code draws NO conclusion from either outcome (D-05): the
         * conclusion is deferred to the page write's own poll, which has a
         * real written byte to compare against (FIX-06). */
        if (s_poll_addr_toggles) {
            return (s_reads_at_poll_addr % 2 == 0) ? (uint8_t)0x00 : (uint8_t)0x40;
        }
        return 0xFF; /* virgin default -- never satisfies the 0x20 SDP-disable poll */
    }
    return 0xFF;
}

/* The FIX-01 reference emitter: h->firestarter_set_data is memory_set_data
 * (assigned by configure_memory), which routes through
 * mem_util_remap_address_bus -- the remap-aware target stream, with zero
 * hand derivation. Identical shape to test_sdp_harness.cpp's helper of the
 * same name (separate TU, no shared linkage). */
static void drive_reference_emitter(firestarter_handle_t* h, const byte_flip_t* table, size_t len, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    for (size_t i = 0; i < len; i++) {
        h->firestarter_set_data(h, table[i].address, table[i].byte);
    }
}

/* Drives the REAL eeprom28c_write_init (via configure_memory dispatch) after
 * reassigning firestarter_get_data to the address-keyed mock, so the
 * 2000-iteration completion-poll timeout contributes zero strobes to the
 * recorded stream (Task 1 / Open Question 2 resolution: full-stream equality
 * is possible only because the mock satisfies no bus traffic). D-01 (Phase
 * 117): unlike Phase 116, firestarter_set_data is left as configure_memory's
 * real memory_set_data -- FIX-01 routes the emitter through that exact
 * pointer, so a no-op there would make the recorded stream empty post-fix. */
static void drive_write_init(firestarter_handle_t* h, rurp_register_t ctrl_seed) {
    configure_memory(h);
    h->firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    h->firestarter_operation_init(h);
}

/* Case 5's mechanism (RESEARCH Open Question 1, second leg): reaches the
 * stale-upper-address state via an ACTUAL preceding memory_get_data() read
 * rather than a directly-seeded cache, so a reviewer cannot object that a
 * hand-seeded cache is unrepresentative. DIP32_28C512_EEPROM's rw_line (20)
 * folds READ_FLAG=1 into CONTROL bit 0x10 (CTRL_ADDRESS_LINE_17) on every
 * real read through the production get_data path — one read is sufficient.
 * Returns the CONTROL value actually left behind, so the caller can drive an
 * identically-seeded reference emitter for comparison. */
static rurp_register_t drive_write_init_after_real_read(firestarter_handle_t* h, uint32_t probe_addr) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, 0x00);
    h->firestarter_get_data(h, probe_addr); /* REAL preceding read -- production memory_get_data */
    rurp_register_t stale_ctrl = rurp_read_from_register(CONTROL_REGISTER);
    h->firestarter_get_data = mock_get_data_keyed;
    /* D-01 (Phase 117): firestarter_set_data stays the real memory_set_data
     * (see drive_write_init's comment above) -- FIX-01's emitter is built on
     * it. */
    clear_strobes();
    h->firestarter_operation_init(h);
    return stale_ctrl;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Plan 119-05 Task 2/3 — driving the PRODUCTION lock op (CMD_SDP_LOCK)
 * ───────────────────────────────────────────────────────────────────────── */

/* Builds a handle for the lock op: CMD_SDP_LOCK, chip_id 0 (no identity
 * gate -- eeprom28c_sdp_lock_execute has none anyway, since init/end are
 * NULL for this cmd and configure_eeprom28c only ever sets `main`). */
static firestarter_handle_t make_lock_handle(const sdp_bus_config_row_t& row) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_SDP_LOCK;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = row.mem_size;
    h.bus_config = row.bus_config;
    h.ctrl_flags = 0;
    return h;
}

/* Plan 153-04 (ERASE-03/ERASE-04): builds a handle for the erase op --
 * CMD_ERASE, chip_id 0 (no identity gate -- eeprom28c_erase_execute has
 * none, since init/end are NULL for this cmd and configure_eeprom28c only
 * ever sets `main`). Identical to make_lock_handle above except cmd. */
static firestarter_handle_t make_erase_handle(const sdp_bus_config_row_t& row) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_ERASE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = row.mem_size;
    h.bus_config = row.bus_config;
    h.ctrl_flags = 0;
    return h;
}

/* Load-bearing order (Task 2's key_link, mirrored from drive_reference_emitter
 * / drive_write_init above): configure_memory (itself writes mem_util_set_address(handle, 0)),
 * THEN reset_register_cache, THEN clear_strobes, THEN the op call --
 * h.firestarter_operation_main(&h), since init/end are NULL by design for
 * CMD_SDP_LOCK (LOCK-02) so calling `main` directly IS the whole operation. */
static void drive_lock_op(firestarter_handle_t* h, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    h->firestarter_operation_main(h);
}

/* Plan 153-04 (ERASE-04): drives the REAL eeprom28c_erase_execute (via
 * configure_memory dispatch on CMD_ERASE), following the same load-bearing
 * order as drive_write_init/drive_lock_op above -- configure_memory, THEN
 * reassign get_data, THEN reset_register_cache, THEN clear_strobes, THEN the
 * op call directly (init/end are NULL for CMD_ERASE, so main IS the whole
 * operation, same reasoning as drive_lock_op).
 *
 * LOAD-BEARING, unlike drive_lock_op: the erase's SDP-disable prefix
 * (eeprom28c_sdp_unlock_execute, called first inside eeprom28c_erase_execute)
 * ends in eeprom28c_wait_for_sdp_completion, which polls via
 * handle->firestarter_get_data. Left as the real memory_get_data, each of
 * that poll's up to 2000 iterations would latch a fresh address
 * (memory_get_data -> firestarter_set_address -> LSB/MSB/CONTROL register
 * writes), flooding the 512-entry strobe recorder long before the erase's
 * own six writes ever run and turning every downstream positional stream
 * assertion into an overflow failure. Reassigning to mock_get_data_keyed
 * (same mock drive_write_init uses) contributes zero strobes to that poll.
 * firestarter_set_data is left as the real memory_set_data -- the whole
 * point of this driver is to record what the production routing emits. */
static void drive_erase_op(firestarter_handle_t* h, rurp_register_t ctrl_seed) {
    configure_memory(h);
    h->firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    h->firestarter_operation_main(h);
}

#ifdef SDP_TRACE_DUMP
/* TEMPORARY (Plan 119-05 Task 2): dumps the production lock op's recorded
 * stream for each SDP_BUS_CONFIGS row, mirroring test_sdp_harness.cpp's
 * dump_strobes helper. `pio test` swallows printf from test bodies, so this
 * is run via the built suite binary directly
 * (.pio/build/native/test_eeprom28c_sdp/program or the equivalent path under
 * .pio/build/native/), never via `pio test`. Kept behind this #ifdef,
 * matching test_sdp_harness.cpp's style -- never compiled by default. */
static void dump_strobes_ready_to_paste(const char* tag) {
    printf("##### %s total=%d overflow=%d\n", tag, strobe_count(), strobe_overflowed());
    for (int i = 0; i < strobe_count(); i++) {
        if (strobe_kind(i) == STROBE_KIND_DATA) {
            printf("    {1, 0, 0x%02X},\n", strobe_value(i));
        } else {
            printf("    {2, 0x%02X, %d},\n", strobe_pin(i), strobe_value(i));
        }
    }
}

void test_dump_lock_goldens(void) {
    firestarter_handle_t h0 = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 / DIP28_28C256 */
    drive_lock_op(&h0, 0x00);
    dump_strobes_ready_to_paste("LOCK_DIP28_28C256");

    firestarter_handle_t h1 = make_lock_handle(SDP_BUS_CONFIGS[1]); /* AT28C64 / DIP28_28C64 */
    drive_lock_op(&h1, 0x00);
    dump_strobes_ready_to_paste("LOCK_DIP28_28C64");

    firestarter_handle_t h2 = make_lock_handle(SDP_BUS_CONFIGS[2]); /* AT28C16 / DIP24_2816 */
    drive_lock_op(&h2, 0x00);
    dump_strobes_ready_to_paste("LOCK_DIP24_2816");

    /* DIP32_28C512_EEPROM (row 3, AT28C010): deliberately stale upper-address
     * CONTROL seed (CTRL_ADDRESS_LINE_17|18), mirroring case 4's mechanism --
     * this pinout's remap is the identity function under a zero seed, so a
     * zero-seeded dump would prove almost nothing (only the OE-edge
     * reordering, which is already covered by the FIXED golden itself). */
    firestarter_handle_t h3 = make_lock_handle(SDP_BUS_CONFIGS[3]); /* AT28C010 */
    drive_lock_op(&h3, CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18);
    dump_strobes_ready_to_paste("LOCK_DIP32_28C512_EEPROM");
}
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 1-3 — ordered capture per DIP28/DIP24 pinout (TRACE-02, D-06)
 * ───────────────────────────────────────────────────────────────────────── */

/* RED today, named mechanical reason: eeprom28c_write_init's SDP-disable
 * sequence is emitted by flash_execute_command -> flash_util_byte_flipping ->
 * fu_flash_fast_address, which writes the LITERAL {0x5555, 0x2AAA} magic
 * addresses via raw LSB/MSB register writes and NEVER consults
 * handle->bus_config. On AT28C256/DIP28_28C256, remap(0x5555) == 0x9555 (MSB
 * 0x95, not 0x55) -- the shipped stream's address bytes are simply wrong for
 * this pinout's real bus wiring, so the ordered-stream comparison against the
 * FIX-01 target (SDP_FIXED_DIP28_28C256, built on the remap-aware
 * memory_set_data) diverges from the very first MSB write. */
void test_case1_at28c256_stream_matches_fixed(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_write_init(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN,
        "Case 1: AT28C256/DIP28_28C256 -- eeprom28c_write_init's raw-address shipped "
        "stream must match the FIX-01 remap-aware target");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 1: the completion poll is advisory only (D-05) and must never report ERROR "
        "for a write-init that emitted the correct sequence");
}

/* RED today, same mechanism as Case 1: on AT28C64/DIP28_28C64, remap(0x5555)
 * == 0x1555 (MSB 0x15, not 0x55) and remap(0x2AAA) == 0x0AAA (MSB 0x0A, not
 * 0x2A) -- again wrong for this pinout's real 13-address-line wiring. */
void test_case2_at28c64_stream_matches_fixed(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[1]); /* AT28C64 */
    drive_write_init(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C64, SDP_FIXED_DIP28_28C64_LEN,
        "Case 2: AT28C64/DIP28_28C64 -- eeprom28c_write_init's raw-address shipped "
        "stream must match the FIX-01 remap-aware target");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 2: the completion poll is advisory only (D-05) and must never report ERROR "
        "for a write-init that emitted the correct sequence");
}

/* RED today, same mechanism as Case 1: on AT28C16/DIP24_2816 (11 address
 * lines), remap(0x5555) == 0x0555 (MSB 0x05) and remap(0x2AAA) == 0x02AA
 * (MSB 0x02). */
void test_case3_at28c16_stream_matches_fixed(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[2]); /* AT28C16 */
    drive_write_init(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP24_2816, SDP_FIXED_DIP24_2816_LEN,
        "Case 3: AT28C16/DIP24_2816 -- eeprom28c_write_init's raw-address shipped "
        "stream must match the FIX-01 remap-aware target");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 3: the completion poll is advisory only (D-05) and must never report ERROR "
        "for a write-init that emitted the correct sequence");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 4-5 — DIP32_28C512_EEPROM, deliberate stale-upper-address state
 * (CORRECTION 3, TRACE-02, D-09)
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * CORRECTION 3 (116-RESEARCH.md CORRECTION 3 / 116-05-SUMMARY.md): a PLAIN
 * DIP32 trace against the canonical zero-CONTROL-seed SDP_FIXED_DIP32_*
 * target is decorative here. mem_util_remap_address_bus returns 0x5555
 * unchanged for this pinout (address_mask == 0xFFFF, rw_line-20's bit is
 * WRITE_FLAG == 0 on a write) -- shipped and fixed share byte-identical
 * LSB/MSB/data values under a zero seed, differing only by an INCIDENTAL
 * /OE-edge ordering artifact of which emitter function happens to be used. A
 * future fix that preserved that ordering would leave a plain trace GREEN
 * with nothing about the real bug proven.
 *
 * The REAL bug: DIP32's rw_line (20) folds into CONTROL bit 0x10
 * (CTRL_ADDRESS_LINE_17) -- the WRITE-ENABLE line this 32-pin pinout
 * repurposes an upper address line for. fu_flash_fast_address (the SHIPPED
 * emitter) writes ONLY LSB/MSB and NEVER writes CONTROL_REGISTER at all -- so
 * whatever a PRECEDING operation left in CONTROL's upper-address bits stays
 * stuck for the entire SDP sequence: /WE inhibited, and (for
 * CTRL_ADDRESS_LINE_18) the chip's real upper address wrong. The reference
 * emitter (memory_set_data, the FIX-01 target) recomputes and writes
 * CONTROL_REGISTER on every address change via mem_util_set_address, and
 * WOULD clear those stale bits.
 *
 * Non-decorative comparison target: instead of the canonical zero-seed
 * SDP_FIXED_DIP32_28C512_EEPROM array, both cases below dynamically drive the
 * reference emitter under the SAME stale seed as the shipped path, snapshot
 * it, and assert the shipped stream equals THAT snapshot. This is the
 * "large, unambiguous divergence" CORRECTION 3 calls for: shipped emits no
 * CONTROL_REGISTER entries under any seed (cache-hit branch is a no-op either
 * way), while the stale-seeded reference emitter emits an EXTRA
 * CONTROL_REGISTER write clearing the stale bits -- a difference in KIND, not
 * merely incidental ordering. It is also self-repairing: once Phase 117
 * rebuilds eeprom28c_write_init on the same remap-aware, CONTROL-writing
 * emitter, driving the "shipped" handle will produce byte-identical output to
 * driving the reference handle, and this assertion passes with no further
 * edit.
 */

/* Case 4 (STALE STATE MECHANISM: direct seed). AT28C010 (128 KB). */
void test_case4_at28c010_stale_direct_seed(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[3]); /* AT28C010 */
    drive_write_init(&h, CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18);

    sdp_strobe_t shipped_snapshot[64];
    int shipped_len = sdp_snapshot(shipped_snapshot, 64);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 4: shipped-path snapshot must not overflow");

    firestarter_handle_t h_ref = make_sdp_handle(SDP_BUS_CONFIGS[3]);
    drive_reference_emitter(&h_ref, FLASH_DISABLE_WRITE_PROTECTION, 6, CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 4: fixed-reference drive must not overflow");
    TEST_ASSERT_GREATER_THAN_MESSAGE(SDP_FIXED_DIP32_28C512_EEPROM_LEN, strobe_count(),
        "Case 4: stale-seeded reference emitter must emit an extra CONTROL_REGISTER write "
        "clearing CTRL_ADDRESS_LINE_17|18 versus the zero-seed target -- if this ever drops "
        "to the zero-seed length, the stale seed stopped taking effect");

    sdp_assert_stream_equals(shipped_snapshot, shipped_len,
        "Case 4: AT28C010/DIP32_28C512_EEPROM, directly-seeded stale CTRL_ADDRESS_LINE_17|18 -- "
        "shipped path (no CONTROL_REGISTER write, ever) must clear the stale write-inhibit bits "
        "like the fixed reference emitter (memory_set_data) does");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 4: the completion poll is advisory only (D-05) and must never report ERROR "
        "for a write-init that cleared the stale write-inhibit bits and emitted the correct "
        "sequence");
}

/* Case 5 (STALE STATE MECHANISM: a real preceding read through
 * memory_get_data, per RESEARCH Open Question 1's second leg). AT28C040
 * (512 KB) -- shares AT28C010's bus_config byte-for-byte (116-02 D-09). */
void test_case5_at28c040_stale_via_real_read(void) {
    /* Probe address 0x0000, deliberately NOT 0x5555/0x2AAA (the SDP
     * sequence's own magic addresses): CTRL_ADDRESS_LINE_17 is set by
     * rw_line's READ_FLAG bit alone, independent of which address is read, so
     * any address demonstrates the mechanism. Reusing 0x5555 would also
     * pre-warm the LSB/MSB latch cache to the SAME value write #1 of the SDP
     * sequence needs, eliding that write's address latch too (a real,
     * correct, but SEPARATE cache-elision effect) and conflating it with the
     * CONTROL-bit finding this case exists to isolate. */
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[4]); /* AT28C040 */
    rurp_register_t stale_ctrl = drive_write_init_after_real_read(&h, 0x0000);
    TEST_ASSERT_BITS_HIGH_MESSAGE((uint8_t)CTRL_ADDRESS_LINE_17, stale_ctrl,
        "Case 5: a real preceding read (at an arbitrary address, 0x0000) must leave "
        "CTRL_ADDRESS_LINE_17 stuck HIGH on DIP32_28C512_EEPROM -- rw_line 20 folds "
        "READ_FLAG into this CONTROL bit regardless of which address is read");

    sdp_strobe_t shipped_snapshot[64];
    int shipped_len = sdp_snapshot(shipped_snapshot, 64);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 5: shipped-path snapshot must not overflow");

    firestarter_handle_t h_ref = make_sdp_handle(SDP_BUS_CONFIGS[4]);
    drive_reference_emitter(&h_ref, FLASH_DISABLE_WRITE_PROTECTION, 6, stale_ctrl);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 5: fixed-reference drive must not overflow");
    TEST_ASSERT_GREATER_THAN_MESSAGE(SDP_FIXED_DIP32_28C512_EEPROM_LEN, strobe_count(),
        "Case 5: stale-seeded reference emitter must emit an extra CONTROL_REGISTER write "
        "clearing CTRL_ADDRESS_LINE_17 versus the zero-seed target");

    sdp_assert_stream_equals(shipped_snapshot, shipped_len,
        "Case 5: AT28C040/DIP32_28C512_EEPROM, stale CTRL_ADDRESS_LINE_17 reached via a REAL "
        "preceding read (memory_get_data, not a directly-seeded cache) -- shipped path must clear "
        "the write-inhibit bit like the fixed reference emitter does");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 5: the completion poll is advisory only (D-05) and must never report ERROR "
        "for a write-init that cleared the stale write-inhibit bit and emitted the correct "
        "sequence");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 6-7 — migrated identity-gate assertions, RED half (CORRECTION 2,
 * TRACE-04, TRACE-06)
 * ───────────────────────────────────────────────────────────────────────── */

/* Migrated from the retired test_eeprom28c_chip_id (D-12), landing HERE (not
 * the always-green test_sdp_harness) per CORRECTION 2: this case is
 * SDP-outcome-dependent, so it is RED today and only goes GREEN once Phase
 * 117 fixes eeprom28c_write_init's SDP-disable completion check.
 *
 * RED today, named mechanical reason: a matching identity never sets an
 * error or warning, so eeprom28c_write_init proceeds into
 * flash_execute_command(EEPROM_SDP_DISABLE); eeprom28c_wait_for_write's
 * completion poll (0x5555 == 0x20) never succeeds against the address-keyed
 * mock's virgin 0xFF, times out after 2000 iterations, and unconditionally
 * sets RESPONSE_CODE_ERROR (eeprom_28c.cpp:151-153) -- so
 * NOT_EQUAL(RESPONSE_CODE_ERROR) fails today. */
void test_case6_matching_chip_id_proceeds(void) {
    s_mfr_addr_keyed = 32768 - 64; /* 0x7FC0 */
    s_mfr_hi_keyed = 0x1F;
    s_mfr_lo_keyed = 0x08;
    firestarter_handle_t h = make_identity_handle(0x1F08, 0);
    configure_memory(&h);
    /* configure_memory() overwrites firestarter_get_data (Pattern 3) --
     * re-assign it. D-01 (Phase 117): firestarter_set_data is left as
     * configure_memory's real memory_set_data -- FIX-01's emitter is built
     * on that exact pointer, so a no-op here would make the post-fix
     * recorded stream empty. */
    h.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "migrated (RED, CORRECTION 2): matching identity must proceed past SDP-disable, not "
        "time out and overwrite response_code with ERROR");
}

/* CORRECTION 2's finding, second-order evidence of the inverted completion
 * check -- the check does not merely fail, it DESTROYS severity information.
 * With FLAG_FORCE, eeprom28c_check_chip_id correctly sets
 * RESPONSE_CODE_WARNING on a chip-id mismatch (eeprom_28c.cpp:88), but
 * eeprom28c_write_init's completion wait is UNCONDITIONAL (no flag skips it):
 * eeprom28c_wait_for_write's timeout unconditionally overwrites
 * handle->response_code with RESPONSE_CODE_ERROR, destroying the WARNING.
 * D-12 originally routed this case to the always-green suite; CORRECTION 2
 * moves it here. Do NOT weaken this assertion to make it pass today -- the
 * force/severity fork is exactly what the v1.16 Phase-89 CR-01 regression
 * slipped through (see .planning memory
 * reference_golden_trace_misses_severity_fork.md). */
void test_case7_mismatching_chip_id_with_force_warns(void) {
    s_mfr_addr_keyed = 32768 - 64;
    s_mfr_hi_keyed = 0xDE;
    s_mfr_lo_keyed = 0xAD;
    firestarter_handle_t h = make_identity_handle(0x1F08, FLAG_FORCE);
    configure_memory(&h);
    /* D-01 (Phase 117): see test_case6's comment above -- firestarter_set_data
     * is left as the real memory_set_data. */
    h.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "migrated (RED, CORRECTION 2): mismatching identity + FLAG_FORCE must WARN, not have its "
        "severity destroyed by the unconditional SDP-disable completion wait");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 8 — completion poll must never destroy a prior severity (D-02, D-05)
 * ───────────────────────────────────────────────────────────────────────── */

/* New at Phase 117 commit 1, a PERMANENT regression guard (not migrated,
 * not RED-only-by-construction). chip_id is 0 in make_sdp_handle, so no
 * identity path runs and the WARNING's provenance is unambiguous -- it can
 * only have been altered by the completion poll itself. s_poll_addr_toggles
 * makes the poll never conclude, so this also exercises the "never settles"
 * arm of the mock (the constant-return arm is already exercised by cases
 * 1-7). RED at this commit for a named reason: today's shipped
 * eeprom28c_write_init times out on its (0x5555, 0x20) read-back and
 * unconditionally overwrites handle->response_code with
 * RESPONSE_CODE_ERROR (eeprom_28c.cpp:153), destroying the WARNING -- the
 * same severity-destruction fork the v1.16 Phase-89 CR-01 regression
 * slipped through (.planning memory
 * reference_golden_trace_misses_severity_fork.md). */
void test_case8_completion_poll_preserves_prior_severity(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    h.response_code = RESPONSE_CODE_WARNING;
    s_poll_addr_toggles = true; /* the completion poll can never conclude */
    drive_write_init(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "Case 8: the completion poll is advisory only (D-05) and must never overwrite "
        "a prior response_code, even when it never settles");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 9-10 — the skip/no-skip stream pair, content-positional, from ONE
 * handle factory (D-08, Plan 118-05 Task 1, OBS-02)
 * ───────────────────────────────────────────────────────────────────────── */

/* D-08 constraint 1: drives PRODUCTION eeprom28c_write_init via
 * drive_write_init (configure_memory dispatch), never
 * drive_reference_emitter (which drives FLASH_DISABLE_WRITE_PROTECTION, a
 * DIFFERENT table -- proving nothing about the shipped FLAG_SKIP_SDP_UNLOCK
 * gate).
 *
 * D-08 constraint 2: every assertion below is on the ordered stream's
 * CONTENT, never a call count. Register-write elision (Phase 116 research
 * finding 10) is invisible to a counting test -- a bare
 * strobe_count() == 0 would pass even if the emitter ran and every write
 * happened to elide, so that check appears ONLY as secondary corroboration
 * at the end, never as the load-bearing proof. */
void test_case9_skip_flag_suppresses_unlock_stream(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0], FLAG_SKIP_SDP_UNLOCK); /* AT28C256 */
    drive_write_init(&h, 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(),
        "Case 9 (OBS-02): recorded stream must not overflow");

    /* Content-positional divergence (D-08 constraint 2): the recorded stream
     * must diverge from the full unlock stream at EXACTLY index 0 -- the
     * unlock sequence's own first strobe is absent from the very start, not
     * merely "somewhere". Asserting the exact index, rather than != -1, is
     * itself an assertion about content and position, never a count. */
    TEST_ASSERT_EQUAL_MESSAGE(0, sdp_first_divergence(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN),
        "Case 9 (OBS-02): FLAG_SKIP_SDP_UNLOCK set -- the recorded stream must diverge from the "
        "full unlock stream (SDP_FIXED_DIP28_28C256) starting at index 0");

    /* Content walk over every recorded DATA entry: none of EEPROM_SDP_DISABLE's
     * own payload bytes -- the PRODUCTION command sequence's own command
     * table, not a transcribed copy -- may appear as a DATA value anywhere in
     * the recorded stream. This is the assertion that survives register-write
     * elision (D-08 constraint 2): it inspects every recorded element's
     * {kind, value}, never a length or a count. */
    for (int i = 0; i < strobe_count(); i++) {
        if (strobe_kind(i) != STROBE_KIND_DATA) {
            continue;
        }
        for (size_t j = 0; j < sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]); j++) {
            char msg[224];
            snprintf(msg, sizeof(msg),
                "Case 9 (OBS-02): recorded DATA entry at index %d (value 0x%02X) matches "
                "EEPROM_SDP_DISABLE[%u]'s payload byte 0x%02X -- the unlock sequence must be "
                "TOTALLY ABSENT from the stream when FLAG_SKIP_SDP_UNLOCK is set",
                i, (unsigned)strobe_value(i), (unsigned)j, (unsigned)EEPROM_SDP_DISABLE[j].byte);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(EEPROM_SDP_DISABLE[j].byte, strobe_value(i), msg);
        }
    }

    /* Secondary corroboration ONLY (D-08 explicitly forbids a bare
     * strobe_count() from being the load-bearing proof) -- the two content
     * assertions above are what this case actually rests on. With the whole
     * unlock block skipped and FLAG_SKIP_BLANK_CHECK also set,
     * eeprom28c_write_init emits nothing else, so the count happens to be 0. */
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_count(),
        "Case 9 (OBS-02, secondary corroboration ONLY -- see the index-0 divergence and the "
        "payload-byte-absence walk above for the load-bearing proof)");
}

/* D-08 constraint 3: the flag-absent counterpart, from the SAME handle
 * factory and the SAME row as Case 9 above, asserting the FULL
 * SDP_FIXED_DIP28_28C256 stream, ships in the SAME commit as Case 9. Case 1
 * already makes a similar full-stream assertion, but from the OLD
 * (pre-118-05) factory shape that always passed extra_flags == 0 implicitly
 * -- this counterpart exists so the skip/no-skip pair is self-contained at
 * one call site each, and so a future edit to make_sdp_handle's default
 * ctrl_flags cannot silently drift the flag-absent baseline Case 9 is
 * contrasted against. Cross-reference: Case 9 immediately above. */
void test_case10_flag_absent_emits_full_unlock_stream(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0], 0); /* AT28C256, flag NOT set */
    drive_write_init(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN,
        "Case 10 (OBS-02, D-08 constraint 3): FLAG_SKIP_SDP_UNLOCK absent -- eeprom28c_write_init's "
        "stream must match the full FIX-01 remap-aware target, from the SAME handle factory Case 9 "
        "used, differing only by the flag bit");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 10: the completion poll is advisory only (D-05) and must never report ERROR for a "
        "write-init that emitted the correct sequence");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 11 — the t_BLC runtime budget WARN is observed to actually fire
 * (D-09, Plan 118-05 Task 2, OBS-03)
 * ───────────────────────────────────────────────────────────────────────── */

void test_case11_tblc_budget_exceeded_warns(void) {
    /* AT28C_TBLC_MAX_US (100) is #define'd inside eeprom_28c.cpp's own
     * translation unit (eeprom_28c.cpp:58) and is NOT exported via
     * eeprom_28c.h, so it cannot be included directly here. Mirrored as a
     * named local constant with an explicit citation, rather than left as
     * an unexplained magic number -- the sequence-length half of the
     * production budget formula (sdp_seq_len * AT28C_TBLC_MAX_US) IS derived
     * from the real EEPROM_SDP_DISABLE array below, so only the per-byte
     * microsecond ceiling itself needs mirroring. */
    const uint32_t TEST_MIRROR_AT28C_TBLC_MAX_US = 100; /* mirrors eeprom_28c.cpp:58 */
    uint32_t sdp_seq_len = (uint32_t)(sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]));
    uint32_t over_budget_elapsed = sdp_seq_len * TEST_MIRROR_AT28C_TBLC_MAX_US + 1;

    /* eeprom28c_write_init's unlock branch (via eeprom28c_emit_sdp_sequence_timed)
     * calls micros() exactly twice per drive: once immediately before
     * eeprom28c_emit_command_sequence, once immediately after. Script exactly
     * those two ticks so the difference exceeds the budget. */
    sdp_script_micros({0, over_budget_elapsed});
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* flag absent -- unlock runs */
    drive_write_init(&h, 0x00);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);

    int done_idx = -1, warn_idx = -1;
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == (uint8_t)MSG_INFO_SDP_UNLOCK_DONE_US) {
            done_idx = (int)i;
        }
        if (ids[i] == (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED) {
            warn_idx = (int)i;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(warn_idx != -1,
        "Case 11 (OBS-03, D-09): an over-budget elapsed value must make MSG_WARN_SDP_TBLC_EXCEEDED "
        "appear in the captured serial frames -- a runtime check never observed to fire is "
        "indistinguishable from a dead branch");
    TEST_ASSERT_TRUE_MESSAGE(done_idx != -1 && warn_idx > done_idx,
        "Case 11 (OBS-03): MSG_WARN_SDP_TBLC_EXCEEDED must appear AFTER MSG_INFO_SDP_UNLOCK_DONE_US "
        "in the captured frame order -- the budget check runs after the after-line");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "Case 11 (D-02/D-05): the budget WARN must never write handle->response_code -- severity "
        "lives in the message id's band alone");

    /* Anti-hollow control, same case (D-09's own requirement): with the
     * default tick behaviour (elapsed 0) restored, the WARN id must NOT
     * appear -- an appearing-only assertion could pass against a branch that
     * always fires regardless of the measured duration. */
    sdp_script_micros({0, 0});
    captured_frames.clear();
    firestarter_handle_t h2 = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive_write_init(&h2, 0x00);
    std::vector<uint8_t> ids_default;
    sdp_captured_frame_ids(&ids_default);
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids_default, (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED),
        "Case 11 (anti-hollow control): with the default elapsed value (0), MSG_WARN_SDP_TBLC_EXCEEDED "
        "must NOT appear -- proves the check is conditional on the measured duration, not a branch "
        "that always fires");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 12 — exactly the two new report frames (flag absent) / exactly one
 * WARN frame (flag set), enumerated (D-07 strengthening, OBS-05)
 * ───────────────────────────────────────────────────────────────────────── */

/* Strengthens, but never replaces, D-07's PRIMARY assertion: the recorded
 * BUS stream's byte-identity (Cases 1-3/10 above, and the _shared/ blob-SHA
 * record in RED-BASELINE.md, Plan 118-05 Task 3). This case only makes
 * OBS-05's serial-channel exception machine-checked instead of prose-only,
 * using the per-case capture declared above -- it does not build the
 * general-purpose recorder D-07 explicitly declined. */
/* Plan 119-05 Task 1 (re-verified under the scripted micros() queue, no
 * assertion changed): this case drives drive_write_init ONLY -- it never
 * calls eeprom28c_write_execute. That was incidental before the scripted
 * queue existed (the old modulo-2 alternator did not care how many
 * functions were driven); it is now LOAD-BEARING, because D-16's page-load
 * tracker (Plan 119-08) will add its own micros() calls inside
 * write_execute, and the frame counts asserted below (2 frames flag-absent,
 * 1 frame flag-set) describe write_init's report pair only. If this case is
 * ever widened to also drive write_execute, its frame-count expectations
 * MUST be re-scoped to account for write_execute's own report line -- do
 * not assume they stay 2 and 1. */
void test_case12_flag_absent_emits_exactly_two_report_frames(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* flag absent, default ticks */
    drive_write_init(&h, 0x00);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_EQUAL_MESSAGE(2, (int)ids.size(),
        "Case 12 (OBS-05): the flag-absent default path must emit EXACTLY two report frames");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_INFO_SDP_UNLOCK, ids.size() > 0 ? ids[0] : (uint8_t)0xFF,
        "Case 12: frame 0 must be MSG_INFO_SDP_UNLOCK");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_INFO_SDP_UNLOCK_DONE_US, ids.size() > 1 ? ids[1] : (uint8_t)0xFF,
        "Case 12: frame 1 must be MSG_INFO_SDP_UNLOCK_DONE_US");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_UNLOCK_SKIPPED),
        "Case 12: the flag-absent path must never emit MSG_WARN_SDP_UNLOCK_SKIPPED");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED),
        "Case 12: the flag-absent default (elapsed 0) path must never emit MSG_WARN_SDP_TBLC_EXCEEDED");

    /* Skip-path mirror, same case -- the executable form of D-02's "in place
     * of, never in addition to": with FLAG_SKIP_SDP_UNLOCK set, EXACTLY
     * MSG_WARN_SDP_UNLOCK_SKIPPED and neither INFO id. */
    captured_frames.clear();
    firestarter_handle_t h_skip = make_sdp_handle(SDP_BUS_CONFIGS[0], FLAG_SKIP_SDP_UNLOCK);
    drive_write_init(&h_skip, 0x00);
    std::vector<uint8_t> ids_skip;
    sdp_captured_frame_ids(&ids_skip);
    TEST_ASSERT_EQUAL_MESSAGE(1, (int)ids_skip.size(),
        "Case 12 (skip mirror, D-02): the skip path must emit EXACTLY one frame");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)MSG_WARN_SDP_UNLOCK_SKIPPED, ids_skip.size() > 0 ? ids_skip[0] : (uint8_t)0xFF,
        "Case 12 (skip mirror): the one frame must be MSG_WARN_SDP_UNLOCK_SKIPPED");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids_skip, (uint8_t)MSG_INFO_SDP_UNLOCK),
        "Case 12 (skip mirror): the skip path must never emit MSG_INFO_SDP_UNLOCK");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids_skip, (uint8_t)MSG_INFO_SDP_UNLOCK_DONE_US),
        "Case 12 (skip mirror): the skip path must never emit MSG_INFO_SDP_UNLOCK_DONE_US");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 13-16 — the production lock op's stream, per pinout (LOCK-01)
 * ───────────────────────────────────────────────────────────────────────── */

/* Every one of cases 13-19 below drives the PRODUCTION lock op --
 * h.cmd = CMD_SDP_LOCK, then h.firestarter_operation_main(&h) via
 * drive_lock_op -- never a transcribed table loop. The load-bearing drive
 * order (configure_memory, THEN reset_register_cache, THEN clear_strobes,
 * THEN the op call) lives in drive_lock_op itself, above. */

void test_case13_lock_dip28_28c256_stream_matches_fixed(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_lock_op(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_LOCK_DIP28_28C256, SDP_FIXED_LOCK_DIP28_28C256_LEN,
        "Case 13 (LOCK-01): AT28C256/DIP28_28C256 -- the production CMD_SDP_LOCK op's stream must "
        "match the dump-authored FIXED lock golden");
}

void test_case14_lock_dip28_28c64_stream_matches_fixed(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[1]); /* AT28C64 */
    drive_lock_op(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_LOCK_DIP28_28C64, SDP_FIXED_LOCK_DIP28_28C64_LEN,
        "Case 14 (LOCK-01): AT28C64/DIP28_28C64 -- the production CMD_SDP_LOCK op's stream must "
        "match the dump-authored FIXED lock golden");
}

void test_case15_lock_dip24_2816_stream_matches_fixed(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[2]); /* AT28C16 */
    drive_lock_op(&h, 0x00);
    sdp_assert_stream_equals(SDP_FIXED_LOCK_DIP24_2816, SDP_FIXED_LOCK_DIP24_2816_LEN,
        "Case 15 (LOCK-01): AT28C16/DIP24_2816 -- the production CMD_SDP_LOCK op's stream must "
        "match the dump-authored FIXED lock golden");
}

/* Case 16 uses the SAME deliberately stale upper-address CONTROL seed
 * (CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18) the DIP32 golden was recorded
 * under (see sdp_expected.h's SDP_FIXED_LOCK_DIP32_28C512_EEPROM comment) --
 * a zero seed would leave this pinout's remap the identity function and
 * prove almost nothing beyond the OE-edge reordering. */
void test_case16_lock_dip32_28c512_eeprom_stream_matches_fixed_stale_seed(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[3]); /* AT28C010 */
    drive_lock_op(&h, CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18);
    sdp_assert_stream_equals(SDP_FIXED_LOCK_DIP32_28C512_EEPROM, SDP_FIXED_LOCK_DIP32_28C512_EEPROM_LEN,
        "Case 16 (LOCK-01): AT28C010/DIP32_28C512_EEPROM, driven under the SAME deliberately stale "
        "upper-address CONTROL seed (CTRL_ADDRESS_LINE_17|18) its golden was recorded under");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 17 — the no-payload termination, D-10's load-bearing absence (LOCK-05)
 * ───────────────────────────────────────────────────────────────────────── */

/* EEPROM_SDP_ENABLE is byte-identical to FLASH_ENABLE_WRITE (the
 * protected-write prefix) and to FLASH_ENABLE_WRITE_PROTECTION -- the ONLY
 * thing that makes this sequence a LOCK rather than the first three writes
 * of a byte write is that NO DATA WRITE FOLLOWS it. A table comparison
 * cannot show an absence (Pitfall 4): this case asserts it POSITIONALLY, on
 * the live stream, three ways -- length equality against the golden, the
 * final entry being a PIN edge (not DATA), and no DATA entry anywhere after
 * the third write's payload index. */
void test_case17_lock_terminates_after_three_writes_no_trailing_data(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_lock_op(&h, 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 17: lock stream must not overflow");
    TEST_ASSERT_EQUAL_MESSAGE(SDP_FIXED_LOCK_DIP28_28C256_LEN, strobe_count(),
        "Case 17 (LOCK-05): the lock stream's length must equal the golden's length -- "
        "EEPROM_SDP_ENABLE is byte-identical to FLASH_ENABLE_WRITE (the protected-write prefix), so "
        "the only discriminator between a lock and a byte write is that no data write follows");

    int last = strobe_count() - 1;
    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_PIN, strobe_kind(last),
        "Case 17 (LOCK-05): the final recorded entry must be a PIN edge (CE deassert), never a DATA "
        "entry -- the absence of a trailing data write is the whole safety claim, and a table "
        "comparison structurally cannot observe it");
    TEST_ASSERT_EQUAL_MESSAGE(CHIP_ENABLE, strobe_pin(last), "Case 17: final entry's pin must be CHIP_ENABLE");
    TEST_ASSERT_EQUAL_MESSAGE(1, strobe_value(last), "Case 17: final entry must be CE deassert (value 1)");

    /* write #3's payload sits at LEN-3 (payload, CE-low, CE-high are the last
     * three entries of any un-elided write) -- derived from the golden's own
     * length rather than a second magic number. No DATA entry may appear at
     * or after this index+1: if a future edit appended a dummy byte write,
     * this loop fails loudly rather than staying silently green. */
    int payload_index = SDP_FIXED_LOCK_DIP28_28C256_LEN - 3;
    for (int i = payload_index + 1; i < strobe_count(); i++) {
        char msg[176];
        snprintf(msg, sizeof(msg),
            "Case 17 (LOCK-05): index %d must not be a DATA entry -- no data write may follow the "
            "third command write's payload (index %d)", i, payload_index);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(STROBE_KIND_DATA, strobe_kind(i), msg);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 18-19 — exact divergence from the unlock and chip-erase streams
 * (LOCK-05 stream half)
 * ───────────────────────────────────────────────────────────────────────── */

/* The lock stream must diverge from the SIX-WRITE UNLOCK stream at an EXACT
 * index -- never `!= -1` (Pitfall 4: a golden pinned to the wrong
 * expectation stays green under a bare not-equal check). Drives the
 * production lock op, snapshots it (mandatory: drive_reference_emitter below
 * calls clear_strobes(), which would erase the lock stream first), then
 * drives the six-write unlock table through the SAME FIXED (remap-aware)
 * emitter the lock op itself uses, for an apples-to-apples index. */
void test_case18_lock_diverges_from_unlock_at_exact_index(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_lock_op(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 18: lock-stream snapshot must not overflow");
    sdp_strobe_t lock_snapshot[64];
    int lock_len = sdp_snapshot(lock_snapshot, 64);

    firestarter_handle_t h_unlock = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive_reference_emitter(&h_unlock, FLASH_DISABLE_WRITE_PROTECTION,
        sizeof(FLASH_DISABLE_WRITE_PROTECTION) / sizeof(FLASH_DISABLE_WRITE_PROTECTION[0]), 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 18: unlock-reference drive must not overflow");

    int div = sdp_first_divergence(lock_snapshot, lock_len);
    TEST_ASSERT_EQUAL_MESSAGE(27, div,
        "Case 18 (LOCK-05 stream half): the lock stream diverges from the SDP-disable unlock stream "
        "at EXACTLY index 27 -- write #3's payload byte (0xA0 in the lock vs 0x80 in the unlock)");
}

/* Same shape as Case 18, comparing against the stream FLASH_ERASE produces
 * through the SAME FIXED emitter. Chip-erase's third payload is ALSO 0x80
 * (identical to the unlock's), so this diverges from the lock at the
 * IDENTICAL index 27 -- FIX-05's one-nibble hazard class (EEPROM_SDP_DISABLE
 * vs FLASH_ERASE differing by one nibble in one byte), now checked for the
 * lock table too. Reads FLASH_ERASE from flash_utils.h (read-only; that file
 * stays FIX-04 frozen). */
void test_case19_lock_diverges_from_chip_erase_at_exact_index(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_lock_op(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 19: lock-stream snapshot must not overflow");
    sdp_strobe_t lock_snapshot[64];
    int lock_len = sdp_snapshot(lock_snapshot, 64);

    firestarter_handle_t h_erase = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive_reference_emitter(&h_erase, FLASH_ERASE, sizeof(FLASH_ERASE) / sizeof(FLASH_ERASE[0]), 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 19: erase-reference drive must not overflow");

    int div = sdp_first_divergence(lock_snapshot, lock_len);
    TEST_ASSERT_EQUAL_MESSAGE(27, div,
        "Case 19 (LOCK-05 stream half, FIX-05's one-nibble hazard class applied to the lock table): "
        "the lock stream diverges from the chip-erase stream at EXACTLY index 27 -- the SAME index as "
        "Case 18's unlock divergence, because chip-erase's third payload (0x80) equals the unlock's, "
        "so the lock's 0xA0 payload diverges from both at the identical position");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 20 — D-12's report shape: both lock ids, ordered, response_code
 * untouched, FLAG_VERBOSE unset, unlock's ids and the skip-WARN absent
 * ───────────────────────────────────────────────────────────────────────── */

/* Firmware-side proof of HOST-05: response_code is deliberately never
 * written on the SDP path (117 D-05 / 118 D-02, permanently enforced by
 * Case 8 above), so an untouched path reports OK -- the honesty therefore
 * cannot live in the status code and lives in MSG_INFO_SDP_LOCK_DONE_US's
 * message text instead, which states both that the sequence was emitted
 * and that the protection state is not readable.
 *
 * Rejected alternatives (D-12), recorded here because this case is their
 * disproof: (1) an unconditional unverifiable-state WARN on every lock --
 * rejected because it warns on a correctly completed operation and trains
 * users to ignore the WARN band; (2) reporting the DQ6 toggle poll's
 * outcome as lock evidence -- rejected because a settled toggle bit proves
 * a write cycle finished, not that protection latched, which is FIX-02's
 * deleted mistake in a new costume (eeprom28c_sdp_lock_execute deliberately
 * never calls eeprom28c_wait_for_sdp_completion at all, per D-11).
 *
 * make_lock_handle sets ctrl_flags = 0, so this case drives with
 * FLAG_VERBOSE NOT set by construction -- pinning the unconditional bare
 * LOG_ID / LOG_ID_U32 spelling (118 D-01) rather than the
 * FLAG_VERBOSE-gated LOG_INFO_ID* family: with the gated family a default
 * `dev sdp enable` would go silent, which is the defect this spelling
 * exists to avoid. */
void test_case20_lock_report_shape_and_response_code(void) {
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]); /* AT28C256, ctrl_flags = 0 */
    drive_lock_op(&h, 0x00);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);

    int lock_idx = -1, done_idx = -1;
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == (uint8_t)MSG_INFO_SDP_LOCK) {
            lock_idx = (int)i;
        }
        if (ids[i] == (uint8_t)MSG_INFO_SDP_LOCK_DONE_US) {
            done_idx = (int)i;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(lock_idx != -1,
        "Case 20 (D-12): MSG_INFO_SDP_LOCK must appear in the captured frame ids, with FLAG_VERBOSE "
        "NOT set -- pinning the unconditional bare LOG_ID spelling (118 D-01)");
    TEST_ASSERT_TRUE_MESSAGE(done_idx != -1,
        "Case 20 (D-12): MSG_INFO_SDP_LOCK_DONE_US must also appear, also with FLAG_VERBOSE NOT set");
    TEST_ASSERT_TRUE_MESSAGE(lock_idx != -1 && done_idx != -1 && lock_idx < done_idx,
        "Case 20 (D-12): MSG_INFO_SDP_LOCK must appear BEFORE MSG_INFO_SDP_LOCK_DONE_US -- the "
        "ordered positions, not just membership, so the 'starting' line cannot follow the "
        "'emitted' line");

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "Case 20 (D-12): handle->response_code must still be RESPONSE_CODE_OK after the lock op -- "
        "see this case's header comment for the full reasoning and the two rejected alternatives");

    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_SDP_UNLOCK),
        "Case 20: the lock path must never emit MSG_INFO_SDP_UNLOCK -- the lock and unlock report "
        "surfaces must not be conflated");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_SDP_UNLOCK_DONE_US),
        "Case 20: the lock path must never emit MSG_INFO_SDP_UNLOCK_DONE_US");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_UNLOCK_SKIPPED),
        "Case 20: the lock path must never emit MSG_WARN_SDP_UNLOCK_SKIPPED -- that id belongs "
        "only to the unlock's skip path");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 21-22 — D-14's t_BLC budget WARN: fires over budget, does NOT fire
 * at a normal elapsed time (the anti-hollow pair)
 * ───────────────────────────────────────────────────────────────────────── */

/* Case 21: the budget WARN fires. Mirrors Case 11's shape for the six-write
 * unlock budget, but for the three-write lock. AT28C_TBLC_MAX_US (100) is
 * #define'd inside eeprom_28c.cpp's own translation unit (eeprom_28c.cpp:58)
 * and is NOT exported via eeprom_28c.h, so it is mirrored here as a named
 * local constant with an explicit citation -- the budget is computed as
 * three times that value from the sequence length, never as a literal 300. */
void test_case21_lock_tblc_budget_warn_fires(void) {
    const uint32_t TEST_MIRROR_AT28C_TBLC_MAX_US = 100; /* mirrors eeprom_28c.cpp:58 */
    const uint32_t lock_seq_len = 3;                    /* EEPROM_SDP_ENABLE's length */
    uint32_t lock_budget_us = lock_seq_len * TEST_MIRROR_AT28C_TBLC_MAX_US; /* 300 us */
    uint32_t over_budget_elapsed = lock_budget_us + 1;

    /* eeprom28c_sdp_lock_execute (via eeprom28c_emit_sdp_sequence_timed) calls
     * micros() exactly twice per drive: once immediately before
     * eeprom28c_emit_command_sequence, once immediately after. Script exactly
     * those two ticks so the difference exceeds the budget. */
    sdp_script_micros({0, over_budget_elapsed});
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]);
    drive_lock_op(&h, 0x00);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED),
        "Case 21 (D-14): an over-budget elapsed value must make MSG_WARN_SDP_TBLC_EXCEEDED appear "
        "in the captured frame ids -- F-118-01 measured 572 us against the unlock's 600 us budget "
        "(only 4.7% headroom) on real hardware, so the lock's 300 us budget is genuinely "
        "load-bearing at n=3, not a latent invariant that never fires");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "Case 21 (117 D-05 / 118 D-02): the budget WARN must never write handle->response_code -- "
        "severity lives in the message id's band alone");
}

/* Case 22: the anti-hollow control. A check that always fires and a check
 * that never fires are BOTH hollow, so D-14 needs the pair: this case
 * scripts ticks comfortably inside the 300 us budget and asserts the WARN
 * id is absent WHILE both lock INFO ids are present -- the presence half is
 * load-bearing, since without it this case would pass vacuously if nothing
 * were captured at all. F-118-01 measured the real unlock timing at ~95 us
 * per byte against a 100 us per-byte datasheet maximum, so the lock's
 * 300 us budget lands near ~286 us on real hardware -- a synthetic in-budget
 * value is used here since native host timing is not representative of AVR
 * cycles, but the real-hardware margin is why this check is genuinely
 * load-bearing rather than a latent invariant. */
void test_case22_lock_tblc_budget_warn_does_not_fire_at_normal_elapsed(void) {
    sdp_script_micros({0, 50}); /* comfortably inside the 300 us budget */
    firestarter_handle_t h = make_lock_handle(SDP_BUS_CONFIGS[0]);
    drive_lock_op(&h, 0x00);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED),
        "Case 22 (D-14 anti-hollow control): with a normal (in-budget) elapsed value, "
        "MSG_WARN_SDP_TBLC_EXCEEDED must NOT appear");
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_SDP_LOCK),
        "Case 22: MSG_INFO_SDP_LOCK must still be present -- proves frames WERE captured, so the "
        "WARN's absence is meaningful rather than vacuous");
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_SDP_LOCK_DONE_US),
        "Case 22: MSG_INFO_SDP_LOCK_DONE_US must also be present, for the same reason");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Case 23 — the standalone unlock's stream is byte-identical to the
 * auto-unlock's (D-13's "reads identically however it was triggered")
 * ───────────────────────────────────────────────────────────────────────── */

/* Drives the standalone CMD_SDP_UNLOCK op directly (h.firestarter_operation_main,
 * since init/end are NULL for this cmd per LOCK-02), snapshotting its stream
 * (mandatory: the next drive's clear_strobes() would otherwise erase it),
 * then drives eeprom28c_write_init on a fresh handle with FLAG_SKIP_SDP_UNLOCK
 * clear (the auto-unlock path) and asserts exact-index (-1) divergence
 * against the snapshot. This holds because both paths call the SAME shared
 * eeprom28c_emit_sdp_sequence_timed helper with the SAME table
 * (EEPROM_SDP_DISABLE) and the SAME completion wait
 * (eeprom28c_wait_for_sdp_completion) -- D-13's reused-ids decision made
 * assertable, not merely asserted.
 *
 * firestarter_get_data is reassigned to mock_get_data_keyed for the
 * standalone drive, mirroring drive_write_init's own override above: the
 * standalone op does NOT go through drive_write_init (it drives `main`
 * directly, since init/end are NULL), so without this override the
 * completion poll's reads would hit the real memory_get_data and inject
 * extra recorded strobes that the auto-unlock path (which DOES override
 * get_data via drive_write_init) never contributes -- breaking the
 * byte-identity this case exists to prove. */
void test_case23_standalone_unlock_matches_auto_unlock_stream(void) {
    firestarter_handle_t h_unlock = {};
    h_unlock.protocol = 0x0D;
    h_unlock.cmd = CMD_SDP_UNLOCK;
    h_unlock.response_code = RESPONSE_CODE_OK;
    h_unlock.chip_id = 0;
    h_unlock.mem_size = SDP_BUS_CONFIGS[0].mem_size;
    h_unlock.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h_unlock.ctrl_flags = 0;
    configure_memory(&h_unlock);
    h_unlock.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h_unlock.firestarter_operation_main(&h_unlock);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 23: standalone-unlock drive must not overflow");

    sdp_strobe_t unlock_snapshot[64];
    int unlock_len = sdp_snapshot(unlock_snapshot, 64);

    /* Secondary assertion (D-13's reused-ids decision made explicit) --
     * captured BEFORE the second drive/clear below, since captured_frames is
     * cleared per-case in setUp() only, not between the two drives here. */
    std::vector<uint8_t> unlock_ids;
    sdp_captured_frame_ids(&unlock_ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(unlock_ids, (uint8_t)MSG_INFO_SDP_UNLOCK),
        "Case 23: the standalone unlock's captured frame ids must contain MSG_INFO_SDP_UNLOCK");
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(unlock_ids, (uint8_t)MSG_INFO_SDP_UNLOCK_DONE_US),
        "Case 23: the standalone unlock's captured frame ids must contain MSG_INFO_SDP_UNLOCK_DONE_US");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(unlock_ids, (uint8_t)MSG_INFO_SDP_LOCK),
        "Case 23: the standalone unlock must never emit MSG_INFO_SDP_LOCK");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(unlock_ids, (uint8_t)MSG_INFO_SDP_LOCK_DONE_US),
        "Case 23: the standalone unlock must never emit MSG_INFO_SDP_LOCK_DONE_US");

    /* Clear captured_frames before the second drive so nothing from the
     * standalone drive leaks into a future extension of this case. */
    captured_frames.clear();

    /* The auto-unlock path: SAME row, flag absent (default extra_flags == 0
     * means FLAG_SKIP_SDP_UNLOCK is clear), via drive_write_init -- this is
     * the identical handle factory + drive helper Case 10 uses. */
    firestarter_handle_t h_auto = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive_write_init(&h_auto, 0x00);

    /* The stream comparison is the PRIMARY assertion for this case. */
    TEST_ASSERT_EQUAL_MESSAGE(-1, sdp_first_divergence(unlock_snapshot, unlock_len),
        "Case 23 (D-13): the standalone unlock's stream must be element-wise IDENTICAL to the "
        "auto-unlock's stream -- both call the same shared helper with the same table and the "
        "same completion wait");
    sdp_assert_stream_equals(unlock_snapshot, unlock_len,
        "Case 23 (D-13): standalone unlock == auto-unlock (byte-identical streams)");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 24-25 (v1.22 Phase 119 D-06/D-07, 119-07 Task 3, option (a)) — the
 * refusal FRAME itself, proven at the op layer, plus DEVTEST-01's firmware
 * half proven end to end through the CMD_ERASE dispatch path on 0x0D. Both
 * are possible only because Task 1 widened build_src_filter with
 * operation_utils.cpp -- under option (b) neither case would exist and the
 * matrix would be prose.
 * ───────────────────────────────────────────────────────────────────────── */

/* Case 24: a handle whose firestarter_operation_main is NULL (no dispatch
 * involved -- this proves the REFUSAL itself, not any one handler's
 * omission), driven through the REAL op_execute_stateful_operation, exactly
 * as every eprom_* entry point does
 * (`return !op_execute_stateful_operation(callback, handle)`). Passing NULL
 * for the callback parameter is safe: the NULL-main guard at
 * operation_utils.cpp:63 short-circuits before the callback is ever
 * touched. */
void test_case24_null_main_refusal_emits_not_supported_and_error_response(void) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_ERASE; /* any command -- the refusal cares only about main */
    h.response_code = RESPONSE_CODE_OK;
    h.firestarter_operation_main = NULL;
    h.firestarter_operation_init = NULL;
    h.firestarter_operation_end = NULL;

    bool still_in_progress = op_execute_stateful_operation(NULL, &h);

    TEST_ASSERT_FALSE_MESSAGE(still_in_progress,
        "Case 24 (D-06/D-07): op_execute_stateful_operation must return false on a NULL main -- "
        "every eprom_* caller inverts this return to report the command as finished, unchanged "
        "semantics from before this task");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 24 (D-06/D-07): the NULL-main fall-through must now set RESPONSE_CODE_ERROR, "
        "replacing the pre-119-07 silent RESPONSE_CODE_OK phantom success");

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_ERR_NOT_SUPPORTED),
        "Case 24 (D-06/D-07): MSG_ERR_NOT_SUPPORTED must appear in the captured frame ids -- the "
        "generic refusal reuses this existing id (already eprom_erase's FLAG_CAN_ERASE refusal); no "
        "new catalog id was added");
}

/* Case 25 helper -- deviation from the plan's literal drive shape, discovered
 * mid-task (Rule 1/3: the plan's "keep the op_execute_simple_operation drive
 * as-is" instruction crashed on contact with the REAL state machine once
 * main stopped being NULL; see the case comment below for the full trace).
 *
 * A continuous virtual "OK" byte stream feeding op_wait_for_ack's internal
 * ACK poll (op_get_message -> rurp_communication_available/peak/read, which
 * route through the REAL src/boards/rurp_serial_utils.cpp -> Serial, pulled
 * into this suite's link by Phase 6's [env:native] widening). LOAD-BEARING:
 * this suite's setUp() mocks millis() to a constant 0 and delay() to a
 * no-op (both required elsewhere in this file, e.g. Case 8's completion
 * poll) -- so an op_wait_for_ack() call that is NOT fed an immediate ACK
 * busy-loops forever rather than timing out, because `millis() < timeout`
 * can never become false. Feeding 'O','K' resolves every ACK wait on its
 * very first poll, so that busy-loop is never entered. */
static size_t s_case25_ack_pos;
static int case25_serial_available() { return 2; }
static int case25_serial_peek() {
    static const uint8_t pattern[2] = {'O', 'K'};
    return (int)pattern[s_case25_ack_pos % 2];
}
static int case25_serial_read() {
    static const uint8_t pattern[2] = {'O', 'K'};
    int b = (int)pattern[s_case25_ack_pos % 2];
    s_case25_ack_pos++;
    return b;
}

/* Case 25 -- MANDATORY INVERSION (Phase 153 / ERASE-03). Recorded in the
 * reversal-record voice this project uses: mechanism-corrected,
 * intent-satisfied -- never as failed. `configure_eeprom28c` now carries a
 * `case CMD_ERASE:` arm (Phase 153 plan 03) assigning `eeprom28c_erase_execute`,
 * so this cell has LEFT Phase 119 D-06's op-layer NULL-main guard's coverage
 * -- the same guard test_case24 above still proves, generically, for every
 * cell that remains NULL. All four beats stay POSITIVE.
 *
 * Switched the handle factory from make_lock_handle to make_erase_handle
 * (defined above, alongside drive_erase_op) and reassigned
 * firestarter_get_data to mock_get_data_keyed before driving: this case now
 * drives the REAL eeprom28c_erase_execute through op_execute_simple_operation,
 * whose SDP-disable prefix polls via handle->firestarter_get_data --
 * left as the real memory_get_data, that poll's read iterations would each
 * latch a fresh address and could overflow the 512-entry strobe recorder
 * before this case's own assertions run (drive_erase_op's own comment states
 * this same reason in full; this case does not use that helper directly
 * because op_execute_simple_operation, not h.firestarter_operation_main, is
 * the call under test here -- DEVTEST-01's op-layer entry point). This case
 * still calls op_execute_simple_operation directly, not eprom_erase
 * (src/eprom_operations.cpp, an AVR-only TU excluded from [env:native]'s
 * build_src_filter) -- the exact op-layer function eprom_erase's body
 * delegates to (`return !op_execute_simple_operation(handle);`),
 * deliberately bypassing eprom_erase's own EARLIER FLAG_CAN_ERASE
 * precondition check (a different, unrelated refusal) so this case isolates
 * ERASE-03's dispatch arm alone.
 *
 * DEVIATION (discovered running this case, Rule 1/3): with main non-NULL,
 * op_execute_stateful_operation no longer short-circuits at the top -- it
 * enters the REAL INIT/MAIN/END housekeeping state machine
 * (operation_utils.cpp), which gates every phase transition behind
 * op_wait_for_ack(). A single call therefore no longer completes the
 * operation (it did, trivially, when main was NULL and the guard fired
 * immediately): reaching completion needs FOUR calls to
 * op_execute_simple_operation (INIT-start ack, MAIN-start ack + the actual
 * erase run, END-start ack, and the final ack that flips
 * is_all_operations_done()'s message check), each consuming one ACK. The
 * loop below drives exactly that, bounded well above the deterministic
 * count so a shape change fails loudly rather than hanging. */
void test_case25_cmd_erase_on_0x0d_dispatches_and_succeeds_erase03(void) {
    firestarter_handle_t h = make_erase_handle(SDP_BUS_CONFIGS[0]); /* protocol 0x0D, ctrl_flags 0 */
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_main,
        "Case 25 precondition (ERASE-03): configure_eeprom28c now carries a case CMD_ERASE: arm "
        "assigning eeprom28c_erase_execute, so main must be non-NULL on 0x0D");
    h.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();

    s_case25_ack_pos = 0;
    When(Method(ArduinoFake(Serial), available)).AlwaysDo(case25_serial_available);
    When(Method(ArduinoFake(Serial), peek)).AlwaysDo(case25_serial_peek);
    When(Method(ArduinoFake(Serial), read)).AlwaysDo(case25_serial_read);

    bool still_in_progress = true;
    int calls = 0;
    const int MAX_CALLS = 10; /* deterministic trace needs exactly 4; generous margin, not an escape hatch */
    while (still_in_progress && calls < MAX_CALLS) {
        still_in_progress = op_execute_simple_operation(&h);
        calls++;
    }

    TEST_ASSERT_FALSE_MESSAGE(still_in_progress,
        "Case 25 (ERASE-03, mechanism-corrected/intent-satisfied -- never as failed): "
        "op_execute_simple_operation must reach completion (false) within MAX_CALLS iterations of "
        "the real ACK-gated INIT/MAIN/END state machine -- eprom_erase reports the erase as "
        "finished, the same call-site contract as before this task, now honestly (the erase actually "
        "ran instead of silently doing nothing)");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "Case 25 (ERASE-03): CMD_ERASE on 0x0D must now report RESPONSE_CODE_OK -- the new dispatch "
        "arm routes to a real operation instead of leaving main NULL for the generic op-layer "
        "refusal");

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_ERR_NOT_SUPPORTED),
        "Case 25 (ERASE-03): MSG_ERR_NOT_SUPPORTED must NOT appear in the captured frame ids for a "
        "CMD_ERASE attempt on protocol 0x0D -- an unexpected refusal here is exactly the regression "
        "this leg now guards against, kept as a negative-presence check rather than deleted");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 26-29 (Plan 119-08, D-16) — the page-load worst-per-byte-interval
 * report: fires with the correct decoded value on a completing multi-page
 * write, fires on the aborting exit the bench actually takes, adds no
 * budget WARN, and leaves response_code untouched.
 *
 * These cases drive PRODUCTION eeprom28c_write_execute directly via
 * h.firestarter_operation_main(&h) after configure_memory (CMD_WRITE's main
 * on 0x0D), never a hand-rolled copy of the loop.
 * ───────────────────────────────────────────────────────────────────────── */

/* Handle factory for driving eeprom28c_write_execute. Mirrors
 * test_val_eeprom28c.cpp's make_write_handle shape (same suite family,
 * separate TU, no shared linkage -- that file's helper is static). Every
 * data_buffer byte is (0x10 + k) mod 0x40, so every byte value stays under
 * 0x80 (DQ7 clear) -- this is what makes mock_get_data_page_load_always_wrong's
 * fixed 0xFF (DQ7 set) reliably mismatch every intended byte, below. */
static firestarter_handle_t make_page_load_handle(uint32_t address, uint32_t data_size) {
    firestarter_handle_t h = {};
    h.protocol = 0x0D;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.mem_size = SDP_BUS_CONFIGS[0].mem_size;
    h.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK;
    h.address = address;
    h.data_size = data_size;
    for (uint32_t k = 0; k < data_size; k++) {
        h.data_buffer[k] = (char)(0x10 + (k % 0x40));
    }
    return h;
}

/* Always-succeeds mock: returns exactly the byte the write intended for that
 * address (derived from h->address and h->data_buffer), so both
 * eeprom28c_wait_for_page_write's DQ7-complement poll and
 * eeprom28c_verify_page_readback's whole-byte compare see the expected value
 * on the FIRST read at every flush window -- the driven write completes
 * normally, all the way to the loop's normal exit. */
static uint8_t mock_get_data_page_load_ok(firestarter_handle_t* h, uint32_t address) {
    uint32_t k = address - h->address;
    return (uint8_t)h->data_buffer[k];
}

/* Always-fails mock: a fixed 0xFF (DQ7 set) never matches any byte this
 * suite's data pattern produces (all < 0x80, DQ7 clear -- see
 * make_page_load_handle above), so eeprom28c_wait_for_page_write times out
 * on the FIRST flush window it polls. This IS Plan 119-11's bench
 * condition: an EMPTY SOCKET reads back a fixed idle level that never
 * matches an intended write, so the first page's write poll always fails
 * -- exactly the condition under which gh#11's symptom appears. */
static uint8_t mock_get_data_page_load_always_wrong(firestarter_handle_t*, uint32_t) {
    return 0xFF;
}

/* Case 26 -- the report line fires on a completing write, with the correct
 * worst value. Two pages (data_size 72, AT28C_PAGE_SIZE_FALLBACK 64: one flush at the
 * page-64 boundary, one at the last byte), so the flush path runs more than
 * once. The scripted tick queue is deliberately NON-MONOTONIC with its
 * largest gap at byte index 40 -- neither the first byte (0) nor the last
 * (71) -- so this case proves the tracker keeps a RUNNING MAXIMUM rather
 * than reporting the first or the last interval. Every micros() read the
 * driven path makes is scripted (1 seed + 72 in-loop = 73 entries): an
 * under-scripted case would silently measure the documented TAIL value for
 * every read past the script's end, which reads as "a real measurement" but
 * is actually just the tail repeated. */
void test_case26_write_execute_reports_worst_interval_on_completing_write(void) {
    const size_t   data_size = 72; /* > AT28C_PAGE_SIZE_FALLBACK (64): two flush windows */
    const size_t   spike_after_byte = 40; /* the (spike_after_byte+1)-th byte's load -- deliberately mid-write */
    const uint32_t spike_us = 77;

    std::vector<uint32_t> ticks;
    ticks.reserve(data_size + 1);
    uint32_t running = 0;
    ticks.push_back(running); /* seed, read immediately before the loop */
    for (size_t i = 0; i < data_size; i++) {
        running += (i == spike_after_byte) ? spike_us : 1;
        ticks.push_back(running); /* one entry per firestarter_set_data call */
    }
    sdp_script_micros(ticks);

    firestarter_handle_t h = make_page_load_handle(0, (uint32_t)data_size);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_page_load_ok;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 26: a clean two-page write with every read matching its intended byte must not "
        "report ERROR");

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_PAGE_LOAD_WORST_US),
        "Case 26 (D-16): MSG_INFO_PAGE_LOAD_WORST_US must appear in the captured frame ids after a "
        "completing write");

    uint32_t decoded = 0;
    bool found = sdp_decode_u32_param_for_id((uint8_t)MSG_INFO_PAGE_LOAD_WORST_US, &decoded);
    TEST_ASSERT_TRUE_MESSAGE(found, "Case 26: MSG_INFO_PAGE_LOAD_WORST_US frame must be decodable");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spike_us, decoded,
        "Case 26 (T-119-08-VACUOUSVALUE): the DECODED worst-interval parameter must equal the "
        "scripted maximum (77 us, at byte index 40 -- neither the first nor the last byte) -- "
        "membership alone would pass a tracker that reported zero or the first/last interval "
        "instead of the running maximum");
}

/* Case 27 -- the report line fires on the ABORTING exit. Same two-page
 * geometry as Case 26, but mock_get_data_page_load_always_wrong makes the
 * FIRST page's write poll fail, so the loop takes the early exit (via the
 * flagged break -- D-16's single-exit restructure) after loading only the
 * first page's 64 bytes; the second page (bytes 64-71) is never reached.
 * This is the case that makes Plan 119-11's bench run meaningful: with an
 * EMPTY SOCKET the first page always fails, and it is also the condition
 * under which gh#11's symptom appears -- so a report unreachable on the
 * abort path would have measured nothing where it matters most. Only the
 * first 64 bytes' ticks are scripted (1 seed + 64 in-loop = 65 entries); a
 * huge tail value is installed and must NEVER be observed in the decoded
 * result, since the loop must abort before it is ever read. */
void test_case27_write_execute_reports_worst_interval_on_aborting_write(void) {
    const size_t   loaded_before_abort = 64; /* AT28C_PAGE_SIZE_FALLBACK -- the first page, in full */
    const size_t   spike_after_byte = 30;    /* mid-first-page, not first/last of the loaded range */
    const uint32_t spike_us = 55;
    const uint32_t never_reached_tail = 999999999u;

    std::vector<uint32_t> ticks;
    ticks.reserve(loaded_before_abort + 1);
    uint32_t running = 0;
    ticks.push_back(running);
    for (size_t i = 0; i < loaded_before_abort; i++) {
        running += (i == spike_after_byte) ? spike_us : 1;
        ticks.push_back(running);
    }
    sdp_script_micros(ticks, never_reached_tail);

    firestarter_handle_t h = make_page_load_handle(0, 72); /* two pages -- second must never be reached */
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_page_load_always_wrong;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "Case 27: the first page's write poll must fail against the always-wrong mock, aborting "
        "the write");

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_PAGE_LOAD_WORST_US),
        "Case 27 (T-119-08-UNREACHABLE): MSG_INFO_PAGE_LOAD_WORST_US must still appear even though "
        "the write aborted on its first page -- the single-exit restructure is what makes this "
        "reachable");
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_ERR_EEPROM_TIMEOUT),
        "Case 27: the page-write failure's own error id (MSG_ERR_EEPROM_TIMEOUT) must also appear");

    uint32_t decoded = 0;
    bool found = sdp_decode_u32_param_for_id((uint8_t)MSG_INFO_PAGE_LOAD_WORST_US, &decoded);
    TEST_ASSERT_TRUE_MESSAGE(found, "Case 27: MSG_INFO_PAGE_LOAD_WORST_US frame must be decodable");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(spike_us, decoded,
        "Case 27: the reported worst value must correspond to the 64 bytes actually loaded before "
        "the abort (scripted max 55 us) -- never the installed 999999999 tail, which the loop must "
        "never reach because it aborts first");
}

/* Case 28 -- no budget WARN on this path, even at an interval far above
 * AT28C_TBLC_MAX_US (100). D-16 deliberately declines a runtime compare in
 * the hot per-byte path, preserving 118's D-10; this case pins that
 * declination so a future editor who adds one sees it fail here and has to
 * make the decision deliberately. The presence half (MSG_INFO_PAGE_LOAD_WORST_US)
 * is load-bearing so this case cannot pass vacuously against a driven path
 * that captured no frames at all. */
void test_case28_write_execute_no_tblc_budget_warn(void) {
    const uint32_t over_budget_interval_us = 1000; /* >> AT28C_TBLC_MAX_US (100) */
    sdp_script_micros({0, over_budget_interval_us});

    firestarter_handle_t h = make_page_load_handle(0, 1); /* single byte -- one flush at last_byte */
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_page_load_ok;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_main(&h);

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_SDP_TBLC_EXCEEDED),
        "Case 28 (T-119-08-DECLINEDWARN): MSG_WARN_SDP_TBLC_EXCEEDED must NOT appear on the "
        "page-load path, even at a scripted interval (1000 us) far above AT28C_TBLC_MAX_US (100) "
        "-- D-16 declines a runtime budget compare in this hot path");
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_PAGE_LOAD_WORST_US),
        "Case 28: MSG_INFO_PAGE_LOAD_WORST_US must still be present -- proves frames WERE captured, "
        "so the WARN's absence is meaningful rather than vacuous");
}

/* Case 29 -- response_code is untouched by the report line. Mirrors
 * test_case8_completion_poll_preserves_prior_severity's invariant onto this
 * new emission: a prior severity must survive a completing write's report
 * line exactly as it survives the completion poll. */
void test_case29_write_execute_report_preserves_response_code(void) {
    firestarter_handle_t h = make_page_load_handle(0, 4); /* single flush window, well inside one page */
    h.response_code = RESPONSE_CODE_WARNING;
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_page_load_ok;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "Case 29: the page-load worst-interval report must never overwrite a prior response_code, "
        "even on a cleanly completing write");

    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_INFO_PAGE_LOAD_WORST_US),
        "Case 29: MSG_INFO_PAGE_LOAD_WORST_US must still be present -- proves the report ran, so "
        "the response_code check above is meaningful rather than vacuous");
}

/* Case 30 -- ERASE-01 / 152-CONTEXT.md D-07. Built from
 * make_sdp_handle_blank_check_enabled (the ONLY factory in this suite that
 * leaves the blank-check axis live), this case proves that a write-INIT
 * driven with FLAG_SKIP_BLANK_CHECK CLEAR is byte-identical in behavior to
 * every other case here (which all drive with the flag SET): no blank-check
 * progress allocation, no multi-call INIT loop, and the exact same golden
 * stream. `mem_util_blank_check` is the ONLY setter of
 * is_operation_in_progress on this path (memory.cpp:401-425), so a FALSE
 * result below is the single-shot-INIT proof, not an assumption. */
void test_case30_write_init_no_blank_check_with_flag_clear_erase01(void) {
    firestarter_handle_t h = make_sdp_handle_blank_check_enabled(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_write_init(&h, 0x00);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "Case 30 (ERASE-01): is_operation_in_progress must be FALSE after exactly one "
        "eeprom28c_write_init call with FLAG_SKIP_BLANK_CHECK clear -- mem_util_blank_check is "
        "the only setter of this flag on the write-INIT path, so TRUE here would mean the "
        "pre-write blank check still ran and left a multi-call INIT loop pending");
    /* The companion `h.progress_data must be NULL` assertion is GONE, and so is
     * the field: mem_util_blank_check no longer mallocs a
     * blank_check_progress_data_t (it keeps its saved address in a file-scope
     * static), so there is no allocation left to observe. This is a loss of a
     * redundant PROBE, not of coverage -- is_operation_in_progress above is set
     * by the same statement of the same function that used to do the malloc, so
     * the behaviour under test is still pinned. */
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN,
        "Case 30 (ERASE-01): with FLAG_SKIP_BLANK_CHECK clear, the AT28C256/DIP28_28C256 stream "
        "must now be byte-identical to the golden captured with the flag SET -- the D-07 policy "
        "expressed as a stream identity: a pre-write blank check contributes zero strobes "
        "whether or not the caller asks to skip it");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Cases 31-33 (Phase 153 / ERASE-04) -- pinning eeprom28c_erase_execute's
 * emitted stream against the tree at head, tail and divergence, per
 * D-153-01's binding gate on the plan-03 inline literals.
 * ───────────────────────────────────────────────────────────────────────── */

/* Case 31 -- the erase stream's HEAD is the SDP-disable prefix, positionally,
 * over the golden's full length -- D-153-02's prefix is emitted verbatim
 * before any erase-specific write happens. Then asserts the stream
 * continues PAST the prefix: an equality at exactly this length would mean
 * the erase emitted only the prefix and nothing else -- D-153-02's prefix
 * WITHOUT D-153-02's erase, the exact silent-half-feature this case guards
 * against. */
void test_case31_erase_stream_head_equals_sdp_disable_golden_erase04(void) {
    firestarter_handle_t h = make_erase_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 / DIP28_28C256 */
    drive_erase_op(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 31: erase stream must not overflow");

    for (int i = 0; i < SDP_FIXED_DIP28_28C256_LEN; i++) {
        char msg[192];
        snprintf(msg, sizeof(msg),
            "Case 31 (ERASE-04): erase stream index %d must equal SDP_FIXED_DIP28_28C256's entry "
            "at the same index -- eeprom28c_erase_execute's first bus-visible action is the "
            "D-153-02 SDP-disable prefix, emitted verbatim", i);
        TEST_ASSERT_EQUAL_MESSAGE(SDP_FIXED_DIP28_28C256[i].kind, strobe_kind(i), msg);
        TEST_ASSERT_EQUAL_MESSAGE(SDP_FIXED_DIP28_28C256[i].pin, strobe_pin(i), msg);
        TEST_ASSERT_EQUAL_MESSAGE(SDP_FIXED_DIP28_28C256[i].value, strobe_value(i), msg);
    }

    TEST_ASSERT_TRUE_MESSAGE(strobe_count() > SDP_FIXED_DIP28_28C256_LEN,
        "Case 31 (ERASE-04): the erase stream must continue PAST the SDP-disable prefix's length "
        "-- the six AN-0544B chip-erase writes follow it; equality at exactly this length would "
        "mean the erase emitted only the prefix and nothing else, D-153-02's prefix WITHOUT "
        "D-153-02's erase");
}

/* Case 32 -- the erase stream's TAIL is the chip-erase terminal byte, not
 * the SDP-disable terminal byte -- FIX-05's one-nibble hazard class
 * (EEPROM_SDP_DISABLE 0x20 vs FLASH_ERASE 0x10 at the same table position),
 * checked on the erase op's own emitted stream. Modelled on Case 17's
 * derivation: payload, CE-low, CE-high are the last three entries of any
 * un-elided write, and the erase's own final write (chip-erase's sixth,
 * address 0x5555 differing from write #5's 0x2AAA) is never elided. */
void test_case32_erase_stream_terminates_on_chip_erase_byte_erase04(void) {
    firestarter_handle_t h = make_erase_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 / DIP28_28C256 */
    drive_erase_op(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 32: erase stream must not overflow");

    int payload_index = strobe_count() - 3;
    TEST_ASSERT_EQUAL_MESSAGE(STROBE_KIND_DATA, strobe_kind(payload_index),
        "Case 32 (ERASE-04): the entry three-before-the-end must be a DATA payload write -- the "
        "erase's terminal command write, per Case 17's un-elided-write derivation");

    size_t erase_len = sizeof(FLASH_ERASE) / sizeof(FLASH_ERASE[0]);
    uint8_t erase_terminal_byte = FLASH_ERASE[erase_len - 1].byte;
    size_t sdp_disable_len = sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]);
    uint8_t sdp_disable_terminal_byte = EEPROM_SDP_DISABLE[sdp_disable_len - 1].byte;

    TEST_ASSERT_EQUAL_MESSAGE(erase_terminal_byte, strobe_value(payload_index),
        "Case 32 (ERASE-04): the terminal command payload must equal FLASH_ERASE's own last "
        "entry's byte, read from flash_utils.h (FIX-04 frozen) -- never a retyped literal");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(sdp_disable_terminal_byte, strobe_value(payload_index),
        "Case 32 (ERASE-04, FIX-05's one-nibble hazard class): the terminal command payload must "
        "NOT equal EEPROM_SDP_DISABLE's own last entry's byte -- the chip-erase code (0x10) and "
        "the SDP-disable code (0x20) differ by exactly one nibble in this position, and a stream "
        "that accidentally emitted the SDP-disable byte again here would still look plausible "
        "without this check");

    for (int i = payload_index + 1; i < strobe_count(); i++) {
        char msg[192];
        snprintf(msg, sizeof(msg),
            "Case 32 (ERASE-04): index %d must not be a DATA entry -- no data write may follow the "
            "erase's terminal command payload (index %d)", i, payload_index);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(STROBE_KIND_DATA, strobe_kind(i), msg);
    }
}

/* Case 33 -- the erase op's full stream (SDP-disable prefix immediately
 * followed by the six chip-erase writes) diverges from a BARE chip-erase-
 * only reference stream at an EXACT index, never `!= -1` (the same rule
 * Cases 18/19 exist for -- a not-equal check stays green against a golden
 * pinned to the wrong expectation). The SDP-disable and chip-erase AN
 * sequences share IDENTICAL addresses and payload bytes for their first
 * five writes (0x5555/0xAA, 0x2AAA/0x55, 0x5555/0x80, [elided]/0xAA,
 * 0x2AAA/0x55) and differ ONLY in the sixth write's terminal payload byte
 * (0x20 vs 0x10) -- so the erase op's own prefix, walked from index 0
 * against the bare reference, must diverge at exactly that sixth write's
 * payload position: SDP_FIXED_DIP28_28C256_LEN - 3, the same "payload is
 * three-before-the-end of the golden" derivation Case 17/32 use, expressed
 * against the golden's own length rather than a magic number. Mandatory
 * sequencing note from Case 18's own comment: drive_reference_emitter calls
 * clear_strobes(), so the production op's stream is snapshotted FIRST. */
void test_case33_erase_stream_diverges_from_bare_chip_erase_at_exact_index_erase04(void) {
    firestarter_handle_t h = make_erase_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 / DIP28_28C256 */
    drive_erase_op(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 33: erase-stream snapshot must not overflow");
    sdp_strobe_t erase_snapshot[128]; /* the erase stream is roughly twice a six-write stream; 64 (Case 18/19's size) is not enough */
    int erase_len = sdp_snapshot(erase_snapshot, 128);

    firestarter_handle_t h_bare = make_sdp_handle(SDP_BUS_CONFIGS[0]);
    drive_reference_emitter(&h_bare, FLASH_ERASE, sizeof(FLASH_ERASE) / sizeof(FLASH_ERASE[0]), 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "Case 33: bare chip-erase reference drive must not overflow");

    int div = sdp_first_divergence(erase_snapshot, erase_len);
    int expected_div = SDP_FIXED_DIP28_28C256_LEN - 3;
    TEST_ASSERT_EQUAL_MESSAGE(expected_div, div,
        "Case 33 (ERASE-04): the erase op's full stream must diverge from a BARE chip-erase-only "
        "reference stream at EXACTLY SDP_FIXED_DIP28_28C256_LEN - 3 -- the SDP-disable prefix and "
        "the bare chip-erase reference share the first five writes' addresses and payloads "
        "byte-for-byte, diverging only at the sixth write's terminal payload (0x20 in the erase's "
        "prefix vs 0x10 in the bare reference) -- never `!= -1`, the same reason Cases 18 and 19 "
        "assert exact indices");
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_case1_at28c256_stream_matches_fixed);
    RUN_TEST(test_case2_at28c64_stream_matches_fixed);
    RUN_TEST(test_case3_at28c16_stream_matches_fixed);
    RUN_TEST(test_case4_at28c010_stale_direct_seed);
    RUN_TEST(test_case5_at28c040_stale_via_real_read);
    RUN_TEST(test_case6_matching_chip_id_proceeds);
    RUN_TEST(test_case7_mismatching_chip_id_with_force_warns);
    RUN_TEST(test_case8_completion_poll_preserves_prior_severity);
    RUN_TEST(test_case9_skip_flag_suppresses_unlock_stream);
    RUN_TEST(test_case10_flag_absent_emits_full_unlock_stream);
    RUN_TEST(test_case11_tblc_budget_exceeded_warns);
    RUN_TEST(test_case12_flag_absent_emits_exactly_two_report_frames);
    RUN_TEST(test_case13_lock_dip28_28c256_stream_matches_fixed);
    RUN_TEST(test_case14_lock_dip28_28c64_stream_matches_fixed);
    RUN_TEST(test_case15_lock_dip24_2816_stream_matches_fixed);
    RUN_TEST(test_case16_lock_dip32_28c512_eeprom_stream_matches_fixed_stale_seed);
    RUN_TEST(test_case17_lock_terminates_after_three_writes_no_trailing_data);
    RUN_TEST(test_case18_lock_diverges_from_unlock_at_exact_index);
    RUN_TEST(test_case19_lock_diverges_from_chip_erase_at_exact_index);
    RUN_TEST(test_case20_lock_report_shape_and_response_code);
    RUN_TEST(test_case21_lock_tblc_budget_warn_fires);
    RUN_TEST(test_case22_lock_tblc_budget_warn_does_not_fire_at_normal_elapsed);
    RUN_TEST(test_case23_standalone_unlock_matches_auto_unlock_stream);
    RUN_TEST(test_case24_null_main_refusal_emits_not_supported_and_error_response);
    RUN_TEST(test_case25_cmd_erase_on_0x0d_dispatches_and_succeeds_erase03);
    RUN_TEST(test_case26_write_execute_reports_worst_interval_on_completing_write);
    RUN_TEST(test_case27_write_execute_reports_worst_interval_on_aborting_write);
    RUN_TEST(test_case28_write_execute_no_tblc_budget_warn);
    RUN_TEST(test_case29_write_execute_report_preserves_response_code);
    RUN_TEST(test_case30_write_init_no_blank_check_with_flag_clear_erase01);
    RUN_TEST(test_case31_erase_stream_head_equals_sdp_disable_golden_erase04);
    RUN_TEST(test_case32_erase_stream_terminates_on_chip_erase_byte_erase04);
    RUN_TEST(test_case33_erase_stream_diverges_from_bare_chip_erase_at_exact_index_erase04);

#ifdef SDP_TRACE_DUMP
    RUN_TEST(test_dump_lock_goldens);
#endif

    return UNITY_END();
}
