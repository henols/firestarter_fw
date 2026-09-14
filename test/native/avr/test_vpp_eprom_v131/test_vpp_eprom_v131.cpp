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
}
#include "firestarter.h"
#include "eprom.h"
#include "eprom_params.h"
#include "memory_utils.h"
#include "messages.h"  /* MSG_ERR_VPP_HIGH / MSG_WARN_VPP_HIGH / MSG_WARN_VPP_LOW /
                         * MSG_WARN_REV0_VPP_UNSUPPORTED -- neither firestarter.h
                         * nor eprom.h/eprom_params.h/memory_utils.h pulls this in
                         * transitively. */

using namespace fakeit;

/* ─────────────────────────────────────────────────────────────────────────
 * Restated locally, because #defines inside host_stubs.cpp's translation
 * unit are invisible here, a SEPARATE translation unit -- mirrors
 * test_loop_eprom_v131.cpp's identical restatement of these same four kind
 * constants.
 * ───────────────────────────────────────────────────────────────────────── */
#define STROBE_KIND_DATA     1
#define STROBE_KIND_PIN      2
#define TIMING_KIND_DELAY_US 3
#define TIMING_KIND_DELAY_MS 4

/* Shared strobe/timing recorder accessors (../_shared/host_stubs_common.inc,
 * compiled into host_stubs.cpp's translation unit under the
 * HOST_STUBS_REAL_REGISTER_UTILS + HOST_STUBS_RECORD_TIMING arms). */
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
extern "C" void     timing_push(uint8_t kind, uint32_t us);

/* host_stubs.cpp's own seams (Task 2). reset_register_cache mirrors the
 * sibling suite's seam of the same name; the vpp_readback_ family is this
 * suite's own extended (mismatch-window) model; set_mock_vpp_mv is this
 * suite's voltage-injection seam. rurp_read_data_buffer and
 * rurp_log_id/_u8/_u16/_u24/_u32 are already declared via firestarter.h ->
 * rurp_shield.h, so they are not re-declared here. */
extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);

extern "C" void vpp_readback_reset(void);
extern "C" void vpp_readback_seed(uint16_t addr16, uint8_t target, uint16_t converge_after, uint16_t mismatch_from);
extern "C" int  vpp_readback_reads(uint16_t addr16);
extern "C" int  vpp_readback_seeded_count(void);

extern "C" void     clear_logged_ids(void);
extern "C" int      logged_id_count(void);
extern "C" uint8_t  logged_id_at(int i);
extern "C" uint8_t  logged_id_param_count(int i);
extern "C" uint8_t  logged_id_param(int i, int j);
extern "C" int      logged_ids_overflowed(void);

extern "C" void set_mock_vpp_mv(uint16_t mv);

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
    clear_logged_ids();
    vpp_readback_reset();
    reset_register_cache(0x00, 0x00, 0x00);
    /* No case may inherit another's injected voltage. */
    set_mock_vpp_mv(0);
}

void tearDown(void) {
    /* Reset any hardware-revision override back to the file's default
     * (REVISION_0, via the shared stub base's zero-initialised
     * s_host_config) so it can never leak into a case that runs after one
     * that overrode it -- Unity calls tearDown() even when a case fails via
     * TEST_ASSERT's longjmp, so this reset is unconditional and always
     * runs. */
    rurp_get_config()->hardware_revision = 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * make_vpp_handle -- fresh, zero-initialised handle per case.
 *
 * h.chip_id = 0 skips the chip-id branch (eprom_generic_init only reaches
 * eprom_internal_check_chip_id when chip_id > 0). ctrl_flags always carries
 * FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE so a case's own extra_ctrl_flags
 * (e.g. FLAG_FORCE, FLAG_VPE_AS_VPP) never has to repeat them.
 * ───────────────────────────────────────────────────────────────────────── */
[[maybe_unused]] static firestarter_handle_t make_vpp_handle(uint32_t protocol, uint8_t pins, uint32_t mem_size,
                                                                uint32_t pulse_delay_us, uint16_t vpp_setpoint_mv,
                                                                uint32_t extra_ctrl_flags, const bus_config_t& bus_config) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.pins = pins;
    h.mem_size = mem_size;
    h.pulse_delay = pulse_delay_us;
    h.bus_config = bus_config;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.chip_id = 0;
    h.ctrl_flags = extra_ctrl_flags | FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    h.vpp_mv = vpp_setpoint_mv;
    return h;
}

/* ─────────────────────────────────────────────────────────────────────────
 * bus_config ground truth, copied VERBATIM (renamed VPP_BUS_CONFIG_0x07 /
 * _0x08 / _0x0B) from test_loop_eprom_v131.cpp's LOOP_BUS_CONFIG_0x07 /
 * _0x08 / _0x0B (itself copied from test_trace_eprom_v131.cpp's
 * V131_BUS_CONFIG_*) -- do NOT invent new ones: a zeroed bus_config is
 * DEGENERATE, not an identity remap (mem_util_remap_address_bus starts from
 * `config.address_mask & address`, and address_mask == 0 collapses every
 * address to 0).
 * ───────────────────────────────────────────────────────────────────────── */

/* AM27C512 (protocol 0x07) -- pinout DIP28_27512, mem_size 65536, pulse
 * 100 us (the modal 0x07 value). derive_row: bus=[0..15], matching_lines=16,
 * rw_line=None, vpp_pin=None, static_high=None. */
[[maybe_unused]] static const bus_config_t VPP_BUS_CONFIG_0x07 = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF },
    0x0000FFFFUL,
    16,
    0xFF,
    0xFF,
    0x00000000UL
};

/* AM27C020 (protocol 0x08) -- pinout DIP32_27C020, mem_size 262144, pulse
 * 100 us (the modal 0x08 value). derive_row: bus=[0..16,20],
 * matching_lines=17, rw_line=22, vpp_pin=21, static_high=None. vpp_pin=21=
 * 0x15=VPP_P1_32_DIP exactly, so using_p1_as_vpp(handle) is TRUE for this
 * chip (handle.pins==32). matching_lines=17 / static_high_mask=0 below is
 * also this suite's own read-back-model key derivation (host_stubs.cpp): it
 * is the reason the 16-bit LSB|MSB<<8 key equals address & 0xFFFF for this,
 * the tree's only 32-pin config. */
[[maybe_unused]] static const bus_config_t VPP_BUS_CONFIG_0x08 = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0xFF },
    0x0011FFFFUL,
    17,
    0x16,
    0x15,
    0x00000000UL
};

/* AM2716 (protocol 0x0B) -- pinout DIP24_2716, mem_size 2048, pulse 500 us
 * (the modal 0x0B value). derive_row: bus=[0..10], matching_lines=11,
 * rw_line=None, vpp_pin=11, static_high=[13]. vpp_pin=11=0x0B=
 * VPP_P21_24_DIP exactly, so using_p1_as_vpp(handle) is also TRUE for this
 * chip (handle.pins==24). */
[[maybe_unused]] static const bus_config_t VPP_BUS_CONFIG_0x0B = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0xFF },
    0x000007FFUL,
    11,
    0xFF,
    0x0B,
    0x00002000UL
};

/* ─────────────────────────────────────────────────────────────────────────
 *   configure_memory (writes address 0 itself, memory.cpp:93)
 *   -> reset_register_cache (AFTER configure_memory -- it calls
 *      mem_util_set_address(handle, 0), memory.cpp:97)
 *   -> clear_strobes / clear_timings / clear_logged_ids
 * ───────────────────────────────────────────────────────────────────────── */
[[maybe_unused]] static void drive_vpp_write(firestarter_handle_t* h, uint32_t base,
                                              const uint8_t* block, uint8_t n) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    clear_timings();
    clear_logged_ids();
    h->address = base;
    h->data_size = n;
    for (uint8_t i = 0; i < n; i++) {
        h->data_buffer[i] = (char)block[i];
    }
    h->firestarter_operation_main(h);
}

[[maybe_unused]] static void drive_vpp_init(firestarter_handle_t* h) {
    configure_memory(h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    clear_timings();
    clear_logged_ids();
    h->firestarter_operation_init(h);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Shared helpers for later plans -- the CONTROL-register write stream.
 * VERBATIM from test_loop_eprom_v131.cpp's identical helpers (:1136-1193).
 *
 * Every non-elided CONTROL write (rurp_write_to_register(CONTROL_REGISTER,
 * ...), include/rurp_register_utils.h) shows up in the strobe recorder as
 * THREE consecutive entries via rurp_internal_write_to_register, in this
 * FIXED, unconditional order, never interleaved with anything else:
 *   [STROBE_KIND_DATA, pin=0,               value=<the physical byte>]
 *   [STROBE_KIND_PIN,  pin=CONTROL_REGISTER, value=1]  (latch rise)
 *   [STROBE_KIND_PIN,  pin=CONTROL_REGISTER, value=0]  (latch fall)
 * STROBE_KIND_DATA is NOT a safe raw pulse-count oracle, because a
 * register-shift write pushes this exact same DATA-strobe shape as a
 * genuine chip-data pulse. These helpers exploit that fixed 3-entry shape
 * directly instead of fighting it.
 * ───────────────────────────────────────────────────────────────────────── */
[[maybe_unused]] static int control_write_count(void) {
    int n = strobe_count();
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (strobe_kind(i) == STROBE_KIND_PIN && strobe_pin(i) == CONTROL_REGISTER && strobe_value(i) == 1) c++;
    }
    return c;
}

[[maybe_unused]] static int control_write_strobe_index(int idx) {
    int n = strobe_count();
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (strobe_kind(i) == STROBE_KIND_PIN && strobe_pin(i) == CONTROL_REGISTER && strobe_value(i) == 1) {
            if (c == idx) return i;
            c++;
        }
    }
    return -1;
}

/* The Nth (0-indexed) non-elided CONTROL_REGISTER write's PHYSICAL byte
 * value -- i.e. AFTER rurp_map_ctrl_reg_for_hardware_revision's per-
 * revision remap (rurp_hw_rev_utils.h), not the pre-remap LOGICAL value
 * mem_util_calculate_top_address_register / eprom.cpp compute. -1 means
 * "not decodable"; every assertion built on this must check `v >= 0` first. */
[[maybe_unused]] static int control_write_value(int idx) {
    int strobe_idx = control_write_strobe_index(idx);
    if (strobe_idx <= 0 || strobe_kind(strobe_idx - 1) != STROBE_KIND_DATA) return -1;
    return (int)strobe_value(strobe_idx - 1);
}

/* The stream index of the first GENUINE chip-data pulse -- the first
 * STROBE_KIND_DATA entry whose value matches one of the block's own byte
 * values, never a register-shift write's LSB/MSB/CONTROL byte. */
[[maybe_unused]] static int first_genuine_pulse_strobe_index(const uint8_t* values, int n_values) {
    int n = strobe_count();
    for (int i = 0; i < n; i++) {
        if (strobe_kind(i) != STROBE_KIND_DATA) continue;
        uint8_t v = strobe_value(i);
        for (int j = 0; j < n_values; j++) {
            if (values[j] == v) return i;
        }
    }
    return -1;
}

[[maybe_unused]] static int count_logged_id(uint8_t id) {
    int n = logged_id_count();
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (logged_id_at(i) == id) c++;
    }
    return c;
}

[[maybe_unused]] static int find_logged_id(uint8_t id) {
    int n = logged_id_count();
    for (int i = 0; i < n; i++) {
        if (logged_id_at(i) == id) return i;
    }
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Mandatory for every drop-bit assertion: on the default REVISION_0/1
 * mapping, CTRL_ADDRESS_LINE_16 and CTRL_VPP_VPE_DROP_ENABLE remap onto the
 * SAME physical bit (0x01), which would make a drop-bit claim undecidable
 * from the recorded physical byte alone. On REVISION_2_x they map to
 * distinct physical bits: CTRL_ADDRESS_LINE_16_REV2 (0x20) vs
 * CTRL_VPP_VPE_DROP_ENABLE_REV2 (0x01). This changes nothing about the
 * LOGICAL behaviour under test -- every eprom.cpp/memory.cpp bit check
 * operates on the pre-remap logical value; only the recorded strobe BYTE
 * differs.
 * ───────────────────────────────────────────────────────────────────────── */
[[maybe_unused]] static void use_revision_2_2_for_this_case(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
}

/* ─────────────────────────────────────────────────────────────────────────
 * The one case this plan owns: harness self-check. Three independently
 * failable groups.
 * ───────────────────────────────────────────────────────────────────────── */
void test_setup_leaves_the_harness_clean_and_the_composites_correct(void) {
    /* (a) All recorders/models start clean after setUp(). */
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_count(), "strobe_count after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_count(), "timing_count after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, logged_id_count(), "logged_id_count after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed after setUp");
    TEST_ASSERT_EQUAL_MESSAGE(0, vpp_readback_seeded_count(), "vpp_readback_seeded_count after setUp");

    TEST_ASSERT_EQUAL_MESSAGE((CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE),
        EPROM_HV_ROUTE_MASK, "EPROM_HV_ROUTE_MASK must be exactly regulator|drop");
    TEST_ASSERT_EQUAL_MESSAGE((CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE),
        EPROM_HV_ALL_OFF_MASK, "EPROM_HV_ALL_OFF_MASK must be exactly regulator|drop|a9|vpe");
    TEST_ASSERT_EQUAL_MESSAGE(0, (EPROM_HV_ALL_OFF_MASK & CTRL_VPP_P1_ENABLE),
        "EPROM_HV_ALL_OFF_MASK must NOT name CTRL_VPP_P1_ENABLE (C-4 + the VPE->P1 remap)");

    const uint8_t target = 0x55;
    reset_register_cache(0x34, 0x12, 0x00);  /* LSB=0x34, MSB=0x12 -> key 0x1234 */
    vpp_readback_seed(0x1234, target, 1, 2);
    uint8_t r1 = rurp_read_data_buffer();
    uint8_t r2 = rurp_read_data_buffer();
    uint8_t r3 = rurp_read_data_buffer();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, r1, "read 1: not yet converged");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(target, r2, "read 2: converged, matches");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE((uint8_t)(~target), r3, "read 3: mismatch window reached, diverges again");
    TEST_ASSERT_EQUAL_MESSAGE(3, vpp_readback_reads(0x1234), "read_count must have advanced by exactly 3");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Group A calls mem_util_calculate_top_address_register directly
 * (declared memory_utils.h:22) and reads the return value in LOGICAL bit
 * space -- NOT through rurp_map_ctrl_reg_for_hardware_revision's physical
 * remap -- so no REV2-vs-REV0/1 disambiguation on a recorded byte is
 * needed here (contrast Group B below, which DOES need it, because it
 * reads the strobe recorder's post-remap PHYSICAL byte). address = 0 so no
 * address bit confounds the assertion.
 *
 * Every row is registered as its OWN Unity case in main() (never folded
 * into one table-driven function) so pio test's per-case pass/fail report
 * can show "exactly these three rows are RED" directly by name -- a single
 * function combining all nine would longjmp out of TEST_ASSERT on the
 * first failure and hide the rest (Unity has no try/continue).
 * ───────────────────────────────────────────────────────────────────────── */
static rurp_register_t truth_table_drop_bit(uint8_t pins, uint8_t revision, rurp_register_t seeded_ctrl) {
    rurp_get_config()->hardware_revision = revision;
    firestarter_handle_t h = {};
    h.pins = pins;
    reset_register_cache(0x00, 0x00, seeded_ctrl);
    rurp_register_t top_address = mem_util_calculate_top_address_register(&h, 0);
    return (rurp_register_t)(top_address & CTRL_VPP_VPE_DROP_ENABLE);
}

void test_vpp01_truthtable_pins28_rev2_2_drop_bit_present(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(28, REVISION_2_2, EPROM_HV_ROUTE_MASK) != 0,
        "row (pins=28, REVISION_2_2): drop bit must be PRESENT -- today's behaviour, unchanged");
}

void test_vpp01_truthtable_pins28_rev1_drop_bit_present(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(28, REVISION_1, EPROM_HV_ROUTE_MASK) != 0,
        "row (pins=28, REVISION_1): drop bit must be PRESENT -- today's behaviour, unchanged");
}

void test_vpp01_truthtable_pins32_rev2_0_drop_bit_present(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_2_0, EPROM_HV_ROUTE_MASK) != 0,
        "row (pins=32, REVISION_2_0): drop bit must be PRESENT -- D-01/D-02, new");
}

void test_vpp01_truthtable_pins32_rev2_2_drop_bit_present(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_2_2, EPROM_HV_ROUTE_MASK) != 0,
        "row (pins=32, REVISION_2_2): drop bit must be PRESENT -- D-01/D-02, new");
}

void test_vpp01_truthtable_pins32_rev2_3_drop_bit_present(void) {
    /* REVISION_2_3 must be an explicit switch case in memory.cpp's new arm,
     * never a `>= REVISION_2_0` range test -- rurp_shield.h:25-31 numbers
     * revisions 0..5, and a range test would silently swallow a future
     * REVISION_2_4. This row is what makes that requirement executable. */
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_2_3, EPROM_HV_ROUTE_MASK) != 0,
        "row (pins=32, REVISION_2_3): drop bit must be PRESENT -- D-01/D-02, new; must be an explicit case, not a range test");
}

void test_vpp01_truthtable_pins32_rev1_drop_bit_absent(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_1, EPROM_HV_ROUTE_MASK) == 0,
        "row (pins=32, REVISION_1): drop bit must be ABSENT -- D-02's gate, the load-bearing negative");
}

void test_vpp01_truthtable_pins32_rev0_drop_bit_absent(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_0, EPROM_HV_ROUTE_MASK) == 0,
        "row (pins=32, REVISION_0): drop bit must be ABSENT");
}

void test_vpp01_truthtable_pins32_revunknown_drop_bit_absent(void) {
    /* Fail-safe direction: an unrecognised revision (0xFE) keeps TODAY's
     * stripping, matching rurp_hw_rev_utils.h:33-37's `default` leaving
     * ctrl_reg = 0. */
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_UNKNOWN, EPROM_HV_ROUTE_MASK) == 0,
        "row (pins=32, REVISION_UNKNOWN): drop bit must be ABSENT -- fail-safe default direction");
}

void test_vpp01_truthtable_pins32_rev2_2_preserve_never_introduces(void) {
    TEST_ASSERT_TRUE_MESSAGE(truth_table_drop_bit(32, REVISION_2_2, 0x00) == 0,
        "row (pins=32, REVISION_2_2, no bit seeded): preserve must never INTRODUCE the drop bit");
}

void test_vpp01_dip32_nonEprom_0x10_route_is_byte_identical_before_and_after(void) {
    /*
     * mem_util_set_address is firestarter_set_address for EVERY protocol
     * (memory.cpp:92, installed by configure_memory for all handles) and
     * writes CONTROL_REGISTER unconditionally (memory.cpp:231), so a plain
     * address sweep -- never a 0x10 write/read/erase operation -- covers
     * the whole surface at risk without depending on any 0x10 operation
     * semantics. The four addresses cross the A16 boundary (0x00FFFE /
     * 0x00FFFF below it, 0x010000 / 0x010001 at/above it) so the
     * top-address computation is genuinely exercised, not just re-latching
     * a static value.
     * would make "no drop bit leaked" undecidable from the byte alone. */
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x10 /* PROTO_FLASH_INTEL */, 32, 262144, 100, 1200, 0, VPP_BUS_CONFIG_0x08);
    configure_memory(&h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();

    const uint32_t addrs[4] = { 0x00FFFEUL, 0x00FFFFUL, 0x010000UL, 0x010001UL };
    for (int i = 0; i < 4; i++) {
        h.firestarter_set_address(&h, addrs[i]);
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- tiny four-address sweep, must be sound");

    int n = control_write_count();
    for (int i = 0; i < n; i++) {
        int v = control_write_value(i);
        char msg[128];
        snprintf(msg, sizeof(msg), "control write %d (0x%02X) must NOT carry CTRL_VPP_VPE_DROP_ENABLE_REV2 -- 0x10 never sets it, no leak", i, v);
        TEST_ASSERT_TRUE_MESSAGE(v >= 0 && (v & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0, msg);
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, n, "control_write_count -- measured pre-change literal");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x20, control_write_value(0), "control write 0 -- measured pre-change literal (A16 boundary crossing, REV2-physical)");
}

void test_vpp04_a_overvoltage_refusal_fires_by_id_with_payload_shape(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;  /* mandatory: on
        REVISION_0 eprom_check_vpp takes the early return at eprom.cpp:334-338
        and never reaches the over-voltage compare at all (PATTERNS SS B-8);
        also mandatory because the drop bit and A16 share physical 0x01 on
        Rev 0/1, which would make leg (b)'s drop-bit clear undecidable. */
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13501);  /* one mV past the 13000+500 over-voltage boundary -- pins the boundary, not a wildly out-of-range value */
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "an injected 13501 mV reading (setpoint 13000, boundary 13500) must refuse with RESPONSE_CODE_ERROR");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_ERR_VPP_HIGH),
        "MSG_ERR_VPP_HIGH (0xB8) must be logged exactly once, BY ID -- no test in this tree asserts this before this plan (D-13)");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_WARN_VPP_HIGH),
        "the hard-ERROR fork must NOT also log the WARNING id -- pins the FLAG_FORCE fork in both directions");
    int idx = find_logged_id(MSG_ERR_VPP_HIGH);
    TEST_ASSERT_TRUE_MESSAGE(idx >= 0, "MSG_ERR_VPP_HIGH must actually be present in the logged-id stream");
    TEST_ASSERT_EQUAL_MESSAGE(8, logged_id_param_count(idx),
        "the ERROR frame must carry the 8 payload bytes eprom.cpp:369-371 (LOG_ERROR_ID_BYTES) emits");
}

void test_vpp04_b_no_hv_route_left_asserted_on_the_refusal_path(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13501);
    drive_vpp_init(&h);

    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "non-vacuity: at least the over-voltage assert and the refusal's own disable must both have written CONTROL");

    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after an over-voltage refusal must have CTRL_VPP_REGULATOR_ENABLE CLEAR -- eprom_check_vpp's own :393 disable");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0,
        "the LAST control value after an over-voltage refusal must have the drop bit CLEAR too");

    bool saw_earlier_set = false;
    for (int i = 0; i < n - 1; i++) {
        int v = control_write_value(i);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_earlier_set = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_earlier_set,
        "an EARLIER control value must have CTRL_VPP_REGULATOR_ENABLE SET -- otherwise the 'last value clear' assertion is vacuously true of a register that was never energised at all");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- soundness precondition for the strobe walk above");
}

void test_vpp04_c_flag_force_downgrades_to_warning_and_still_clears_the_route(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, FLAG_FORCE, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13501);
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "FLAG_FORCE must downgrade the identical over-voltage reading to RESPONSE_CODE_WARNING");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_WARN_VPP_HIGH),
        "MSG_WARN_VPP_HIGH (0x82) must be logged exactly once under FLAG_FORCE");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VPP_HIGH),
        "the FLAG_FORCE downgrade must NOT also log the hard-error id");

    /* The downgrade IS the refusal's semantics (D-15c) -- it must leave no
     * route asserted too, exactly like leg (b) above. */
    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "non-vacuity: at least the over-voltage assert and the refusal's own disable must both have written CONTROL");
    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after the FLAG_FORCE downgrade must have CTRL_VPP_REGULATOR_ENABLE CLEAR too");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0,
        "the LAST control value after the FLAG_FORCE downgrade must have the drop bit CLEAR too");
    bool saw_earlier_set = false;
    for (int i = 0; i < n - 1; i++) {
        int v = control_write_value(i);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_earlier_set = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_earlier_set,
        "an EARLIER control value must have CTRL_VPP_REGULATOR_ENABLE SET -- otherwise the 'last value clear' assertion is vacuously true");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- soundness precondition for the strobe walk above");
}

void test_vpp04_d_in_range_reading_fires_neither_error_nor_warning(void) {
    /* The control that makes (a) and (c) mean anything: without this leg,
     * (a)'s id assertion could pass for a reason unrelated to the injected
     * reading (e.g. a resolver that always logs MSG_ERR_VPP_HIGH). Task 3's
     * SUMMARY records that flipping set_mock_vpp_mv below to 13501 makes
     * this leg fail -- a control that cannot fail is not a control. */
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13000);  /* == setpoint: 13000 > 13500 is false, 13000 < 12350 is false */
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "an in-range reading (== setpoint) must leave RESPONSE_CODE_OK");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VPP_HIGH), "no MSG_ERR_VPP_HIGH on an in-range reading");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_WARN_VPP_HIGH), "no MSG_WARN_VPP_HIGH on an in-range reading");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_WARN_VPP_LOW), "no MSG_WARN_VPP_LOW on an in-range reading");
}

/* ─────────────────────────────────────────────────────────────────────────
 * The under-voltage severity pairing.
 * Before this case, nothing anywhere asserted that MSG_WARN_VPP_LOW is
 * emitted or that it carries RESPONSE_CODE_WARNING -- the only prior
 * reference to MSG_WARN_VPP_LOW in this whole tree was the negative leg in
 * test_vpp04_d above (asserting it is ABSENT on an in-range reading).
 * Severity rides ENTIRELY in the message id here: LOG_WARN_ID_BYTES
 * (include/logging_id.h:119) and LOG_ERROR_ID_BYTES (include/logging_id.h:110)
 * are the SAME alias of LOG_ID_BYTES, so the response_code assertion below
 * structurally cannot see a transposed id, and the id assertions cannot see
 * a swapped response_code -- the pair together is the oracle, neither half
 * alone. This suite runs only under native_loop_v131, whose own
 * platformio.ini comment says NO CI COVERAGE, so this case is a local-run
 * obligation, not CI-visible evidence.
 * ───────────────────────────────────────────────────────────────────────── */
void test_vpp04_e_undervoltage_warning_pairing_fires_by_id_with_payload_shape(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;  /* mandatory: on
        REVISION_0 eprom_check_vpp takes the early return at eprom.cpp:334-338
        and never reaches the under-voltage compare at all. */
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    /* 12349 is one mV inside the under-voltage boundary: the arm fires when
     * vpp_mv < handle->vpp_mv * 95 / 100, and with setpoint 13000 that
     * threshold is 13000 * 95 / 100 == 12350, so 12349 fires and 12350 would
     * not -- pins the boundary, not a wildly out-of-range value. */
    set_mock_vpp_mv(12349);
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "an injected 12349 mV reading (setpoint 13000, boundary 12350) must warn with RESPONSE_CODE_WARNING");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_WARN_VPP_LOW),
        "MSG_WARN_VPP_LOW (0x81) must be logged exactly once, BY ID -- no test in this tree asserted "
        "this positively before this plan");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VPP_HIGH),
        "the under-voltage WARN fork must NOT also log the over-voltage ERROR id -- this is what a "
        "transposed (MSG_WARN_VPP_LOW, RESPONSE_CODE_WARNING) pair trips");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_WARN_VPP_HIGH),
        "the under-voltage WARN fork must NOT also log the over-voltage WARN id -- the other half of "
        "what a transposed pair trips");
    int idx = find_logged_id(MSG_WARN_VPP_LOW);
    TEST_ASSERT_TRUE_MESSAGE(idx >= 0, "MSG_WARN_VPP_LOW must actually be present in the logged-id stream");
    TEST_ASSERT_EQUAL_MESSAGE(8, logged_id_param_count(idx),
        "the WARN frame must carry the 8 payload bytes eprom.cpp's under-voltage arm (LOG_WARN_ID_BYTES) emits");
}

/* ─────────────────────────────────────────────────────────────────────────
 * The flash_intel.cpp twin of the under-voltage severity pairing above.
 * Reading drive_vpp_init and configure_memory suggested
 * flash_intel_write_init's own VPP check is reachable from this suite
 * (protocol 0x10 routes configure_memory -> configure_flash_intel ->
 * flash_intel_write_init for CMD_WRITE, and drive_vpp_init drives exactly
 * that init path). Reached successfully -- see the SUMMARY for the full
 * record. FLAG_SKIP_BLANK_CHECK in the ctrl_flags slot is load-bearing:
 * without it flash_intel_write_init continues past the VPP check into
 * mem_util_blank_check, which needs data mocks this case does not set and
 * can overwrite response_code. chip_id left at 0 (make_vpp_handle's default)
 * skips flash_intel_check_chip_id for the same reason.
 * ───────────────────────────────────────────────────────────────────────── */
void test_vpp04_f_flash_intel_undervoltage_warning_pairing(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;  /* this file's own
        REVISION_0 early return applies to flash_intel_check_vpp too. */
    firestarter_handle_t h = make_vpp_handle(0x10, 32, 262144, 100, 12000, FLAG_SKIP_BLANK_CHECK, VPP_BUS_CONFIG_0x08);
    /* 11399 is one mV inside the 12000 * 95 / 100 == 11400 boundary. */
    set_mock_vpp_mv(11399);
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_WARNING, h.response_code,
        "an injected 11399 mV reading (setpoint 12000, boundary 11400) must warn with RESPONSE_CODE_WARNING");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_WARN_VPP_LOW),
        "MSG_WARN_VPP_LOW (0x81) must be logged exactly once, BY ID, on the flash_intel.cpp twin of the "
        "eprom.cpp under-voltage arm -- this narrows coverage ceiling 2, it does not remove it: the "
        "over-voltage arm and the rest of the flash_intel.cpp VPP path stay uncovered");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VPP_HIGH),
        "the under-voltage WARN fork must NOT also log the over-voltage ERROR id");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_WARN_VPP_HIGH),
        "the under-voltage WARN fork must NOT also log the over-voltage WARN id");
    int idx = find_logged_id(MSG_WARN_VPP_LOW);
    TEST_ASSERT_TRUE_MESSAGE(idx >= 0, "MSG_WARN_VPP_LOW must actually be present in the logged-id stream");
    TEST_ASSERT_EQUAL_MESSAGE(8, logged_id_param_count(idx),
        "the WARN frame must carry the 8 payload bytes flash_intel_check_vpp's under-voltage arm (LOG_WARN_ID_BYTES) emits");
}

void test_vpp03_case_e_cmd_erase_control_stream_is_pinned_pre_rewrite(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, FLAG_SKIP_BLANK_CHECK, VPP_BUS_CONFIG_0x07);
    h.cmd = CMD_ERASE;  /* MUST be set before configure_memory so configure_eprom installs eprom_erase_execute as firestarter_operation_main */
    set_mock_vpp_mv(13000);  /* in-range; this command never reaches eprom_check_vpp, kept for hygiene with the rest of the suite */

    configure_memory(&h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    clear_timings();
    clear_logged_ids();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- soundness precondition for the enumerated sequence below");

    int n = control_write_count();
    TEST_ASSERT_EQUAL_MESSAGE(4, n, "control_write_count -- measured pre-rewrite literal (CMD_ERASE)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x80, control_write_value(0), "control write 0 -- measured pre-rewrite literal (regulator on, eprom_internal_erase:399)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x90, control_write_value(1), "control write 1 -- measured pre-rewrite literal (first set_address; A17 forced for pins==28)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x96, control_write_value(2), "control write 2 -- measured pre-rewrite literal (A9|VPE asserted for the erase pulse, eprom_internal_erase:402)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x10, control_write_value(3), "control write 3 -- measured pre-rewrite literal (eprom_internal_erase's own disable at :409)");

    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after CMD_ERASE must have CTRL_VPP_REGULATOR_ENABLE clear -- the property :409 provides today and must keep providing");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_A9_ENABLE) == 0,
        "the LAST control value after CMD_ERASE must have CTRL_VPP_A9_ENABLE clear");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPE_ENABLE) == 0,
        "the LAST control value after CMD_ERASE must have CTRL_VPE_ENABLE clear");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Debug session w27c512-devtest-all-bad -- the CE ERASE pulse width.
 *
 * `eprom_internal_erase` implements the Winbond 27C/27E electrical-erase
 * algorithm: OE/VPP at VPE, A9 at VPE, address 0x0000, then CE pulsed low.
 * The W27C512 datasheet's CE erase pulse width T_PWE is 95 ms min / 100 ms
 * typ / 105 ms max. Before this fix the function spent `handle->pulse_delay`
 * -- the per-byte PROGRAM pulse width, 100 us for W27C512 -- with CE low,
 * i.e. ~950x below the datasheet MINIMUM, so the part only ever partially
 * erased. That is what made `dev test w27c512` report write/verify/erase/
 * blank-check all BAD: CMD_ERASE installs mem_util_blank_check as its END
 * phase and eprom_write_init runs erase-then-blank-check, so one partial
 * erase fails four steps.
 *
 * This case measures the CE-low interval from the recorded stream rather
 * than asserting a literal at the call site, so it stays true if the
 * constant is ever re-expressed (us vs ms, helper vs bare delay). It is
 * NOT a control-value test -- case E above already pins those four writes
 * and this case deliberately leaves them alone.
 *
 * Both bounds are asserted. The upper bound is not decoration: T_PWE has a
 * datasheet MAX, and over-erasing a flotox cell shifts it toward depletion,
 * so "make it bigger to be safe" is the wrong instinct here and this case
 * exists to refuse it.
 * ───────────────────────────────────────────────────────────────────────── */
void test_erase_ce_pulse_width_is_the_datasheet_erase_pulse_not_the_program_pulse(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    /* pulse_delay = 100 us -- exactly W27C512's DB `pulse_duration_us`, so a
     * regression that re-reads the program pulse here reproduces the shipped
     * defect's own number and this case names it in the failure message. */
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, FLAG_SKIP_BLANK_CHECK, VPP_BUS_CONFIG_0x07);
    h.cmd = CMD_ERASE;
    set_mock_vpp_mv(13000);

    configure_memory(&h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    clear_timings();
    clear_logged_ids();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- soundness precondition");
    TEST_ASSERT_EQUAL_MESSAGE(0, timing_overflowed(), "timing_overflowed -- soundness precondition");

    /* Locate the CE-low / CE-high strobe pair. rurp_chip_enable() drives
     * CHIP_ENABLE to 0 (active low) and rurp_chip_disable() drives it to 1. */
    int ce_low = -1, ce_high = -1;
    for (int i = 0; i < strobe_count(); i++) {
        if (strobe_kind(i) != STROBE_KIND_PIN || strobe_pin(i) != CHIP_ENABLE) continue;
        if (ce_low < 0 && strobe_value(i) == 0) { ce_low = i; continue; }
        if (ce_low >= 0 && strobe_value(i) != 0) { ce_high = i; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(ce_low >= 0, "no CHIP_ENABLE-low strobe found -- the erase never pulsed CE at all");
    TEST_ASSERT_TRUE_MESSAGE(ce_high > ce_low, "no CHIP_ENABLE-high strobe after the CE-low strobe -- the erase pulse never ended");

    /* Sum every delay pushed between those two strobes. A timing entry's seq
     * is s_strobe_count AT PUSH TIME, so an entry pushed after strobe k-1 and
     * before strobe k carries seq == k: the CE-low..CE-high window is
     * seq in [ce_low+1, ce_high]. DELAY_MS entries carry milliseconds in
     * `us`, so they are scaled here. */
    uint32_t ce_low_us = 0;
    for (int i = 0; i < timing_count(); i++) {
        int seq = timing_after_strobe(i);
        if (seq < ce_low + 1 || seq > ce_high) continue;
        ce_low_us += (timing_kind(i) == TIMING_KIND_DELAY_MS) ? timing_us(i) * 1000UL : timing_us(i);
    }

    TEST_ASSERT_TRUE_MESSAGE(ce_low_us >= 95000UL,
        "the CE erase pulse is shorter than the W27C512 datasheet T_PWE MINIMUM of 95 ms. "
        "A measurement of 100 us means eprom_internal_erase is spending handle->pulse_delay -- "
        "the per-BYTE PROGRAM pulse -- as the erase pulse again (debug session w27c512-devtest-all-bad)");
    TEST_ASSERT_TRUE_MESSAGE(ce_low_us <= 105000UL,
        "the CE erase pulse is longer than the W27C512 datasheet T_PWE MAXIMUM of 105 ms -- "
        "over-erase shifts a flotox cell toward depletion; this bound is deliberate, do not raise it");
}

void test_vpp03_case_i_cmd_check_chip_id_control_stream_is_pinned_pre_rewrite(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, FLAG_SKIP_BLANK_CHECK, VPP_BUS_CONFIG_0x07);
    h.cmd = CMD_CHECK_CHIP_ID;  /* MUST be set before configure_memory so configure_eprom installs eprom_check_chip_id_execute as firestarter_operation_main */
    h.chip_id = 0xFFFF;  /* the value eprom_get_chip_id will read back: addresses 0x0000 and 0x0001 are both unseeded, and the read-back model returns 0xFF for an unseeded address (host_stubs.cpp's rurp_read_data_buffer) */
    set_mock_vpp_mv(13000);

    configure_memory(&h);
    reset_register_cache(0x00, 0x00, 0x00);
    clear_strobes();
    clear_timings();
    clear_logged_ids();
    h.firestarter_operation_main(&h);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "response_code must NOT be RESPONSE_CODE_ERROR -- proves the drive actually reached eprom_internal_check_chip_id's comparison (0xFFFF == 0xFFFF) rather than bailing out earlier");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- soundness precondition for the enumerated sequence below");

    int n = control_write_count();
    TEST_ASSERT_EQUAL_MESSAGE(4, n, "control_write_count -- measured pre-rewrite literal (CMD_CHECK_CHIP_ID)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x80, control_write_value(0), "control write 0 -- measured pre-rewrite literal (regulator on, eprom_get_chip_id:320)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x82, control_write_value(1), "control write 1 -- measured pre-rewrite literal (A9 additionally asserted, eprom_get_chip_id:323)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x92, control_write_value(2), "control write 2 -- measured pre-rewrite literal (first get_data's own set_address; A17 forced for pins==28)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x10, control_write_value(3), "control write 3 -- measured pre-rewrite literal (eprom_get_chip_id's own disable at :327)");

    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after CMD_CHECK_CHIP_ID must have CTRL_VPP_REGULATOR_ENABLE clear -- the property :327 provides today and must keep providing");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_A9_ENABLE) == 0,
        "the LAST control value after CMD_CHECK_CHIP_ID must have CTRL_VPP_A9_ENABLE clear");
}

static rurp_register_t resolver_mask_for(uint32_t protocol, uint32_t ctrl_flags) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.ctrl_flags = ctrl_flags;
    return eprom_hv_route_mask(&h);
}

void test_vpp01_resolver_0x07_noflags_returns_route_mask(void) {
    TEST_ASSERT_EQUAL_MESSAGE(EPROM_HV_ROUTE_MASK, resolver_mask_for(0x07, 0),
        "row (0x07, no flags): eprom_hv_route_mask must return EPROM_HV_ROUTE_MASK -- vpp_path is VPP_PATH_DROP_RESISTOR");
}

void test_vpp01_resolver_0x08_noflags_returns_route_mask(void) {
    TEST_ASSERT_EQUAL_MESSAGE(EPROM_HV_ROUTE_MASK, resolver_mask_for(0x08, 0),
        "row (0x08, no flags): eprom_hv_route_mask must return EPROM_HV_ROUTE_MASK -- vpp_path is VPP_PATH_DROP_RESISTOR");
}

void test_vpp01_resolver_0x0b_noflags_returns_regulator_only(void) {
    TEST_ASSERT_EQUAL_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, resolver_mask_for(0x0B, 0),
        "row (0x0B, no flags): eprom_hv_route_mask must return CTRL_VPP_REGULATOR_ENABLE EXACTLY, drop bit ABSENT -- vpp_path is VPP_PATH_DIRECT_VPE");
}

void test_vpp01_resolver_0x07_vpeasvpp_returns_regulator_only(void) {
    TEST_ASSERT_EQUAL_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, resolver_mask_for(0x07, FLAG_VPE_AS_VPP),
        "row (0x07, FLAG_VPE_AS_VPP): D-06 -- the flag overrides the table's drop-resistor default and forces the direct-VPE path");
}

void test_vpp01_resolver_0x08_vpeasvpp_returns_regulator_only(void) {
    TEST_ASSERT_EQUAL_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, resolver_mask_for(0x08, FLAG_VPE_AS_VPP),
        "row (0x08, FLAG_VPE_AS_VPP): D-06 -- the flag overrides the table's drop-resistor default and forces the direct-VPE path");
}

void test_vpp01_resolver_0x0b_vpeasvpp_returns_regulator_only(void) {
    /* The flag AGREES with the table on this row (0x0B's own vpp_path is
     * already VPP_PATH_DIRECT_VPE) -- pins that the override does not
     * corrupt an already-direct row when it is redundant with the table. */
    TEST_ASSERT_EQUAL_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, resolver_mask_for(0x0B, FLAG_VPE_AS_VPP),
        "row (0x0B, FLAG_VPE_AS_VPP): the flag agrees with the table -- must still return CTRL_VPP_REGULATOR_ENABLE, not a corrupted mask");
}

void test_vpp01_resolver_unrecognised_protocol_fails_closed_to_route_mask(void) {
    TEST_ASSERT_EQUAL_MESSAGE(EPROM_HV_ROUTE_MASK, resolver_mask_for(0x99, 0),
        "row (0x99, unrecognised protocol, no flags): fail-closed NULL-row arm must return EPROM_HV_ROUTE_MASK -- unreachable through any drive");
}

void test_vpp01_resolver_0x0b_result_differs_from_0x07_result(void) {
    /* Mandatory non-vacuity leg: without this, a resolver that always
     * returned EPROM_HV_ROUTE_MASK (a constant) would satisfy six of the
     * seven rows above -- this shows the 0x0B row's direct-VPE result
     * genuinely differs from the 0x07 row's drop-resistor result. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(resolver_mask_for(0x07, 0), resolver_mask_for(0x0B, 0),
        "0x0B (direct VPE) must differ from 0x07 (drop resistor) -- the mandatory non-vacuity leg for the truth table above");
}

/* VPP_BUS_CONFIG_0x0B carries a nonzero static_high_mask (0x00002000UL,
 * bit 13) -- mem_util_remap_address_bus ORs it into every address
 * unconditionally, on both the read and write path (memory.cpp:350), so the
 * read-back model's key for a real (handle-level) address A on this
 * bus_config is A + 0x2000, not A. Mirrors test_loop_eprom_v131.cpp's
 * identical k0b() helper -- this is the first 0x0B WRITE this suite drives;
 * VPP_BUS_CONFIG_0x07 and _0x08 both carry static_high_mask == 0, so
 * neither needs this adjustment. */
static uint16_t vpp_k0b(uint32_t real_addr) {
    return (uint16_t)(real_addr + 0x2000UL);
}

void test_vpp01_route_0x0b_takes_the_direct_path(void) {
    /* 0x0B's own table row is VPP_PATH_DIRECT_VPE -- the resolved mask must
     * reach the wire with NO drop bit in any control value. 500us is
     * MANDATORY, not incidental: 0x0B is the only shipped row with
     * energy_cap_us > 0 (50000) -- a wider pulse would be refused
     * pre-flight by configure_eprom's Refusal 2 and this case would
     * measure nothing at all. */
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x0B, 24, 2048, 500, 13000, 0, VPP_BUS_CONFIG_0x0B);
    const uint8_t block[4] = {0x3C, 0x55, 0xAA, 0x0F};
    for (int i = 0; i < 4; i++) {
        vpp_readback_seed(vpp_k0b((uint32_t)i), block[i], 1, 0xFFFF);
    }
    drive_vpp_write(&h, 0, block, 4);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code, "response_code -- the block must converge");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- small block, must be sound");

    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "non-vacuity: the block must have produced at least one CONTROL write");
    bool saw_regulator_set = false;
    for (int i = 0; i < n; i++) {
        int v = control_write_value(i);
        char msg[128];
        snprintf(msg, sizeof(msg), "control write %d (0x%02X) must NOT carry CTRL_VPP_VPE_DROP_ENABLE_REV2 -- 0x0B takes the direct-VPE path, no drop bit", i, v);
        TEST_ASSERT_TRUE_MESSAGE(v >= 0 && (v & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0, msg);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_regulator_set = true; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_regulator_set,
        "non-vacuity partner: at least one control value must carry CTRL_VPP_REGULATOR_ENABLE -- otherwise the 'no drop bit' walk above is vacuously true of a register that was never energised at all");
}

void test_vpp01_route_vpeasvpp_forces_the_direct_path_onto_0x07(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, FLAG_VPE_AS_VPP, VPP_BUS_CONFIG_0x07);
    const uint8_t block[4] = {0x3C, 0x55, 0xAA, 0x0F};
    const uint16_t converge_after[4] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        vpp_readback_seed((uint16_t)i, block[i], converge_after[i], 0xFFFF);
    }
    drive_vpp_write(&h, 0, block, 4);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code, "response_code -- the block must converge");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- small block, must be sound");

    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "non-vacuity: the block must have produced at least one CONTROL write");
    bool saw_regulator_set = false;
    for (int i = 0; i < n; i++) {
        int v = control_write_value(i);
        char msg[160];
        snprintf(msg, sizeof(msg), "control write %d (0x%02X) must NOT carry CTRL_VPP_VPE_DROP_ENABLE_REV2 -- FLAG_VPE_AS_VPP (D-06) forces the direct-VPE path onto this 0x07 handle", i, v);
        TEST_ASSERT_TRUE_MESSAGE(v >= 0 && (v & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0, msg);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_regulator_set = true; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_regulator_set,
        "non-vacuity partner: at least one control value must carry CTRL_VPP_REGULATOR_ENABLE");
}

void test_vpp01_route_0x08_on_rev1_still_strips_the_drop_bit(void) {
    rurp_get_config()->hardware_revision = REVISION_1;
    firestarter_handle_t h = make_vpp_handle(0x08, 32, 262144, 100, 13000, 0, VPP_BUS_CONFIG_0x08);
    const uint8_t block[4] = {0x3C, 0x55, 0xAA, 0x0F};
    const uint16_t converge_after[4] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        vpp_readback_seed((uint16_t)i, block[i], converge_after[i], 0xFFFF);
    }
    drive_vpp_write(&h, 0, block, 4);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code, "response_code -- the block must converge");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- small block, must be sound");

    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "non-vacuity: at least the top-of-block assert (drop SET) and the block's first set_address must both have written CONTROL");

    int v0 = control_write_value(0);
    TEST_ASSERT_TRUE_MESSAGE(v0 >= 0 && (v0 & CTRL_VPP_VPE_DROP_ENABLE_REV1) != 0,
        "control write 0 (the top-of-block assert) must have CTRL_VPP_VPE_DROP_ENABLE_REV1 SET -- the non-vacuity partner proving the drop route was asserted at all");

    for (int i = 1; i < n; i++) {
        int v = control_write_value(i);
        char msg[192];
        snprintf(msg, sizeof(msg), "control write %d (0x%02X) must have CTRL_VPP_VPE_DROP_ENABLE_REV1 CLEAR -- on Rev 1 the drop bit and A16 share physical 0x01, so D-02's preserve mask keeps today's stripping outside the top-of-block assert", i, v);
        TEST_ASSERT_TRUE_MESSAGE(v >= 0 && (v & CTRL_VPP_VPE_DROP_ENABLE_REV1) == 0, msg);
    }
}

/* The largest control-write index whose stream position (control_write_
 * strobe_index) is BEFORE strobe_idx -- i.e. the physical control byte in
 * effect at that stream position. Indices are monotonic (each subsequent
 * control write happens strictly later in the stream), so this is simply
 * the last index whose position precedes strobe_idx. */
static int last_control_write_index_before_strobe(int strobe_idx) {
    int n = control_write_count();
    int result = -1;
    for (int i = 0; i < n; i++) {
        int cw_strobe_idx = control_write_strobe_index(i);
        if (cw_strobe_idx >= 0 && cw_strobe_idx < strobe_idx) {
            result = i;
        }
    }
    return result;
}

void test_vpp03_check_vpp_measures_the_route_the_write_path_applies_at_the_first_pulse(void) {
    const rurp_register_t route_mask = (rurp_register_t)(CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE_REV2);

    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h_check = make_vpp_handle(0x08, 32, 262144, 100, 13000, 0, VPP_BUS_CONFIG_0x08);
    set_mock_vpp_mv(13000);
    drive_vpp_init(&h_check);

    int leg_a_raw = control_write_value(0);
    TEST_ASSERT_TRUE_MESSAGE(leg_a_raw >= 0, "non-vacuity guard 1: leg A's control_write_value(0) must be decodable (not -1) -- eprom_check_vpp's own route assert");
    rurp_register_t leg_a_masked = (rurp_register_t)(leg_a_raw & route_mask);

    /* Non-vacuity guard 3 (checked on leg A ONLY, deliberately: leg A is
     * measured via eprom_check_vpp, untouched by the write-path planted
     * violation below, so this guard stays green under the plant and the
     * plant's failure lands on the EQUALITY assertion instead, exactly as
     * intended). The shared masked value must be non-zero and carry BOTH
     * bits 0x08's table row selects -- without this, an equality of two
     * zeros would pass vacuously. */
    TEST_ASSERT_EQUAL_MESSAGE(route_mask, leg_a_masked,
        "non-vacuity guard 3: leg A's masked value must carry BOTH CTRL_VPP_REGULATOR_ENABLE and CTRL_VPP_VPE_DROP_ENABLE_REV2 -- the specific route 0x08's table row selects");

    /* Leg B -- the applied route at the first GENUINE program pulse. A
     * SECOND, identically-parameterised handle. */
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h_write = make_vpp_handle(0x08, 32, 262144, 100, 13000, 0, VPP_BUS_CONFIG_0x08);
    const uint8_t block[4] = {0x3C, 0x55, 0xAA, 0x0F};
    const uint16_t converge_after[4] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        vpp_readback_seed((uint16_t)i, block[i], converge_after[i], 0xFFFF);
    }
    drive_vpp_write(&h_write, 0, block, 4);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h_write.response_code, "leg B: the block must converge -- a failed write is not 'the applied route at the first pulse'");

    int first_pulse_idx = first_genuine_pulse_strobe_index(block, 4);
    TEST_ASSERT_TRUE_MESSAGE(first_pulse_idx >= 0, "non-vacuity guard 2a: a genuine chip-data pulse must have been recorded");

    int last_cw_before_pulse = last_control_write_index_before_strobe(first_pulse_idx);
    TEST_ASSERT_TRUE_MESSAGE(last_cw_before_pulse >= 0, "non-vacuity guard 2b: the located control-write index must not be negative -- at least the top-of-block assert must precede the first genuine pulse");

    int leg_b_raw = control_write_value(last_cw_before_pulse);
    TEST_ASSERT_TRUE_MESSAGE(leg_b_raw >= 0, "leg B: the located control write must be a genuine, decodable value");
    rurp_register_t leg_b_masked = (rurp_register_t)(leg_b_raw & route_mask);

    TEST_ASSERT_EQUAL_MESSAGE(leg_a_masked, leg_b_masked,
        "leg A (the measured route) must equal leg B (the applied route at the first genuine pulse) -- before this phase they differed on this exact row: eprom_check_vpp measured 0x08 with the drop bit ON (eprom.cpp:345, pre-142-04 line numbers) while eprom_write_execute stripped it before the first pulse (:218), so the measured-and-validated voltage was not the voltage applied");
}

void test_vpp02_x4_the_final_pass_verify_failure_disables_the_route(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13000);

    const uint8_t block[4] = {0x3C, 0x55, 0xAA, 0x0F};
    /* Byte 0: 0xFF on the skip-check read (read_count=0 < converge_after=1);
     * matches on the verify read after its single pulse (read_count=1, not
     * < 1, not >= mismatch_from=2 -> target); MISMATCHES on the final
     * full-block pass (read_count=2 >= mismatch_from=2 -> ~target). THIS IS
     * THE LEG THE PLAN-142-01 READ-BACK EXTENSION EXISTS FOR: the sibling
     * suite's unbounded model returns target on every read after
     * convergence, so a final-pass mismatch would be unreachable there and
     * this case could never fail for the right reason. */
    vpp_readback_seed(0, block[0], 1, 2);
    /* Bytes 1-3: converge and STAY converged (the never-mismatch sentinel)
     * -- the ONLY MSG_ERR_VERIFY trigger is byte 0. */
    vpp_readback_seed(1, block[1], 1, 0xFFFF);
    vpp_readback_seed(2, block[2], 1, 0xFFFF);
    vpp_readback_seed(3, block[3], 1, 0xFFFF);
    drive_vpp_write(&h, 0, block, 4);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "response_code -- the final-pass mismatch must abort with an error");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_ERR_VERIFY), "exactly one MSG_ERR_VERIFY frame -- pins the case to the exit it claims");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_MAX_PULSES), "must NOT also fire MSG_ERR_MAX_PULSES -- every byte converges within its single pulse");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_ENERGY_CAP), "must NOT also fire MSG_ERR_ENERGY_CAP -- 0x07 ships energy_cap_us=0 (uncapped)");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- small block, must be sound");

    /* SS B-9 idiom: last-clear + paired non-vacuity. */
    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "non-vacuity: at least the top-of-block assert and the wrapper's error-exit disable must both have written CONTROL");
    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after the final-pass verify failure must have CTRL_VPP_REGULATOR_ENABLE CLEAR -- this exit disabled NOTHING before this phase; eprom_write_execute's wrapper is what clears it now");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0,
        "the LAST control value after the final-pass verify failure must ALSO have the drop bit CLEAR");
    bool saw_earlier_set = false;
    for (int i = 0; i < n - 1; i++) {
        int v = control_write_value(i);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_earlier_set = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_earlier_set,
        "an EARLIER control value must have CTRL_VPP_REGULATOR_ENABLE SET -- otherwise the 'last value clear' assertion is vacuously true of a register that was never energised at all");
}

void test_vpp02_x3_the_energy_cap_exit_disables_the_route(void) {
    /* 0x0B is the ONLY shipped row with energy_cap_us > 0 (50000, which at
     * 500us pulses binds at exactly 100 pulses) -- MSG_ERR_ENERGY_CAP is
     * therefore unreachable on 0x07/0x08 BY DATA, not by accident (both
     * ship energy_cap_us=0, uncapped).
     *
     * T-141-CAP (test_loop_eprom_v131.cpp's own named finding, reused
     * here verbatim): a 100-pulse block emits far more strobe/timing
     * entries than the recorders' 512-entry caps, so strobe_overflowed()
     * WILL be nonzero here, legitimately -- the tail is dropped, the
     * prefix stays valid. This case therefore does NOT read
     * control_write_value(n-1) as "the last write": under overflow that
     * index no longer names the genuine final write at all. Two oracles
     * stay sound instead: control_write_value(0) (the top-of-block
     * assert, always captured intact long before 512 entries could ever
     * accumulate) for the non-vacuity partner, and the handle's own
     * control-register CACHE, read directly via
     * firestarter_get_control_register -- unbounded, and always the true
     * final LOGICAL state regardless of how many strobes overflowed
     * (production's own top-of-block gate reads the identical cache the
     * identical way). */
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x0B, 24, 2048, 500, 13000, 0, VPP_BUS_CONFIG_0x0B);
    const uint8_t block[1] = {0x3C};
    vpp_readback_seed(vpp_k0b(0), block[0], 65535, 0xFFFF);  /* never converges within any reachable read count */
    drive_vpp_write(&h, 0, block, 1);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "response_code -- the energy cap must abort with an error");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_ERR_ENERGY_CAP), "exactly one MSG_ERR_ENERGY_CAP frame -- pins the case to the exit it claims");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VERIFY), "must NOT also fire MSG_ERR_VERIFY -- 0x0B ships VERIFY_PER_PULSE, no final pass");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_MAX_PULSES), "must NOT also fire MSG_ERR_MAX_PULSES -- the energy cap binds first, at 100 pulses, well below max_pulses=255");

    /* Non-vacuity partner, prefix-safe (T-141-CAP): the top-of-block
     * assert (index 0) must have set the regulator. */
    int v0 = control_write_value(0);
    TEST_ASSERT_TRUE_MESSAGE(v0 >= 0 && (v0 & CTRL_VPP_REGULATOR_ENABLE) != 0,
        "control write 0 (the top-of-block assert) must have CTRL_VPP_REGULATOR_ENABLE SET -- the non-vacuity partner proving the route was asserted at all");

    /* The final-state assertion, read from the unbounded register cache
     * rather than a possibly-truncated strobe tail (T-141-CAP):
     * eprom_internal_report_budget_failure's own EPROM_HV_ALL_OFF_MASK
     * disable must have left the regulator (and the drop bit, vacuously
     * true on 0x0B, whose own route never sets it) CLEAR. Note these are
     * the LOGICAL macro names (not the _REV2 physical variants) -- the
     * cache holds the pre-remap logical value, the same value production's
     * own top-of-block gate check reads. */
    TEST_ASSERT_FALSE_MESSAGE(h.firestarter_get_control_register(&h, CTRL_VPP_REGULATOR_ENABLE),
        "after the energy-cap failure, CTRL_VPP_REGULATOR_ENABLE must be CLEAR in the control-register cache -- eprom_internal_report_budget_failure's own EPROM_HV_ALL_OFF_MASK disable");
    TEST_ASSERT_FALSE_MESSAGE(h.firestarter_get_control_register(&h, CTRL_VPP_VPE_DROP_ENABLE),
        "after the energy-cap failure, CTRL_VPP_VPE_DROP_ENABLE must ALSO be CLEAR -- vacuously true on 0x0B (whose own route never sets it), named explicitly rather than silently skipped");
}

void test_vpp02_e1_write_init_error_exit_leaves_no_route_asserted(void) {
    rurp_get_config()->hardware_revision = REVISION_2_2;
    firestarter_handle_t h = make_vpp_handle(0x07, 28, 65536, 100, 13000, 0, VPP_BUS_CONFIG_0x07);
    set_mock_vpp_mv(13501);  /* one mV past the over-voltage boundary -- eprom_check_vpp errors, eprom_write_init takes its early return */
    drive_vpp_init(&h);

    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code, "response_code -- the over-voltage refusal must abort eprom_write_init");
    TEST_ASSERT_EQUAL_MESSAGE(1, count_logged_id(MSG_ERR_VPP_HIGH), "exactly one MSG_ERR_VPP_HIGH frame -- pins the case to the exit it claims");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_VERIFY), "must NOT also fire MSG_ERR_VERIFY -- this exit never reaches the write-execute body at all");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_MAX_PULSES), "must NOT also fire MSG_ERR_MAX_PULSES -- this exit never reaches the write-execute body at all");
    TEST_ASSERT_EQUAL_MESSAGE(0, count_logged_id(MSG_ERR_ENERGY_CAP), "must NOT also fire MSG_ERR_ENERGY_CAP -- this exit never reaches the write-execute body at all");
    TEST_ASSERT_EQUAL_MESSAGE(0, strobe_overflowed(), "strobe_overflowed -- tiny drive, must be sound");

    int n = control_write_count();
    TEST_ASSERT_TRUE_MESSAGE(n >= 2, "non-vacuity: at least the over-voltage assert and its own disable must both have written CONTROL");
    int last = control_write_value(n - 1);
    TEST_ASSERT_TRUE_MESSAGE(last >= 0, "the last CONTROL write must be a genuine, decodable value");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_REGULATOR_ENABLE) == 0,
        "the LAST control value after write_init's error exit must have CTRL_VPP_REGULATOR_ENABLE CLEAR -- already true before this phase (C-3), eprom_check_vpp's own unconditional clear");
    TEST_ASSERT_TRUE_MESSAGE((last & CTRL_VPP_VPE_DROP_ENABLE_REV2) == 0,
        "the LAST control value after write_init's error exit must ALSO have the drop bit CLEAR");
    bool saw_earlier_set = false;
    for (int i = 0; i < n - 1; i++) {
        int v = control_write_value(i);
        if (v >= 0 && (v & CTRL_VPP_REGULATOR_ENABLE)) { saw_earlier_set = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_earlier_set,
        "an EARLIER control value must have CTRL_VPP_REGULATOR_ENABLE SET -- otherwise the 'last value clear' assertion is vacuously true of a register that was never energised at all");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_setup_leaves_the_harness_clean_and_the_composites_correct);

    RUN_TEST(test_vpp01_truthtable_pins28_rev2_2_drop_bit_present);
    RUN_TEST(test_vpp01_truthtable_pins28_rev1_drop_bit_present);
    RUN_TEST(test_vpp01_truthtable_pins32_rev2_0_drop_bit_present);
    RUN_TEST(test_vpp01_truthtable_pins32_rev2_2_drop_bit_present);
    RUN_TEST(test_vpp01_truthtable_pins32_rev2_3_drop_bit_present);
    RUN_TEST(test_vpp01_truthtable_pins32_rev1_drop_bit_absent);
    RUN_TEST(test_vpp01_truthtable_pins32_rev0_drop_bit_absent);
    RUN_TEST(test_vpp01_truthtable_pins32_revunknown_drop_bit_absent);
    RUN_TEST(test_vpp01_truthtable_pins32_rev2_2_preserve_never_introduces);
    RUN_TEST(test_vpp01_dip32_nonEprom_0x10_route_is_byte_identical_before_and_after);

    RUN_TEST(test_vpp04_a_overvoltage_refusal_fires_by_id_with_payload_shape);
    RUN_TEST(test_vpp04_b_no_hv_route_left_asserted_on_the_refusal_path);
    RUN_TEST(test_vpp04_c_flag_force_downgrades_to_warning_and_still_clears_the_route);
    RUN_TEST(test_vpp04_d_in_range_reading_fires_neither_error_nor_warning);

    /* The under-voltage severity pairing that nothing asserted before this
     * plan, on eprom.cpp and its flash_intel.cpp twin. */
    RUN_TEST(test_vpp04_e_undervoltage_warning_pairing_fires_by_id_with_payload_shape);
    RUN_TEST(test_vpp04_f_flash_intel_undervoltage_warning_pairing);

    RUN_TEST(test_vpp03_case_e_cmd_erase_control_stream_is_pinned_pre_rewrite);
    RUN_TEST(test_vpp03_case_i_cmd_check_chip_id_control_stream_is_pinned_pre_rewrite);

    /* Debug session w27c512-devtest-all-bad: the CE erase pulse width must be
     * the datasheet erase pulse, never handle->pulse_delay (the program pulse). */
    RUN_TEST(test_erase_ce_pulse_width_is_the_datasheet_erase_pulse_not_the_program_pulse);

    RUN_TEST(test_vpp01_resolver_0x07_noflags_returns_route_mask);
    RUN_TEST(test_vpp01_resolver_0x08_noflags_returns_route_mask);
    RUN_TEST(test_vpp01_resolver_0x0b_noflags_returns_regulator_only);
    RUN_TEST(test_vpp01_resolver_0x07_vpeasvpp_returns_regulator_only);
    RUN_TEST(test_vpp01_resolver_0x08_vpeasvpp_returns_regulator_only);
    RUN_TEST(test_vpp01_resolver_0x0b_vpeasvpp_returns_regulator_only);
    RUN_TEST(test_vpp01_resolver_unrecognised_protocol_fails_closed_to_route_mask);
    RUN_TEST(test_vpp01_resolver_0x0b_result_differs_from_0x07_result);
    RUN_TEST(test_vpp01_route_0x0b_takes_the_direct_path);
    RUN_TEST(test_vpp01_route_vpeasvpp_forces_the_direct_path_onto_0x07);
    RUN_TEST(test_vpp01_route_0x08_on_rev1_still_strips_the_drop_bit);

    RUN_TEST(test_vpp03_check_vpp_measures_the_route_the_write_path_applies_at_the_first_pulse);

    RUN_TEST(test_vpp02_x4_the_final_pass_verify_failure_disables_the_route);
    RUN_TEST(test_vpp02_x3_the_energy_cap_exit_disables_the_route);
    RUN_TEST(test_vpp02_e1_write_init_error_exit_leaves_no_route_asserted);

    return UNITY_END();
}
