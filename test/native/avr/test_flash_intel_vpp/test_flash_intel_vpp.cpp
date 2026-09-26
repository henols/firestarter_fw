/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Five test cases exercise the flash_intel_check_vpp static helper (added in
 * Task 2) via the canonical dispatch path: configure_memory() → operation_init.
 * Mock VPP voltage and hardware revision are injected through suite-local
 * set_mock_vpp_mv() / set_mock_hw_rev() setters in host_stubs.cpp.
 *
 * Wave 0 state (Task 1): test bodies are empty stubs — they compile, link, and
 * trivially PASS. Task 2 fills the bodies with assertions and wires the
 * production code check into flash_intel_write_init.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "flash_intel.h"
#include "memory.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"

using namespace fakeit;

/* Declared in this suite's host_stubs.cpp; called here to inject mock state. */
extern "C" void set_mock_vpp_mv(uint16_t mv);
extern "C" void set_mock_hw_rev(uint8_t rev);

static rurp_register_t s_last_ctrl_reg = 0;
static bool s_last_ctrl_state = false;
static unsigned s_ctrl_writes_with_p1_low = 0;
static unsigned s_ctrl_writes_asserting_hv = 0;
static void mock_set_ctrl_reg(struct firestarter_handle*, rurp_register_t reg, bool state) {
    s_last_ctrl_reg = reg;
    s_last_ctrl_state = state;
    if ((reg & CTRL_VPP_P1_ENABLE) && state == false) {
        s_ctrl_writes_with_p1_low++;
    }
    if (state && (reg & (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE))) {
        s_ctrl_writes_asserting_hv++;
    }
}
static bool mock_get_ctrl_reg(struct firestarter_handle*, rurp_register_t) { return 0; }
static void mock_set_data(struct firestarter_handle*, uint32_t, uint8_t) {}
static uint8_t mock_get_data(struct firestarter_handle*, uint32_t) { return 0xFF; }

void setUp(void) {
    ArduinoFakeReset();
    /* delay() is called by flash_intel_write_init (500 ms regulator settle).
     * ArduinoFake requires mock setup before a virtual is called or it aborts. */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    /* Every VPP verdict other than in-band emits a log frame through Serial.
     * Without these three, the first warning or error test aborts the whole
     * suite on fakeit::UnexpectedMethodCallException. */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    set_mock_vpp_mv(0);
    set_mock_hw_rev(1);  /* non-REV0 default */
    s_last_ctrl_reg = 0;
    s_last_ctrl_state = false;
    s_ctrl_writes_with_p1_low = 0;
    s_ctrl_writes_asserting_hv = 0;
    /* Restore the shipped calibration: the refusal cases below mutate it. */
    rurp_get_config()->r1 = VALUE_R1;
    rurp_get_config()->r2 = VALUE_R2;
    rurp_get_config()->bandgap_mv = 1019;  // calibrated; see host_stubs_common.inc
}

void tearDown(void) {
}

/*
 * Build a handle pre-wired for the Intel-flash write path.
 * - protocol=0x10 routes to configure_flash_intel → flash_intel_write_init
 * - chip_id=0 skips the chip-id branch so only the VPP check is exercised
 * - FLAG_SKIP_ERASE prevents erase from running against the mock and
 *   polluting response_code; write-init performs no blank check at all
 *   any more regardless of ctrl_flags (FWBLANK-01)
 */
static firestarter_handle_t make_intel_handle(uint16_t vpp_setpoint, uint32_t ctrl_flags) {
    firestarter_handle_t h = {};
    h.protocol = 0x10;
    h.cmd = CMD_WRITE;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv = vpp_setpoint;
    h.ctrl_flags = ctrl_flags | FLAG_SKIP_ERASE;
    h.chip_id = 0;  /* skip chip-id branch */
    h.firestarter_set_control_register = mock_set_ctrl_reg;
    h.firestarter_get_control_register = mock_get_ctrl_reg;
    h.firestarter_set_data = mock_set_data;
    h.firestarter_get_data = mock_get_data;
    return h;
}

/*
 * Test: nominal VPP (12000 mV against setpoint 12000 mV) — within band, no error.
 * Tolerance band: low = 12000 * 95 / 100 = 11400; high = 12000 + 500 = 12500.
 * 12000 is between 11400 and 12500 → no response_code change.
 */
void test_flash_intel_vpp_nominal_proceeds(void) {
    set_mock_vpp_mv(12000);
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/*
 * Test: low VPP (11000 mV against setpoint 12000 mV) — below 95% of setpoint.
 * 11000 < 12000 * 95 / 100 = 11400 → firestarter_warning_response_format fires,
 * response_code = RESPONSE_CODE_WARNING. Init proceeds (low-VPP is warn-and-continue).
 */
void test_flash_intel_low_vpp_warns(void) {
    set_mock_vpp_mv(11000);
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, h.response_code);
}

/*
 * Test: high VPP (12700 mV against setpoint 12000 mV) — above setpoint + 500.
 * 12700 > 12000 + 500 = 12500 → RESPONSE_CODE_ERROR, early return.
 */
void test_flash_intel_high_vpp_errors(void) {
    set_mock_vpp_mv(12700);
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/*
 * Test: high VPP + FLAG_FORCE — error is downgraded to warning.
 * Same 12700 mV / 12000 setpoint, but FLAG_FORCE set on ctrl_flags.
 * FORCE downgrade: RESPONSE_CODE_WARNING instead of RESPONSE_CODE_ERROR.
 */
void test_flash_intel_high_vpp_with_force_warns(void) {
    set_mock_vpp_mv(12700);
    firestarter_handle_t h = make_intel_handle(12000, FLAG_FORCE);
    configure_memory(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, h.response_code);
}

/*
 * Test: REV0 hardware — VPP ADC read is skipped entirely, warning emitted.
 * set_mock_hw_rev(0) → rurp_get_hardware_revision() == REVISION_0.
 * set_mock_vpp_mv(65535) is an extreme reading (max uint16_t) that would normally
 * trigger RESPONSE_CODE_ERROR — if it were read. REV0 guard fires first and returns
 * before the ADC compare, so response_code must NOT be RESPONSE_CODE_ERROR.
 */
void test_flash_intel_rev0_skips_vpp_check(void) {
    set_mock_hw_rev(0);
    set_mock_vpp_mv(65535);  /* max uint16_t — would trigger high-band error if ADC were read */
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
}

/*
 * Assertion: at least one write of CTRL_VPP_P1_ENABLE with state=false occurred
 * during the operation_init call, and the last control-register write was
 * the regulator-clear (state=false with the CTRL_VPP_P1_ENABLE bit set).
 */
void test_flash_intel_high_vpp_error_clears_regulator(void) {
    set_mock_vpp_mv(12700);
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_set_control_register = mock_set_ctrl_reg;
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_TRUE_MESSAGE(s_ctrl_writes_with_p1_low > 0,
        "SAF-04: high-VPP ERROR path left CTRL_VPP_P1_ENABLE asserted (socket still at 12V)");
    TEST_ASSERT_BITS_HIGH(CTRL_VPP_P1_ENABLE, s_last_ctrl_reg);
    TEST_ASSERT_FALSE_MESSAGE(s_last_ctrl_state,
        "SAF-04: final control-register write must drive regulator low after VPP error");
}


/*
 * mem_util_refuse_bad_calibration: the planted-violation cases.
 *
 * These prove the refusal can actually fire, and -- the point of putting it
 * pre-flight -- that it fires BEFORE any high-voltage bit is asserted. Without
 * the refusal an unusable calibration makes rurp_read_voltage_mv return 0,
 * which the low-side test reads as "the rail is low": a WARNING, letting the
 * write proceed with 12 V on socket pin 1 and nothing having measured it.
 */
void test_flash_intel_refuses_r2_zero_before_asserting_hv(void) {
    set_mock_vpp_mv(12000);
    rurp_get_config()->r2 = 0;  /* planted: divider bottom leg unusable */
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_set_control_register = mock_set_ctrl_reg;  /* configure_memory overwrites it */
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT(0, s_ctrl_writes_asserting_hv);
}

void test_flash_intel_refuses_divider_sum_over_bound(void) {
    set_mock_vpp_mv(12000);
    rurp_get_config()->r1 = (long)(RURP_DIVIDER_SUM_MAX - VALUE_R2 + 1);
    rurp_get_config()->r2 = VALUE_R2;
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_set_control_register = mock_set_ctrl_reg;  /* configure_memory overwrites it */
    h.firestarter_operation_init(&h);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_UINT(0, s_ctrl_writes_asserting_hv);
}

/*
 * Non-vacuity: the SAME assertions against a calibration one step inside the
 * bound must NOT refuse, and must assert HV. Without this pair, a refusal that
 * fired unconditionally would pass every test above.
 */
void test_flash_intel_accepts_divider_sum_at_bound_and_asserts_hv(void) {
    set_mock_vpp_mv(12000);
    rurp_get_config()->r1 = (long)(RURP_DIVIDER_SUM_MAX - VALUE_R2);
    rurp_get_config()->r2 = VALUE_R2;
    firestarter_handle_t h = make_intel_handle(12000, 0);
    configure_memory(&h);
    h.firestarter_set_control_register = mock_set_ctrl_reg;  /* configure_memory overwrites it */
    h.firestarter_operation_init(&h);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_GREATER_THAN_UINT(0, s_ctrl_writes_asserting_hv);
}


/*
 * v1.43 Task 6 -- the retuned VPP acceptance window.
 *
 * HIGH was a flat +500 mV. That is +4.2 % at a 12000 mV target but only
 * +2.0 % at 25000, which is tighter than this instrument's measured
 * post-calibration worst case of 3.3 % -- so it could refuse a correctly set
 * rail on measurement error alone, on exactly the 30 database rows at
 * 18000 mV and above. It is now a 3 % headroom with a 500 mV floor, which
 * leaves all 716 rows below 18000 mV on the identical threshold.
 *
 * Every boundary below is asserted as a PAIR: the value that must pass and
 * the adjacent value that must not.
 */

static uint8_t run_at(uint16_t target, uint16_t measured, uint32_t flags) {
    set_mock_vpp_mv(measured);
    firestarter_handle_t h = make_intel_handle(target, flags);
    configure_memory(&h);
    h.firestarter_set_control_register = mock_set_ctrl_reg;
    h.firestarter_operation_init(&h);
    return h.response_code;
}

void test_window_floor_applies_below_the_crossover(void) {
    // 12000 target: 3 % is 360, below the 500 floor, so the ceiling is 12500 --
    // byte-identical to the pre-v1.43 threshold. 561 database rows sit here.
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(12000, 12500, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(12000, 12501, 0));
}

void test_window_percentage_applies_above_the_crossover(void) {
    // 25000 target: 3 % is 750, above the floor, so the ceiling is 25750.
    // Under the old flat +500 the ceiling was 25500, inside the instrument's
    // own worst-case error.
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(25000, 25750, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(25000, 25751, 0));
    // The case the retune exists for: a reading the OLD threshold refused and
    // the new one accepts, because 25501 is inside the measurement error.
    TEST_ASSERT_EQUAL_UINT16(25500, 25000 + 500);
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(25000, 25501, 0));
}

void test_the_crossover_is_where_the_arithmetic_puts_it(void) {
    // 3 % of 18000 is 540, so 18000 is the first target that leaves the floor.
    // 18500 was refused before and is accepted now; 18541 is still refused.
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(18000, 18540, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(18000, 18541, 0));
    // One target below the crossover still uses the floor, not 3 %.
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, run_at(13000, 13501, 0));
    TEST_ASSERT_NOT_EQUAL(RESPONSE_CODE_ERROR, run_at(13000, 13500, 0));
}

void test_low_side_is_unchanged_at_five_percent(void) {
    // 5 % is comfortably outside the 3.3 % worst case, so it raises no false
    // warning -- and it stays a WARNING, never an error.
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, run_at(12000, 11400, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(12000, 11399, 0));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(25000, 23749, 0));
}

void test_force_downgrades_high_but_not_the_threshold(void) {
    // FLAG_FORCE changes the SEVERITY of a high reading, never where the
    // threshold sits.
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(12000, 12501, FLAG_FORCE));
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, run_at(12000, 12500, FLAG_FORCE));
}

void test_an_uncalibrated_board_says_so(void) {
    // The verdict is only as good as the instrument. At the nominal reference
    // the reading carries the full per-die bandgap spread, up to 10 %, against
    // a 3-5 % window -- so the operation warns, without refusing.
    rurp_get_config()->bandgap_mv = (uint16_t)RURP_BANDGAP_NOMINAL_MV;
    TEST_ASSERT_EQUAL(RESPONSE_CODE_WARNING, run_at(12000, 12000, 0));
    // Non-vacuity: the SAME in-band reading on a calibrated board is silent.
    // Without this pair the warning could fire unconditionally and pass.
    rurp_get_config()->bandgap_mv = 1019;
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, run_at(12000, 12000, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_flash_intel_vpp_nominal_proceeds);
    RUN_TEST(test_flash_intel_low_vpp_warns);
    RUN_TEST(test_flash_intel_high_vpp_errors);
    RUN_TEST(test_flash_intel_high_vpp_with_force_warns);
    RUN_TEST(test_flash_intel_rev0_skips_vpp_check);
    RUN_TEST(test_flash_intel_high_vpp_error_clears_regulator);
    RUN_TEST(test_flash_intel_refuses_r2_zero_before_asserting_hv);
    RUN_TEST(test_flash_intel_refuses_divider_sum_over_bound);
    RUN_TEST(test_flash_intel_accepts_divider_sum_at_bound_and_asserts_hv);
    RUN_TEST(test_window_floor_applies_below_the_crossover);
    RUN_TEST(test_window_percentage_applies_above_the_crossover);
    RUN_TEST(test_the_crossover_is_where_the_arithmetic_puts_it);
    RUN_TEST(test_low_side_is_unchanged_at_five_percent);
    RUN_TEST(test_force_downgrades_high_but_not_the_threshold);
    RUN_TEST(test_an_uncalibrated_board_says_so);
    return UNITY_END();
}
