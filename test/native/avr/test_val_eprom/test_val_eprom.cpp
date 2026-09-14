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
 * and `-e native_nodevtools` (.github/workflows/build.yml:142,155,
 * beta-build.yml:122,128). It does not run native_trace_v131 (which owns the
 * frozen cadence golden), native_loop_v131, native_params_v131 or
 * native_pinmap_provisional, and it does not run check_size_baseline.py at
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

    return UNITY_END();
}
