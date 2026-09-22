/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Proves configure_eprom behavior BY SIDE-EFFECT via the recording bus stub:
 *
 *   POSITIVE tests (CMD_WRITE): configure_memory() + firestarter_operation_init()
 *     → eprom_write_init → eprom_generic_init → eprom_check_vpp
 *     → rurp_write_to_register(CONTROL_REGISTER, value | CTRL_VPP_REGULATOR_ENABLE)
 *     The recording must contain at least one CONTROL_REGISTER write with
 *     CTRL_VPP_REGULATOR_ENABLE set.
 *
 * Protocols covered: 0x07 (EPROM_STD), 0x08 (EPROM_QUICK), 0x0B (EPROM_LEGACY).
 *
 * VPP mechanism (from eprom.cpp source — not guessed):
 *   0x07/0x08: CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE
 *   0x0B:      CTRL_VPP_REGULATOR_ENABLE only (direct VPE path)
 * Both paths set CTRL_VPP_REGULATOR_ENABLE — we assert the common bit.
 *
 * NOTE: delay() must be mocked; eprom_check_vpp calls delay(100).
 * Hardware revision stub returns 1 (non-REVISION_0) via host_stubs.cpp so
 * eprom_check_vpp does not take the REV0 early-return path.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
#include "eprom.h"
#include "operation_utils.h"
#include "memory_utils.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

/* Debug session w27c512-write-slow-3x -- read-back model + saturation
 * reporter, compiled because host_stubs.cpp defines
 * HOST_STUBS_CUSTOM_READ_DATA_BUFFER. See that file for the byte-index
 * derivation and for why this invariant lives in THIS suite. */
extern "C" void val_readback_reset();
extern "C" void val_readback_seed(uint8_t idx, uint8_t target, uint8_t converge_after);
extern "C" int  val_recording_saturated();

/* 201-01 Task 1 -- address-keyed shadow model. See host_stubs.cpp for the
 * byte-index derivation and the recorder-saturation fallback. */
extern "C" void val_shadow_reset();
extern "C" void val_shadow_enable();
extern "C" void val_shadow_seed(uint32_t address, uint8_t value);

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by eprom_check_vpp (delay(100)) and eprom_write_execute
     * (delay(500)). Must be stubbed before any ArduinoFake virtual is called. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    /* Debug session w27c512-write-slow-3x: the write-cadence cases below drive
     * eprom_write_execute itself, which the pre-existing cases never did.
     * delayMicroseconds() is called per route settle and per program pulse;
     * millis() feeds the MSG_DATA_PROGRESS emit, which IS compiled on native
     * (only uno/uno328pb define SERIAL_ON_IO). millis() is pinned to a
     * constant so the time-keyed emit never fires -- it writes no register, so
     * it cannot perturb the counts, but a frozen clock keeps the recording
     * minimal and the cases deterministic. */
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    clear_bus_recording();
    val_readback_reset();
    val_shadow_reset();
}

void tearDown(void) {}

/* Build a zero-initialized handle with protocol, cmd, and a VPP setpoint of 0
 * (so eprom_check_vpp voltage comparison does not error on 0 mV reading). */
static firestarter_handle_t make_handle(uint32_t protocol, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = protocol;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv     = 0;  /* vpp setpoint=0 matches stub voltage=0: no warn/error */
    h.chip_id    = 0;  /* skip chip-ID branch */
    h.mem_size   = 65536; /* 64 KB — keeps blank_check from NULL-ptr in mock */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

/* ─── Helper: scan recording for CONTROL_REGISTER writes with VPP bit set ─── */
static bool recording_has_vpp_enable(uint8_t vpp_bit) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & vpp_bit)) {
            return true;
        }
    }
    return false;
}

/* NOTE: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION is defined —
 * it does not fit in uint8_t. The recording buffer stores uint8_t data values,
 * so CTRL_VPP_VPE_DROP_ENABLE cannot be detected via the 8-bit recording when
 * HARDWARE_REVISION is defined. Use CTRL_VPP_REGULATOR_ENABLE (0x80) and
 * CTRL_VPP_P1_ENABLE (0x08) which fit in 8 bits for VPP-enable detection. */
static bool recording_has_any_vpp_enable(void) {
    return recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE) ||
           recording_has_vpp_enable(CTRL_VPP_P1_ENABLE);
}

/* ─── POSITIVE tests: CMD_WRITE + init → VPP regulator must fire ─────────── */

/* Protocol 0x07 (EPROM_STD): write init must enable CTRL_VPP_REGULATOR_ENABLE. */
void test_eprom_0x07_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x07 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x07 write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* Protocol 0x08 (EPROM_QUICK): same mechanism as 0x07. */
void test_eprom_0x08_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x08 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x08 write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* Protocol 0x0B (EPROM_LEGACY): direct VPE path — CTRL_VPP_REGULATOR_ENABLE only. */
void test_eprom_0x0B_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x0B CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    TEST_ASSERT_TRUE_MESSAGE(
        recording_has_vpp_enable(CTRL_VPP_REGULATOR_ENABLE),
        "configure_eprom 0x0B write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* ─── NEGATIVE CONTROL: CMD_READ, configure-only — VPP must NOT fire ─────── */

void test_eprom_0x07_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x07, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x07 CMD_READ");
    /* Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION defined —
     * it cannot be detected via uint8_t recording; check 8-bit-fit VPP bits only. */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x07 CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "configure_eprom 0x07 CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
        }
    }
}

void test_eprom_0x08_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x08, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x08 CMD_READ");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x08 CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

void test_eprom_0x0B_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(0x0B, CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on protocol 0x0B CMD_READ");
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_eprom 0x0B CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

/* ═════════════════════════════════════════════════════════════════════════
 * WRITE-CADENCE INVARIANT (debug session w27c512-write-slow-3x)
 *
 * WHAT IS PINNED: the number of times eprom_write_execute ASSERTS the
 * program-voltage route over one block scales with the PASS count, never
 * with the programmed-byte count.
 *
 * WHY IT IS PINNED HERE, of all places: CI runs only `pio test -e native`
 * and `-e native_nodevtools`, which are now the only native envs. It does
 * not run check_size_baseline.py at
 * all. test_val_eprom is in BOTH pinned envs' test_filter, so this is the
 * only place an automated gate can see the cadence.
 *
 * WHY ROUTE ASSERTS AND NOT TIMING: this suite's recorder (HOST_STUBS_RECORD_
 * BUS) sees rurp_write_to_register calls only -- no data strobes, no pins, no
 * time. That is exactly enough. delayMicroseconds is mocked away, so settle
 * duration is not observable here, but each settle is emitted immediately
 * after a route assert, so counting rising edges of the route bit counts the
 * settles one-for-one. Counting RISING EDGES rather than values-with-the-bit-
 * set matters: mem_util_set_address writes CONTROL_REGISTER once per byte, so
 * values carrying the bit are plentiful in both cadences and would not
 * discriminate.
 *
 * The mask is CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE, not CTRL_VPE_ENABLE
 * alone: eprom_internal_set_control_register substitutes P1 for VPE whenever
 * using_p1_as_vpp(handle) holds. It does not hold for the 28-pin config here
 * (vpp_line 0xFF), so today only 0x04 is ever seen -- but masking both keeps
 * the count honest if this fixture is ever pointed at a P1-routed chip
 * instead of quietly going vacuous. Both bits fit in the recorder's uint8_t
 * data field (0x04, 0x08), unlike CTRL_VPP_VPE_DROP_ENABLE (0x100) -- see the
 * pre-existing note above.
 * ═════════════════════════════════════════════════════════════════════════ */

/* Mirrors host_stubs_common.inc's HOST_STUBS_MAX_RECORDING, restated because
 * a #define inside host_stubs.cpp's TU is not visible in this, a SEPARATE
 * translation unit -- the same convention test_trace_eprom_v131.cpp uses for
 * its own two cap constants. val_recording_saturated() is the authoritative
 * check; this literal only makes the failure message legible. */
#define VAL_EPROM_MAX_RECORDING 256

/* Copied VERBATIM from test_loop_eprom_v131.cpp's LOOP_BUS_CONFIG_0x07,
 * itself copied from test_trace_eprom_v131.cpp's V131_BUS_CONFIG_0x07. Do
 * NOT invent one: a zeroed bus_config is DEGENERATE, not an identity remap --
 * mem_util_remap_address_bus starts from `config.address_mask & address`, and
 * address_mask == 0 collapses every address to 0, which would send every byte
 * of the block to the same read-back slot. */
static const bus_config_t VAL_EPROM_BUS_CONFIG_0x07 = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF },
    0x0000FFFFUL,
    16,
    0xFF,
    0xFF,
    0x00000000UL
};

/* Rising edges of the program-route bit across recorded CONTROL_REGISTER
 * writes == the number of times the loop asserted the route. */
static int route_assert_count(void) {
    const uint8_t route = (uint8_t)(CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE);
    int asserts = 0;
    bool up = false;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) != CONTROL_REGISTER) continue;
        bool now = (recorded_data(i) & route) != 0;
        if (now && !up) asserts++;
        up = now;
    }
    return asserts;
}

/* Drives eprom_write_execute directly -- configure_memory then _main, never
 * _init. _init would run eprom_generic_init/eprom_check_vpp and add register
 * writes that have nothing to do with the loop's cadence, and on a 256-entry
 * recorder every avoidable write is budget. Mirrors test_loop_eprom_v131.cpp's
 * drive_loop_write for exactly the same reason. */
static void drive_val_write(firestarter_handle_t* h, const uint8_t* block, uint8_t n) {
    configure_memory(h);
    clear_bus_recording();
    h->address = 0;
    h->data_size = n;
    for (uint8_t i = 0; i < n; i++) {
        h->data_buffer[i] = (char)block[i];
    }
    h->firestarter_operation_main(h);
}

static firestarter_handle_t make_write_handle(void) {
    firestarter_handle_t h = {};
    h.protocol = 0x07;
    h.pins = 28;
    h.mem_size = 65536;
    h.pulse_delay = 100;
    h.bus_config = VAL_EPROM_BUS_CONFIG_0x07;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = 0;
    h.chip_id = 0;
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

/* Eight distinct non-0xFF targets, each mismatching on its first read and
 * converging after exactly one pulse. One pass suffices, so a per-PASS
 * cadence asserts the route ONCE and a per-BYTE cadence asserts it EIGHT
 * times. */
void test_writeperf_route_is_asserted_once_per_pass_not_once_per_byte(void) {
    const uint8_t block[8] = { 0x3C, 0x55, 0xAA, 0x0F, 0x11, 0x22, 0x44, 0x88 };
    firestarter_handle_t h = make_write_handle();
    val_readback_reset();
    for (uint8_t i = 0; i < 8; i++) {
        val_readback_seed(i, block[i], 1);
    }
    drive_val_write(&h, block, 8);

    TEST_ASSERT_FALSE_MESSAGE(val_recording_saturated(),
        "recorder saturated at " "256" " entries -- every count below would be silently wrong; shrink the block");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "non-vacuity: the block must actually CONVERGE, otherwise no pulse was ever verified and the counts below describe nothing");

    int asserts = route_assert_count();
    /* Non-vacuity floor: a loop that never asserted the route at all would
     * score 0 and satisfy any upper bound trivially. */
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(1, asserts,
        "non-vacuity: the program route must be asserted at least once -- 0 would make the bound below vacuous");
    /* The invariant, as an EXACT count: one pass, therefore one assert. */
    TEST_ASSERT_EQUAL_MESSAGE(1, asserts,
        "one pass must cost exactly ONE program-route assert -- 8 means the per-byte cadence (and its 1100us-per-byte settle) is back");
}

/* Same eight bytes, but byte 3 needs TWO pulses, forcing a second pass. The
 * discriminator: a per-PASS cadence goes 1 -> 2 (it tracks passes), while a
 * per-BYTE cadence goes 8 -> 9 (it tracks pulses). Asserting the exact value
 * pins the scaling law itself, not merely "fewer than the block length". */
void test_writeperf_route_assert_count_tracks_passes_not_pulses(void) {
    const uint8_t block[8] = { 0x3C, 0x55, 0xAA, 0x0F, 0x11, 0x22, 0x44, 0x88 };
    firestarter_handle_t h = make_write_handle();
    val_readback_reset();
    for (uint8_t i = 0; i < 8; i++) {
        val_readback_seed(i, block[i], (uint8_t)(i == 3 ? 2 : 1));
    }
    drive_val_write(&h, block, 8);

    TEST_ASSERT_FALSE_MESSAGE(val_recording_saturated(),
        "recorder saturated at " "256" " entries -- every count below would be silently wrong; shrink the block");
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, h.response_code,
        "non-vacuity: the block must actually CONVERGE, including the two-pulse byte");

    int asserts = route_assert_count();
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(1, asserts,
        "non-vacuity: the program route must be asserted at least once");
    TEST_ASSERT_EQUAL_MESSAGE(2, asserts,
        "two passes must cost exactly TWO program-route asserts -- 9 means the cadence tracks pulses (per-byte) instead of passes");
    /* Stated as a scaling law as well as an exact value, so the intent
     * survives someone re-tuning the fixture's block length. */
    TEST_ASSERT_LESS_THAN_MESSAGE(8, asserts,
        "route asserts must never scale with the programmed-byte count");
}

/* ═══════════════════════════════════════════════════════════════════════
 * 201-01 Task 1 -- positive control for the address-keyed shadow model.
 * ═══════════════════════════════════════════════════════════════════════ */

/* Proves the shadow model recovers a full absolute address rather than
 * aliasing modulo 16 like the legacy 16-slot model above. 0x20, 0x30 and
 * 0x1010 are all congruent to the seeded 0x10 modulo 16 -- under the old
 * model all three would incorrectly read back 0x00 too. */
void test_shadow_seed_is_address_keyed_not_modulo_aliased(void) {
    firestarter_handle_t h = {};
    h.protocol      = 0x07;
    /* cmd is never dispatched here -- this fixture only calls
     * configure_memory(&h) and then drives h.firestarter_get_data directly,
     * never h.firestarter_operation_main/init. Re-keyed in Phase 204
     * (FWCMD-01) from the standalone blank-check command's now-retired
     * ordinal to CMD_READ, a neutral surviving one; the value has no
     * bearing on this test's outcome either way. */
    h.cmd           = CMD_READ;
    h.mem_size      = 16384;
    h.ctrl_flags    = 0;
    h.chip_id       = 0;
    h.vpp_mv        = 0;
    h.response_code = RESPONSE_CODE_OK;
    /* Identity address mapping. A zeroed bus_config is DEGENERATE, not an
     * identity remap -- see VAL_EPROM_BUS_CONFIG_0x07's own comment above.
     * The plan text for this fixture did not list bus_config; without it
     * mem_util_remap_address_bus collapses every address passed below to
     * the same physical line, and the control could not tell 0x10 from
     * 0x20/0x30/0x1010. Recorded as a deviation. */
    h.bus_config = VAL_EPROM_BUS_CONFIG_0x07;
    configure_memory(&h);

    val_shadow_seed(0x10, 0x00);

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, h.firestarter_get_data(&h, 0x10),
        "the seeded address must read back the seeded value");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, h.firestarter_get_data(&h, 0x20),
        "0x20 is congruent to 0x10 modulo 16 -- a modulo-16-aliased model would wrongly read 0x00 here");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, h.firestarter_get_data(&h, 0x30),
        "0x30 is congruent to 0x10 modulo 16 -- same aliasing failure mode");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, h.firestarter_get_data(&h, 0x1010),
        "0x1010 is congruent to 0x10 modulo 16 -- same aliasing failure mode, at a larger address");

    TEST_ASSERT_FALSE_MESSAGE(val_recording_saturated(),
        "recorder saturated -- the addresses composed above would be unreliable");
}

/* ═══════════════════════════════════════════════════════════════════════
 * 201-01 Task 2 -- BLANK-02 contract freeze, written BEFORE the region split
 * lands in plans 201-03 / 201-04, so these characterize TODAY's behaviour
 * rather than tomorrow's (RESEARCH Pitfall 2).
 * ═══════════════════════════════════════════════════════════════════════ */

/* Region-check handle factory: unlike make_handle / make_write_handle above,
 * this clears ctrl_flags entirely so the blank-check axis is LIVE (both
 * FLAG_SKIP_BLANK_CHECK and FLAG_SKIP_ERASE clear, FLAG_CAN_ERASE clear too).
 * make_handle's FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE would make every case
 * below vacuous -- the whole point of these two cases is to drive the
 * blank-check path itself.
 *
 * bus_config = VAL_EPROM_BUS_CONFIG_0x07 for the same reason as the shadow
 * control above: the plan text for this factory did not list bus_config,
 * but an omitted (zeroed) one is degenerate, not an identity remap.
 * Recorded as a deviation. */
static firestarter_handle_t make_region_handle(uint32_t protocol, uint8_t cmd, uint32_t mem_size) {
    firestarter_handle_t h = {};
    h.protocol      = protocol;
    h.cmd           = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv        = 0;
    h.chip_id       = 0;
    h.mem_size      = mem_size;
    h.address       = 0;
    h.ctrl_flags    = 0;
    h.bus_config    = VAL_EPROM_BUS_CONFIG_0x07;
    return h;
}

/* Composes the FIRST absolute address seen in the current recording -- the
 * mirror image of rurp_read_data_buffer's backward (latest-write) scan in
 * host_stubs.cpp. Walking forward and taking the EARLIEST occurrence of each
 * register gives the address the very first mem_util_set_address call in
 * this recording targeted. See host_stubs.cpp for why CONTROL_REGISTER
 * stands in for the plan's "TOP_ADDRESS" -- no such register exists. */
static uint32_t first_recorded_address(void) {
    uint32_t lsb = 0, msb = 0, top = 0;
    bool have_lsb = false, have_msb = false, have_top = false;
    for (int i = 0; i < bus_recording_count() && !(have_lsb && have_msb && have_top); i++) {
        uint8_t reg = recorded_reg(i);
        if (!have_lsb && reg == LEAST_SIGNIFICANT_BYTE) {
            lsb = recorded_data(i);
            have_lsb = true;
        } else if (!have_msb && reg == MOST_SIGNIFICANT_BYTE) {
            msb = recorded_data(i);
            have_msb = true;
        } else if (!have_top && reg == CONTROL_REGISTER) {
            top = recorded_data(i);
            have_top = true;
        }
    }
    return lsb | (msb << 8) | ((top & 0x07) << 16);
}

/* D-15.1: pins the multi-call chunking contract of mem_util_blank_check
 * against UNMODIFIED firmware. 0x2A is neither 0 nor a chunk boundary, so a
 * restore that merely zeroes the cursor instead of restoring it would fail
 * the final assertion -- and this is the BLANK-02 contract verbatim. */
void test_blank_check_resumes_across_chunks_and_restores_the_cursor(void) {
    /* Re-keyed in Phase 204 (FWCMD-01, Fork C): the standalone blank-check
     * command's configure_eprom arm is gone, so nothing dispatches this
     * handle's operation-main pointer to the whole-device blank-check
     * function any more -- assign it directly instead. This test guards
     * the chunking and cursor-restore contract (BLANK-02), which survives
     * to Phase 205; cmd is CMD_READ, a neutral surviving ordinal, because
     * mem_util_blank_check_region no longer reads handle->cmd at all
     * (Phase 204 collapsed its two command-keyed branches to their
     * direct-emit arm). */
    firestarter_handle_t h = make_region_handle(0x07, CMD_READ, 16384);
    configure_memory(&h);
    configure_eprom(&h);  /* configure_memory already dispatches here for 0x07; explicit for clarity. */
    h.firestarter_operation_main = mem_util_blank_check;
    val_shadow_enable();
    h.address = 0x2A;
    clear_bus_recording();

    h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "call 1 must not error on a blank part");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8192, h.address,
        "call 1 must advance the cursor exactly one chunk (BLANK_CHECK_CHUNK_SIZE)");
    TEST_ASSERT_TRUE_MESSAGE(is_operation_in_progress(&h),
        "call 1 must leave the operation in progress -- more of the part remains to scan");

    h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "call 2 must not error on a blank part");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(16384, h.address,
        "call 2 must advance the cursor to exactly the second chunk boundary");
    TEST_ASSERT_TRUE_MESSAGE(is_operation_in_progress(&h),
        "call 2 must still be in progress -- the completion branch fires on the NEXT call");

    h.firestarter_operation_main(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "call 3 must not error");
    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&h),
        "call 3 must complete the operation -- handle.address has reached mem_size");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x2A, h.address,
        "call 3 must restore handle.address to its pre-call value -- this is the BLANK-02 contract verbatim");
}

/* FWBLANK-02 (Phase 205 Plan 03 Task 1) -- test_erase_end_blank_check_scans_from_zero
 * used to pin here that CMD_ERASE's blank-check completion arm
 * (firestarter_operation_end) always scans from address 0. That arm's
 * assignment is deleted by this commit -- configure_eprom's CMD_ERASE case
 * now assigns no firestarter_operation_end at all -- so the contract this
 * test asserted no longer exists to re-key onto anything. DELETED, not
 * re-anchored: see 205-03-SUMMARY.md. */

/* ═══════════════════════════════════════════════════════════════════════
 * 201-02 Task 2 -- D-16.1: the regression test whose absence is why backlog
 * 999.44 shipped. Drives eprom_write_init with the blank-check axis LIVE
 * against a part that is non-blank OUTSIDE the write's own target region.
 *
 * The positive case below is EXPECTED RED at this commit: today's
 * mem_util_blank_check always scans from address 0 across the WHOLE
 * device, so it has no notion of "the write's own region" to scope
 * against. Plan 201-03's commit greens it by teaching the blank check to
 * scope to [handle->address, handle->region_end) when region_end is
 * non-zero. The paired negative control below is GREEN both before and
 * after that fix: scoping the check is not deleting it, because a
 * programmed bit on a UV part cannot be un-programmed.
 * ═══════════════════════════════════════════════════════════════════════ */

/* A single call to firestarter_operation_init proves nothing here: the
 * blank check runs inside it, so is_operation_in_progress is TRUE after
 * call 1 and the seeded byte may live in a chunk the loop has not reached
 * yet (BLANK_CHECK_CHUNK_SIZE is 8192; mem_size here is 16384, i.e. two
 * chunks). Drive it to completion or to an error, with a hard cap so a
 * fixture that never converges fails loudly instead of hanging the suite.
 * unity_capped_iterations names which of the two D-16.1 cases hit the cap,
 * since both loops share this helper.
 *
 * DEVIATION, measured: clear_bus_recording() is called before EVERY
 * iteration, not just once before the loop. One BLANK_CHECK_CHUNK_SIZE
 * (8192-byte) chunk scan is 8192 * 3 = 24576 register writes -- see
 * test_erase_end_blank_check_scans_from_zero's own comment above -- which
 * blows past HOST_STUBS_MAX_RECORDING (4096) well inside a SINGLE chunk.
 * val_shadow's address-keyed read-back model (host_stubs.cpp) recovers the
 * current absolute address by scanning the recorder backward; once it
 * saturates, rurp_read_data_buffer() falls back to the last address it
 * recovered BEFORE saturation and repeats that byte for the rest of the
 * call, so any target address more than ~1365 entries into an uncleared
 * recording silently reads the wrong (stale) shadow slot instead of its
 * own. The negative control's target (0x2400, offset 1024 into chunk 2)
 * sits inside that 1365-entry window only if chunk 2's OWN call starts
 * from a freshly cleared recording -- otherwise chunk 1's already-saturated
 * recording is still active when chunk 2 begins, and the control silently
 * fails to catch its seeded byte at all. Clearing here, per call, keeps
 * every chunk's own address recovery valid for at least its first ~1365
 * bytes, which both D-16.1 targets (offset 16 and offset 1024) sit well
 * inside. */
static void unity_capped_iterations(firestarter_handle_t* h, const char* case_name) {
    /* do-while, deliberately: before the FIRST call, the operation has not
     * started yet, so is_operation_in_progress(h) reads false. A while-loop
     * on that condition would never call firestarter_operation_init at all
     * and both cases would spuriously read RESPONSE_CODE_OK, unchanged from
     * make_region_handle's default -- a silent false pass, not a real
     * exercise of the blank check. */
    int iterations = 0;
    do {
        iterations++;
        if (iterations > 8) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                "%s: firestarter_operation_init did not converge within 8 calls -- "
                "fixture is hung, not just slow", case_name);
            TEST_FAIL_MESSAGE(msg);
            return;
        }
        clear_bus_recording();
        h->firestarter_operation_init(h);
    } while (is_operation_in_progress(h) && h->response_code != RESPONSE_CODE_ERROR);
}

/* FWBLANK-01 (Phase 205 Plan 03 Task 1) -- RE-KEYED. This case used to
 * prove the NARROWER fact that a non-blank byte at 0x10 (16), OUTSIDE the
 * write's own target region [8192, 12288), did not refuse the write --
 * true because the (then region-scoped) check only looked inside the
 * region. That check is now gone entirely, so the narrower proof would
 * stay green for the wrong reason. Re-keyed to the STRONGER, positive
 * claim FWBLANK-01 actually makes: write-init performs no blank check at
 * all, so a non-blank byte anywhere on the device -- inside or outside
 * the region -- never refuses the write. region_end is an ABSOLUTE
 * EXCLUSIVE end address (C-3), never a length: h.address = 8192,
 * h.region_end = 12288.
 *
 * DEVIATION from the plan's literal action text: configure_eprom(&h) is NOT
 * called explicitly here. configure_memory() already dispatches to it for
 * protocol 0x07 (memory.cpp: `if (handle->protocol == PROTO_EPROM_28PIN ||
 * ...) { configure_eprom(handle); return; }`), and configure_eprom's own
 * body does `ep_set_control_register = handle->firestarter_set_control_register;
 * handle->firestarter_set_control_register = eprom_internal_set_control_register;`
 * -- a second, explicit call reads back the WRAPPER it just installed and
 * assigns it to ep_set_control_register, making eprom_internal_set_control_register
 * call itself. Every path this test exercises (eprom_write_init ->
 * eprom_generic_init -> eprom_check_vpp) calls
 * handle->firestarter_set_control_register, so the corrupted pointer
 * recurses until the stack overflows -- measured as a SIGSEGV. */
void test_write_init_performs_no_blank_check_at_all(void) {
    firestarter_handle_t h = make_region_handle(0x07, CMD_WRITE, 16384);
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_init,
        "CMD_WRITE must assign firestarter_operation_init -- a null pointer here means the "
        "fixture, not the production code, is wrong");

    h.address = 8192;
    h.region_end = 12288;

    val_shadow_enable();
    val_shadow_seed(0x10, 0x00);

    unity_capped_iterations(&h, "test_write_init_performs_no_blank_check_at_all");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FWBLANK-01: write-init performs no blank check at all, so a non-blank byte outside "
        "the write's own target region [8192, 12288) must not refuse the write. The host now "
        "owns this refusal.");
    TEST_ASSERT_FALSE_MESSAGE(val_recording_saturated(),
        "recorder saturated -- the addresses composed during this run would be unreliable");
}

/* FWBLANK-01 (Phase 205 Plan 03 Task 1) -- INVERTED. This case used to be
 * the paired negative control proving a non-blank byte INSIDE the write's
 * own target region [8192, 12288) still refused the write -- true because
 * a region-scoped blank check still caught a non-blank byte actually
 * inside the region. FWBLANK-01 removes that check entirely, so the
 * assertion below is the OPPOSITE of what this case asserted before:
 * criterion 3's native-level counterpart, and a cheaper proof than the
 * bench alone. Renamed to the assertion it now makes.
 *
 * Same deviation as the case above: no explicit configure_eprom(&h) call,
 * for the same self-recursion reason. */
void test_write_init_no_longer_refuses_when_target_region_is_non_blank(void) {
    firestarter_handle_t h = make_region_handle(0x07, CMD_WRITE, 16384);
    configure_memory(&h);
    TEST_ASSERT_NOT_NULL_MESSAGE(h.firestarter_operation_init,
        "CMD_WRITE must assign firestarter_operation_init -- a null pointer here means the "
        "fixture, not the production code, is wrong");

    h.address = 8192;
    h.region_end = 12288;

    val_shadow_enable();
    val_shadow_seed(0x2400, 0x00);

    unity_capped_iterations(&h, "test_write_init_no_longer_refuses_when_target_region_is_non_blank");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "FWBLANK-01: a non-blank byte INSIDE the write's own target region [8192, 12288) no "
        "longer refuses the write -- the device-side blank-check pre-flight is gone; the host "
        "now owns this refusal.");
    TEST_ASSERT_FALSE_MESSAGE(val_recording_saturated(),
        "recorder saturated -- the addresses composed during this run would be unreliable");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* POSITIVE: write + init path enables VPP regulator, one test per protocol */
    RUN_TEST(test_eprom_0x07_write_enables_vpp_regulator);
    RUN_TEST(test_eprom_0x08_write_enables_vpp_regulator);
    RUN_TEST(test_eprom_0x0B_write_enables_vpp_regulator);

    /* NEGATIVE CONTROL: configure-only (CMD_READ, no init) — no VPP bits set */
    RUN_TEST(test_eprom_0x07_read_configure_only_does_not_enable_vpp);
    RUN_TEST(test_eprom_0x08_read_configure_only_does_not_enable_vpp);
    RUN_TEST(test_eprom_0x0B_read_configure_only_does_not_enable_vpp);

    /* WRITE-CADENCE INVARIANT (debug session w27c512-write-slow-3x) -- the
     * per-pass route-assert law, enforced here because CI runs only the two
     * pinned native envs. See the block comment above these two cases. */
    RUN_TEST(test_writeperf_route_is_asserted_once_per_pass_not_once_per_byte);
    RUN_TEST(test_writeperf_route_assert_count_tracks_passes_not_pulses);

    /* 201-01 Task 1 control: the address-keyed shadow model is not aliased
     * modulo 16 like the legacy 16-slot model above. */
    RUN_TEST(test_shadow_seed_is_address_keyed_not_modulo_aliased);

    /* 201-01 Task 2: BLANK-02 contract freeze, written before the region
     * split in plans 201-03 / 201-04 lands, so these characterize today's
     * behaviour rather than tomorrow's. test_erase_end_blank_check_scans_from_zero
     * is DELETED (FWBLANK-02, Phase 205 Plan 03 Task 1) -- see the comment
     * where its body used to be. test_blank_check_resumes_across_chunks_and_restores_the_cursor
     * is task 2's disposition, not this task's. */
    RUN_TEST(test_blank_check_resumes_across_chunks_and_restores_the_cursor);

    /* FWBLANK-01 (Phase 205 Plan 03 Task 1) -- both cases RE-KEYED / INVERTED
     * from the D-16.1 pair that used to live here. See the comments above
     * each case's definition for the RE-KEY/INVERT rationale. */
    RUN_TEST(test_write_init_performs_no_blank_check_at_all);
    RUN_TEST(test_write_init_no_longer_refuses_when_target_region_is_non_blank);

    return UNITY_END();
}
