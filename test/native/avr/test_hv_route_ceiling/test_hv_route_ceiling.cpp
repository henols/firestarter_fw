/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include "firestarter.h"
#include "eprom.h"

using namespace fakeit;

extern "C" unsigned hv_route_ceiling_voltage_read_count();
extern "C" void hv_route_ceiling_reset_voltage_read_count();

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
}

void tearDown(void) {}

static firestarter_handle_t make_handle(uint32_t protocol, uint16_t vpp_mv, uint16_t ctrl_flags) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.vpp_mv = vpp_mv;
    h.ctrl_flags = ctrl_flags;
    return h;
}

static rurp_register_t route_for(uint32_t protocol, uint16_t vpp_mv, uint16_t ctrl_flags) {
    hv_route_ceiling_reset_voltage_read_count();
    firestarter_handle_t h = make_handle(protocol, vpp_mv, ctrl_flags);
    return eprom_hv_route_mask(&h);
}

void test_0x07_above_ceiling_routes_undropped_rail_reading_no_voltage(void) {
    rurp_register_t mask = route_for(0x07, 18000, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "a required voltage above the drop path's ceiling must route the undropped rail");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(),
        "the routing decision must read no voltage (D-22): it is a function of vpp_mv, the protocol row and FLAG_VPE_AS_VPP alone");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Boundary triple, derived entirely from the macro so a future
 * re-measurement moves these cases with the constant instead of
 * reddening them falsely. Strictly greater-than, never greater-or-equal.
 * ───────────────────────────────────────────────────────────────────────── */

void test_0x07_at_ceiling_exactly_stays_on_drop_path(void) {
    rurp_register_t mask = route_for(0x07, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(EPROM_HV_ROUTE_MASK, mask,
        "a required voltage exactly at the ceiling must NOT route the undropped rail (strictly greater-than, D-22 boundary)");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "boundary-at case must read no voltage");
}

void test_0x07_one_millivolt_above_ceiling_routes_undropped_rail(void) {
    rurp_register_t mask = route_for(0x07, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV + 1, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "one millivolt above the ceiling must route the undropped rail");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "boundary-above case must read no voltage");
}

void test_0x07_one_millivolt_below_ceiling_stays_on_drop_path(void) {
    rurp_register_t mask = route_for(0x07, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV - 1, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(EPROM_HV_ROUTE_MASK, mask,
        "one millivolt below the ceiling must NOT route the undropped rail");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "boundary-below case must read no voltage");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Protocol 0x08 is the other drop-resistor row (eprom_params.cpp) and must
 * obey the same ceiling on both sides of the boundary.
 * ───────────────────────────────────────────────────────────────────────── */

void test_0x08_at_ceiling_exactly_stays_on_drop_path(void) {
    rurp_register_t mask = route_for(0x08, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(EPROM_HV_ROUTE_MASK, mask,
        "0x08 at the ceiling exactly must NOT route the undropped rail, same as 0x07");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "0x08 boundary-at case must read no voltage");
}

void test_0x08_one_millivolt_above_ceiling_routes_undropped_rail(void) {
    rurp_register_t mask = route_for(0x08, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV + 1, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "0x08 one millivolt above the ceiling must route the undropped rail, same as 0x07");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "0x08 boundary-above case must read no voltage");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Protocol 0x0B is already VPP_PATH_DIRECT_VPE and is unaffected by the
 * ceiling at any required voltage, exactly like the twenty census rows on
 * that path.
 * ───────────────────────────────────────────────────────────────────────── */

void test_0x0B_at_zero_required_voltage_returns_undropped_rail(void) {
    rurp_register_t mask = route_for(0x0B, 0, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "0x0B is direct-VPE unconditionally; a zero required voltage changes nothing");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "0x0B zero-voltage case must read no voltage");
}

void test_0x0B_at_ceiling_returns_undropped_rail(void) {
    rurp_register_t mask = route_for(0x0B, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "0x0B is direct-VPE unconditionally; sitting at the drop path's ceiling changes nothing");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "0x0B at-ceiling case must read no voltage");
}

void test_0x0B_above_ceiling_returns_undropped_rail(void) {
    rurp_register_t mask = route_for(0x0B, RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV + 1, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "0x0B is direct-VPE unconditionally; a required voltage above the ceiling changes nothing");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "0x0B above-ceiling case must read no voltage");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Fail-safes: absent evidence never raises a rail. Both fail toward the
 * regulated drop path, the same direction the resolver already failed.
 * ───────────────────────────────────────────────────────────────────────── */

void test_unresolved_protocol_at_max_representable_voltage_stays_fail_closed_on_drop_path(void) {
    rurp_register_t mask = route_for(0x99, 65535, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(EPROM_HV_ROUTE_MASK, mask,
        "an unresolved protocol returns before the ceiling comparison is ever reached, so even the largest "
        "representable required voltage cannot raise the undropped rail -- absent evidence never raises a rail");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "unresolved-protocol case must read no voltage");
}

void test_zero_required_voltage_on_0x07_stays_fail_closed_on_drop_path(void) {
    rurp_register_t mask = route_for(0x07, 0, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(EPROM_HV_ROUTE_MASK, mask,
        "a zero required voltage is not greater than the ceiling, so it too fails toward the regulated drop path");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "zero-voltage case must read no voltage");
}

/* ─────────────────────────────────────────────────────────────────────────
 * The manual override still wins first and still reads no table: it can
 * only pass if FLAG_VPE_AS_VPP is checked before eprom_params_for is ever
 * called on an unresolved protocol with a zero required voltage.
 * ───────────────────────────────────────────────────────────────────────── */

void test_manual_override_wins_before_any_table_read_on_unresolved_protocol(void) {
    rurp_register_t mask = route_for(0x99, 0, FLAG_VPE_AS_VPP);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "FLAG_VPE_AS_VPP must still win first even on an unresolved protocol with a zero required voltage");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "override case must read no voltage");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Counter non-vacuity: proves the instrument is live and bound to the same
 * rurp_read_voltage_mv symbol eprom.cpp links against. It does NOT prove
 * that eprom_check_vpp increments it -- that claim is not made here.
 * ───────────────────────────────────────────────────────────────────────── */

void test_voltage_read_counter_is_live_and_moves_on_a_direct_call(void) {
    hv_route_ceiling_reset_voltage_read_count();
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "counter must start at zero after reset");
    rurp_read_voltage_mv();
    TEST_ASSERT_EQUAL_MESSAGE(1, hv_route_ceiling_voltage_read_count(),
        "the counter must move from zero to one on a direct call to the same rurp_read_voltage_mv symbol the resolver links against");
}

/* ─────────────────────────────────────────────────────────────────────────
 * D-22, made falsifiable: reproduces the reversal's own two-row table. The
 * witness is the bench record's ADC_PAIRED_DROP_MV figure, not the shipped
 * ceiling -- a monitor reading on the drop path at the same pot setting the
 * ceiling itself was measured at.
 * ───────────────────────────────────────────────────────────────────────── */

static const uint16_t kBenchPairedDropPathMonitorMv = 18700;

void test_d22_row_one_18000_required_adc_check_would_stay_silent_while_path_rule_routes(void) {
    const uint16_t required_mv = 18000;
    rurp_register_t mask = route_for(0x07, required_mv, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "D-22 row one: the path rule routes VPE for a required 18000 mV");
    bool adc_check_would_fire = kBenchPairedDropPathMonitorMv < (uint32_t)required_mv * 95 / 100;
    TEST_ASSERT_FALSE_MESSAGE(adc_check_would_fire,
        "D-22 row one: eprom_check_vpp's existing adc < required*95/100 comparison, evaluated against the "
        "bench-paired drop-path monitor reading, would stay silent for a required 18000 mV -- an "
        "ADC-triggered implementation would have missed this row");
    TEST_ASSERT_TRUE_MESSAGE(RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV < required_mv,
        "D-22 row one: the ceiling is genuinely below the required voltage, which is why the path rule must route");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "D-22 row one routing call must read no voltage");
}

void test_d22_row_two_21000_required_adc_check_would_fire_but_only_this_row_would_be_caught(void) {
    const uint16_t required_mv = 21000;
    rurp_register_t mask = route_for(0x07, required_mv, 0);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(CTRL_VPP_REGULATOR_ENABLE, mask,
        "D-22 row two: the path rule routes VPE for a required 21000 mV");
    bool adc_check_would_fire = kBenchPairedDropPathMonitorMv < (uint32_t)required_mv * 95 / 100;
    TEST_ASSERT_TRUE_MESSAGE(adc_check_would_fire,
        "D-22 row two: the same ADC comparison WOULD fire for a required 21000 mV, which is why an "
        "ADC-triggered implementation would have caught only this one row of the ten");
    TEST_ASSERT_TRUE_MESSAGE(RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV < required_mv,
        "D-22 row two: the ceiling is genuinely below the required voltage here too");
    TEST_ASSERT_EQUAL_MESSAGE(0, hv_route_ceiling_voltage_read_count(), "D-22 row two routing call must read no voltage");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_0x07_above_ceiling_routes_undropped_rail_reading_no_voltage);

    RUN_TEST(test_0x07_at_ceiling_exactly_stays_on_drop_path);
    RUN_TEST(test_0x07_one_millivolt_above_ceiling_routes_undropped_rail);
    RUN_TEST(test_0x07_one_millivolt_below_ceiling_stays_on_drop_path);

    RUN_TEST(test_0x08_at_ceiling_exactly_stays_on_drop_path);
    RUN_TEST(test_0x08_one_millivolt_above_ceiling_routes_undropped_rail);

    RUN_TEST(test_0x0B_at_zero_required_voltage_returns_undropped_rail);
    RUN_TEST(test_0x0B_at_ceiling_returns_undropped_rail);
    RUN_TEST(test_0x0B_above_ceiling_returns_undropped_rail);

    RUN_TEST(test_unresolved_protocol_at_max_representable_voltage_stays_fail_closed_on_drop_path);
    RUN_TEST(test_zero_required_voltage_on_0x07_stays_fail_closed_on_drop_path);

    RUN_TEST(test_manual_override_wins_before_any_table_read_on_unresolved_protocol);

    RUN_TEST(test_voltage_read_counter_is_live_and_moves_on_a_direct_call);

    RUN_TEST(test_d22_row_one_18000_required_adc_check_would_stay_silent_while_path_rule_routes);
    RUN_TEST(test_d22_row_two_21000_required_adc_check_would_fire_but_only_this_row_would_be_caught);

    return UNITY_END();
}
