/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Validation ceiling: every claim
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
#include "operation_utils.h"
#include "../_shared/sdp_bus_config.h"
#include "../_shared/sdp_expected.h"

using namespace fakeit;

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);

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
static bool     s_poll_addr_toggles;

/* the tick source is now a SCRIPTED QUEUE, replacing the
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

/* decodes the u32 parameter of the FIRST captured frame
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
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured_frames.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
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
    h.bus_config = SDP_BUS_CONFIGS[0].bus_config;
    h.ctrl_flags = ctrl_flags | FLAG_SKIP_BLANK_CHECK;
    return h;
}

/* Pattern 3: dispatch on ADDRESS, not call order. Virgin 0xFF everywhere
 * except the two planted manufacturer/device identity bytes; the SDP
 * completion-poll address (0x5555) is deliberately never satisfied. Migrated
 * from test_sdp_harness.cpp (116-05).
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
        if (s_poll_addr_toggles) {
            return (s_reads_at_poll_addr % 2 == 0) ? (uint8_t)0x00 : (uint8_t)0x40;
        }
        return 0xFF; /* virgin default -- never satisfies the 0x20 SDP-disable poll */
    }
    return 0xFF;
}

static void drive_reference_emitter(firestarter_handle_t* h, const byte_flip_t* table, size_t len, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    for (size_t i = 0; i < len; i++) {
        h->firestarter_set_data(h, table[i].address, table[i].byte);
    }
}

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
    clear_strobes();
    h->firestarter_operation_init(h);
    return stale_ctrl;
}

/* ─────────────────────────────────────────────────────────────────────────
 * driving the PRODUCTION lock op (CMD_SDP_LOCK)
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

static void drive_lock_op(firestarter_handle_t* h, rurp_register_t ctrl_seed) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, ctrl_seed);
    clear_strobes();
    h->firestarter_operation_main(h);
}

/*
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

/*
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
    h.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "migrated (RED, CORRECTION 2): matching identity must proceed past SDP-disable, not "
        "time out and overwrite response_code with ERROR");
}

/* CORRECTION 2's finding, second-order evidence of the inverted completion
 * Before this plan, the two chip-ID mismatch ids -- MSG_WARN_CHIP_ID_MISMATCH
 * and MSG_ERR_CHIP_ID_MISMATCH -- appeared in ZERO test files anywhere in
 * this tree, so severity (which rides entirely in the id, not the
 * response_code) had no oracle at all. The response_code legs above and the
 * id legs below are complementary, not redundant: LOG_WARN_ID_BYTES
 * (include/logging_id.h:119) and LOG_ERROR_ID_BYTES
 * (include/logging_id.h:110) are the SAME alias of LOG_ID_BYTES, so a
 * transposed id ships the wrong severity on the wire even when
 * response_code still reads correctly -- neither leg can see the other's
 * transposition. */
void test_case7_mismatching_chip_id_with_force_warns(void) {
    s_mfr_addr_keyed = 32768 - 64;
    s_mfr_hi_keyed = 0xDE;
    s_mfr_lo_keyed = 0xAD;
    firestarter_handle_t h = make_identity_handle(0x1F08, FLAG_FORCE);
    configure_memory(&h);
    h.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "migrated (RED, CORRECTION 2): mismatching identity + FLAG_FORCE must WARN, not have its "
        "severity destroyed by the unconditional SDP-disable completion wait");

    /* WARN direction, by id: severity rides entirely in the id, so this leg
     * is what a transposed (MSG_WARN_CHIP_ID_MISMATCH, MSG_ERR_CHIP_ID_MISMATCH)
     * swap trips -- the response_code assertion above it structurally cannot
     * see that transposition. */
    std::vector<uint8_t> ids;
    sdp_captured_frame_ids(&ids);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_WARN_CHIP_ID_MISMATCH),
        "Case 7 (chip-ID severity fork, WARN direction): MSG_WARN_CHIP_ID_MISMATCH must appear in "
        "the captured frame ids under FLAG_FORCE -- severity rides entirely in the id "
        "(LOG_WARN_ID_BYTES / LOG_ERROR_ID_BYTES are the same alias of LOG_ID_BYTES), so this leg is "
        "what a transposed id would trip");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids, (uint8_t)MSG_ERR_CHIP_ID_MISMATCH),
        "Case 7 (chip-ID severity fork, WARN direction): MSG_ERR_CHIP_ID_MISMATCH must NOT also "
        "appear under FLAG_FORCE -- this pins the fork in both directions, not just the presence half");

    /* ERROR direction, by id -- Case 11's anti-hollow re-drive shape:
     * without this second drive, the WARN-direction id assertions above
     * could pass for a reason unrelated to the flag (e.g. an id that is
     * always emitted regardless of FLAG_FORCE). Re-driving without the flag
     * is what proves the id is CONDITIONAL on FLAG_FORCE rather than always
     * emitted -- an assertion that only ever sees one direction cannot
     * detect a transposition that swaps both ids at once. */
    captured_frames.clear();
    s_mfr_addr_keyed = 32768 - 64;
    s_mfr_hi_keyed = 0xDE;
    s_mfr_lo_keyed = 0xAD;
    firestarter_handle_t h2 = make_identity_handle(0x1F08, 0); /* FLAG_FORCE absent */
    configure_memory(&h2);
    h2.firestarter_get_data = mock_get_data_keyed;
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    h2.firestarter_operation_init(&h2);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h2.response_code,
        "Case 7 (chip-ID severity fork, ERROR direction): mismatching identity WITHOUT FLAG_FORCE "
        "must refuse with RESPONSE_CODE_ERROR");
    std::vector<uint8_t> ids2;
    sdp_captured_frame_ids(&ids2);
    TEST_ASSERT_TRUE_MESSAGE(sdp_ids_contains(ids2, (uint8_t)MSG_ERR_CHIP_ID_MISMATCH),
        "Case 7 (chip-ID severity fork, ERROR direction): MSG_ERR_CHIP_ID_MISMATCH must appear in "
        "the captured frame ids without FLAG_FORCE -- this re-drive is what proves the id is "
        "conditional on the flag rather than always emitted");
    TEST_ASSERT_FALSE_MESSAGE(sdp_ids_contains(ids2, (uint8_t)MSG_WARN_CHIP_ID_MISMATCH),
        "Case 7 (chip-ID severity fork, ERROR direction): MSG_WARN_CHIP_ID_MISMATCH must NOT appear "
        "without FLAG_FORCE -- pins the fork in both directions inside this one case");
}

void test_case8_completion_poll_preserves_prior_severity(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    h.response_code = RESPONSE_CODE_WARNING;
    s_poll_addr_toggles = true; /* the completion poll can never conclude */
    drive_write_init(&h, 0x00);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "Case 8: the completion poll is advisory only (D-05) and must never overwrite "
        "a prior response_code, even when it never settles");
}

void test_case9_skip_flag_suppresses_unlock_stream(void) {
    firestarter_handle_t h = make_sdp_handle(SDP_BUS_CONFIGS[0], FLAG_SKIP_SDP_UNLOCK); /* AT28C256 */
    drive_write_init(&h, 0x00);

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(),
        "Case 9 (OBS-02): recorded stream must not overflow");

    TEST_ASSERT_EQUAL_MESSAGE(0, sdp_first_divergence(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN),
        "Case 9 (OBS-02): FLAG_SKIP_SDP_UNLOCK set -- the recorded stream must diverge from the "
        "full unlock stream (SDP_FIXED_DIP28_28C256) starting at index 0");

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

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_count(),
        "Case 9 (OBS-02, secondary corroboration ONLY -- see the index-0 divergence and the "
        "payload-byte-absence walk above for the load-bearing proof)");
}

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

/* Drives the standalone CMD_SDP_UNLOCK op directly (h.firestarter_operation_main,
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

/* Case 24: a handle whose firestarter_operation_main is NULL (no dispatch
 * involved -- this proves the REFUSAL itself, not any one handler's
 * omission), driven through the REAL op_execute_stateful_operation, exactly
 * as every eprom_* entry point does
 * (`return op_execute_stateful_operation(callback, handle)`). Passing NULL
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

    bool finished = op_execute_stateful_operation(NULL, &h);

    TEST_ASSERT_TRUE_MESSAGE(finished,
        "Case 24 (D-06/D-07): op_execute_stateful_operation must return true on a NULL main -- "
        "the engine now reports finished directly and the nine eprom_* wrappers forward that "
        "result without inverting it, unchanged observable semantics from before this task");
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

/*
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

    bool finished = false;
    int calls = 0;
    const int MAX_CALLS = 10; /* deterministic trace needs exactly 4; generous margin, not an escape hatch */
    while (!finished && calls < MAX_CALLS) {
        finished = op_execute_simple_operation(&h);
        calls++;
    }

    TEST_ASSERT_TRUE_MESSAGE(finished,
        "Case 25 (ERASE-03, mechanism-corrected/intent-satisfied -- never as failed): "
        "op_execute_simple_operation must reach completion (true) within MAX_CALLS iterations of "
        "the real ACK-gated INIT/MAIN/END state machine -- eprom_erase reports the erase as "
        "finished, the same call-site contract as before this task, now honestly (the erase actually "
        "ran instead of silently doing nothing)");
    TEST_ASSERT_EQUAL_MESSAGE(4, calls,
        "Case 25 (ERASE-03): completion must take exactly four engine calls -- the INIT-start "
        "ack, the MAIN-start ack plus the erase run, the END-start ack, and the final ack that "
        "flips the all-operations-done message check -- the count this case's own DEVIATION "
        "comment above documents. Without this assertion the case is vacuous: the un-flipped "
        "loop was measured exiting after one call while this case still reported PASSED");
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

void test_case30_write_init_no_blank_check_with_flag_clear_erase01(void) {
    firestarter_handle_t h = make_sdp_handle_blank_check_enabled(SDP_BUS_CONFIGS[0]); /* AT28C256 */
    drive_write_init(&h, 0x00);

    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "Case 30 (ERASE-01): is_operation_in_progress must be FALSE after exactly one "
        "eeprom28c_write_init call with FLAG_SKIP_BLANK_CHECK clear -- mem_util_blank_check is "
        "the only setter of this flag on the write-INIT path, so TRUE here would mean the "
        "pre-write blank check still ran and left a multi-call INIT loop pending");
    /* The companion "must be NULL" assertion on the removed heap-allocated
     * handle field is GONE, and so is the field itself: mem_util_blank_check
     * no longer allocates that block (it keeps its saved address in a
     * file-scope static in memory.cpp), so there is no allocation left to
     * observe. This is the loss of a redundant PROBE, not of coverage --
     * is_operation_in_progress above and the removed allocation used to be
     * unconditionally adjacent statements in the same then-branch of the
     * same if, with no intervening control flow, early return or condition,
     * so a FALSE result there strictly implied the branch -- and therefore
     * the allocation -- never executed. The behaviour under test is still
     * pinned by the assertion above. */
    sdp_assert_stream_equals(SDP_FIXED_DIP28_28C256, SDP_FIXED_DIP28_28C256_LEN,
        "Case 30 (ERASE-01): with FLAG_SKIP_BLANK_CHECK clear, the AT28C256/DIP28_28C256 stream "
        "must now be byte-identical to the golden captured with the flag SET -- the D-07 policy "
        "expressed as a stream identity: a pre-write blank check contributes zero strobes "
        "whether or not the caller asks to skip it");
}

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
