/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * built the skeleton: two smoke cases proving the timing hook
 * actually fires — via fakeit's .AlwaysDo, NOT a definition in the shared
 * .inc (ArduinoFake DEFINES delay()/delayMicroseconds() itself as free
 * functions; a second definition would be a link error) — before any real
 * protocol case existed. Fakeit's void-returning .AlwaysDo
 * (MethodStubbingProgress<void, arglist...>::AlwaysDo(std::function<void(...)>
 * method), a real specialised overload in this repo's pinned fakeit.hpp /
 * ArduinoFake 0.4.0) compiled directly against delay()/delayMicroseconds()'s
 * void signatures — no adaptation to the non-void serial_read_mock.h /
 * test_rurp_log_id.cpp idiom (both of which wrap a value-returning method)
 * was needed.
 *
 * (this file's remaining content) drives the REAL, UNMODIFIED
 * eprom_write_execute for all three protocols against a small synthetic
 * 4-byte block, using a stateful read-back model (host_stubs.cpp) so the
 * real up-to-20-pass retry loop converges in exactly 3 passes instead of
 * exhausting every retry and overflowing the recorder. Each protocol case
 * proves its own capture is overflow-free, successful, and — the load-
 * bearing property before anything is ever frozen — deterministic: it
 * drives twice and compares two snapshots positionally.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"

#include "../_shared/eprom_v131_expected.h"

using namespace fakeit;

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);

/* timing_push itself is NOT one of eprom_v131_expected.h's twelve read
 * accessors (that header declares only the six-strobe/six-timing READ side,
 * "declared once here so consumers get them from a single place") — it is
 * the WRITE entry point only the suite that installs the fakeit hooks below
 * needs, so it is declared here, not there. Mirrors timing_push's own
 * host_stubs_common.inc doc comment: extern "C", not static, precisely so
 * this setUp() lambda can call it. */
extern "C" void timing_push(uint8_t kind, uint32_t us);

/* host_stubs.cpp's Task 3 read-back model seam (R2): resets/seeds the
 * stateful, index-keyed model so the real write-verify loop converges. */
extern "C" void trace_readback_reset();
extern "C" void trace_readback_seed(uint8_t idx, uint8_t target, uint8_t converge_after);

/* ─────────────────────────────────────────────────────────────────────────
 * setUp / tearDown
 * ───────────────────────────────────────────────────────────────────────── */

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();

    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysDo([](unsigned int us) {
        timing_push(TIMING_KIND_DELAY_US, (uint32_t)us);
    });
    When(Method(ArduinoFake(), delay)).AlwaysDo([](unsigned long ms) {
        timing_push(TIMING_KIND_DELAY_MS, (uint32_t)ms);
    });
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);

    clear_strobes();
    clear_timings();
    reset_register_cache(0x00, 0x00, 0x00);
}

void tearDown(void) {}

/* ─────────────────────────────────────────────────────────────────────────
 * smoke cases — prove the skeleton before any protocol case exists.
 * ───────────────────────────────────────────────────────────────────────── */

/* setUp() alone must leave both recorders empty and un-overflowed — the
 * baseline every later case's assertions build on. */
void test_smoke_setup_leaves_both_recorders_clean(void) {
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_count(), "strobe_count after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_count(), "timing_count after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), "timing_overflowed after setUp");
}

/* Drives delayMicroseconds()/delay() DIRECTLY — no production code involved
 * yet — and proves the hook fires: two timing entries, the right kinds, the
 * right values, both keyed to strobe index 0 (no strobe has been pushed at
 * all). This is the load-bearing, non-vacuous proof that the timing layer
 * works BEFORE any protocol case depends on it. */
void test_smoke_timing_hook_fires_for_delay_and_delaymicroseconds(void) {
    delayMicroseconds(7);
    delay(3);

    TEST_ASSERT_EQUAL_MESSAGE(2, timing_count(), "timing_count after direct delay calls");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), "timing_overflowed after direct delay calls");

    TEST_ASSERT_EQUAL_MESSAGE(TIMING_KIND_DELAY_US, timing_kind(0), "entry 0 kind");
    TEST_ASSERT_EQUAL_MESSAGE(7, timing_us(0), "entry 0 us");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_after_strobe(0), "entry 0 seq");

    TEST_ASSERT_EQUAL_MESSAGE(TIMING_KIND_DELAY_MS, timing_kind(1), "entry 1 kind");
    TEST_ASSERT_EQUAL_MESSAGE(3, timing_us(1), "entry 1 us");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_after_strobe(1), "entry 1 seq");
}

/* ─────────────────────────────────────────────────────────────────────────
 * bus_config ground truth, derived (not invented) from the host's
 * own code path.
 *
 * Derivation command (run live against firestarter_app, 2026-08-08):
 *   cd /workspaces/firestarter_app && python3 -c "
 *     import sys
 *     sys.path.insert(0, 'tools'); sys.path.insert(0, '.')
 *     from gen_sdp_bus_config import derive_row
 *     from firestarter.database import EpromDatabase
 *     db = EpromDatabase(skip_local_override=True)
 *     for chip in ['AM27C512', 'AM27C020', 'AM2716']:
 *         print(chip, derive_row(db, chip))"
 *
 * A zeroed bus_config is DEGENERATE, not an identity remap:
 * mem_util_remap_address_bus (src/proms/memory.cpp:284-307) starts with
 * `reorg_address = config.address_mask & address`, so address_mask == 0
 * collapses every address to 0 — every literal below is therefore sourced
 * from a real chip via derive_row, never hand-written.
 *
 * Translation from derive_row's dict to bus_config_t fields mirrors
 * src/json_parser.c:214-249 (parse_bus_config) field-for-field: a `None`
 * rw_line/vpp_pin becomes the 0xFF sentinel (json_parser.c's own get_rw_pin/
 * get_vpp_pin default the field to 0xFF when the JSON key is absent);
 * static_high (absent for AM27C512/AM27C020, [13] for AM2716) becomes
 * static_high_mask by OR-ing 1UL << line per listed line (json_parser.c:243);
 * address_lines gets a 0xFF sentinel after the last real entry
 * (json_parser.c:230); remaining ADDRESS_LINES_SIZE-slot array members are
 * zero-filled by C++ aggregate-initializer rules (the same convention
 * tools/gen_sdp_bus_config.py's own emit_cpp_header uses for SDP_BUS_CONFIGS).
 * ───────────────────────────────────────────────────────────────────────── */

/* AM27C512 (protocol 0x07) — pinout DIP28_27512, mem_size 65536, pulse 100 us
 * (the modal 0x07 value). derive_row: bus=[0..15], matching_lines=16,
 * rw_line=None, vpp_pin=None, static_high=None. */
static const bus_config_t V131_BUS_CONFIG_0x07 = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF },
    0x0000FFFFUL,
    16,
    0xFF,
    0xFF,
    0x00000000UL
};

/* AM27C020 (protocol 0x08) — pinout DIP32_27C020, mem_size 262144, pulse
 * 100 us (the modal 0x08 value). derive_row: bus=[0..16,20],
 * matching_lines=17, rw_line=22, vpp_pin=21, static_high=None. Note
 * vpp_pin=21=0x15=VPP_P1_32_DIP exactly, so using_p1_as_vpp(handle) is TRUE
 * for this chip (handle.pins==32) — mem_util_remap_address_bus therefore
 * does NOT OR a VPP bit into the address bus at all, and
 * eprom_internal_set_control_register remaps CTRL_VPE_ENABLE to
 * CTRL_VPP_P1_ENABLE for every per-pass VPE assert/release. Both are real,
 * derived-from-source consequences of this chip's actual wiring, not a
 * special case coded here. */
static const bus_config_t V131_BUS_CONFIG_0x08 = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0xFF },
    0x0011FFFFUL,
    17,
    0x16,
    0x15,
    0x00000000UL
};

/* AM2716 (protocol 0x0B) — pinout DIP24_2716, mem_size 2048, pulse 500 us
 * (the modal 0x0B value, and the exact figure chip C1's adjudication table
 * names). derive_row: bus=[0..10], matching_lines=11, rw_line=None,
 * vpp_pin=11, static_high=[13]. vpp_pin=11=0x0B=VPP_P21_24_DIP exactly, so
 * using_p1_as_vpp(handle) is also TRUE for this chip (handle.pins==24) —
 * same two real consequences as AM27C020 above, this time for a completely
 * different reason (a 24-pin, not 32-pin, P1-routing constant). */
static const bus_config_t V131_BUS_CONFIG_0x0B = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0xFF },
    0x000007FFUL,
    11,
    0xFF,
    0x0B,
    0x00002000UL
};

static firestarter_handle_t make_v131_handle(uint32_t protocol, uint8_t pins, uint32_t mem_size,
                                              uint32_t pulse_delay_us, const bus_config_t& bus_config) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.vpp_mv = 0;
    h.pins = pins;
    h.mem_size = mem_size;
    h.pulse_delay = pulse_delay_us;
    h.bus_config = bus_config;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

static const uint8_t V131_SYNTHETIC_BLOCK[4] = { 0x3C, 0xFF, 0x55, 0xAA };

static void drive_v131_write(firestarter_handle_t* h) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, 0x00);
    trace_readback_reset();
    trace_readback_seed(0, V131_SYNTHETIC_BLOCK[0], 0);
    trace_readback_seed(1, V131_SYNTHETIC_BLOCK[1], 0);
    trace_readback_seed(2, V131_SYNTHETIC_BLOCK[2], 2);
    trace_readback_seed(3, V131_SYNTHETIC_BLOCK[3], 1);
    clear_strobes();
    clear_timings();
    h->address = 0;
    h->data_size = 4;
    for (int i = 0; i < 4; i++) {
        h->data_buffer[i] = (char)V131_SYNTHETIC_BLOCK[i];
    }
    h->firestarter_operation_main(h);
}

/* Mirrors host_stubs_common.inc's own cap constants (HOST_STUBS_MAX_STROBES /
 * HOST_STUBS_MAX_TIMINGS, both 512) — restated here because #defines inside
 * host_stubs.cpp's TU are not visible in this, a SEPARATE translation unit. */
#define V131_STROBE_CAP 512
#define V131_TIMING_CAP 512

static void assert_v131_protocol_case(uint32_t protocol, uint8_t pins, uint32_t mem_size,
                                       uint32_t pulse_delay_us, const bus_config_t& bus_config,
                                       const v131_trace_entry_t* expected, int expected_len,
                                       const char* ctx) {
    firestarter_handle_t h = make_v131_handle(protocol, pins, mem_size, pulse_delay_us, bus_config);

    drive_v131_write(&h);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code, ctx);

    int len1 = v131_merged_length();
    TEST_ASSERT_TRUE_MESSAGE(len1 > 0, ctx);
    TEST_ASSERT_TRUE_MESSAGE(len1 <= (int)(0.60 * V131_STROBE_CAP), ctx);
    TEST_ASSERT_TRUE_MESSAGE(len1 <= (int)(0.60 * V131_TIMING_CAP), ctx);

    /* PRIMARY assertion: full ordered positional equality against the frozen
     * pre-change cadence. */
    v131_assert_stream_equals(expected, expected_len, ctx);

    v131_trace_entry_t snap1[600];
    int n1 = v131_snapshot(snap1, 600);
    TEST_ASSERT_EQUAL_MESSAGE(len1, n1, ctx);

    /* Determinism: an identical second drive on the SAME handle must
     * reproduce the identical merged stream, positionally. */
    drive_v131_write(&h);
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), ctx);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code, ctx);

    int div = v131_first_divergence(snap1, n1);
    TEST_ASSERT_EQUAL_MESSAGE(-1, div, ctx);
}

void test_protocol_0x07_am27c512_capture_is_sound_and_deterministic(void) {
    assert_v131_protocol_case(0x07, 28, 65536UL, 100UL, V131_BUS_CONFIG_0x07,
                               EPROM_V131_TRACE_PROTO_07, EPROM_V131_TRACE_PROTO_07_LEN,
                               "0x07 AM27C512 DIP28_27512");
}

void test_protocol_0x08_am27c020_capture_is_sound_and_deterministic(void) {
    assert_v131_protocol_case(0x08, 32, 262144UL, 100UL, V131_BUS_CONFIG_0x08,
                               EPROM_V131_TRACE_PROTO_08, EPROM_V131_TRACE_PROTO_08_LEN,
                               "0x08 AM27C020 DIP32_27C020");
}

void test_protocol_0x0B_am2716_capture_is_sound_and_deterministic(void) {
    assert_v131_protocol_case(0x0B, 24, 2048UL, 500UL, V131_BUS_CONFIG_0x0B,
                               EPROM_V131_TRACE_PROTO_0B, EPROM_V131_TRACE_PROTO_0B_LEN,
                               "0x0B AM2716 DIP24_2716");
}

#ifdef EPROM_V131_TRACE_DUMP
static void dump_v131_merged_ready_to_paste(const char* tag) {
    int n = v131_merged_length();
    printf("##### %s total=%d strobe_overflow=%d timing_overflow=%d\n",
           tag, n, strobe_overflowed(), timing_overflowed());
    for (int i = 0; i < n; i++) {
        v131_trace_entry_t e;
        v131_merged_at(i, &e);
        printf("    {%d, 0x%02X, 0x%02X, %luUL}, /* %d */\n",
               e.kind, e.pin, e.value, (unsigned long)e.us, i);
    }
}

void test_dump_v131_traces(void) {
    firestarter_handle_t h07 = make_v131_handle(0x07, 28, 65536UL, 100UL, V131_BUS_CONFIG_0x07);
    drive_v131_write(&h07);
    dump_v131_merged_ready_to_paste("EPROM_V131_TRACE_PROTO_07");

    firestarter_handle_t h08 = make_v131_handle(0x08, 32, 262144UL, 100UL, V131_BUS_CONFIG_0x08);
    drive_v131_write(&h08);
    dump_v131_merged_ready_to_paste("EPROM_V131_TRACE_PROTO_08");

    firestarter_handle_t h0B = make_v131_handle(0x0B, 24, 2048UL, 500UL, V131_BUS_CONFIG_0x0B);
    drive_v131_write(&h0B);
    dump_v131_merged_ready_to_paste("EPROM_V131_TRACE_PROTO_0B");
}
#endif

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* SMOKE: the skeleton (recorders, hook wiring) — before any protocol case */
    RUN_TEST(test_smoke_setup_leaves_both_recorders_clean);
    RUN_TEST(test_smoke_timing_hook_fires_for_delay_and_delaymicroseconds);

    /* real, unmodified write loop, one case per protocol */
    RUN_TEST(test_protocol_0x07_am27c512_capture_is_sound_and_deterministic);
    RUN_TEST(test_protocol_0x08_am27c020_capture_is_sound_and_deterministic);
    RUN_TEST(test_protocol_0x0B_am2716_capture_is_sound_and_deterministic);

#ifdef EPROM_V131_TRACE_DUMP
    RUN_TEST(test_dump_v131_traces);
#endif

    return UNITY_END();
}
