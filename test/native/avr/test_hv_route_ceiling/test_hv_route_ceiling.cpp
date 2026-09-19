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

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_0x07_above_ceiling_routes_undropped_rail_reading_no_voltage);

    return UNITY_END();
}
