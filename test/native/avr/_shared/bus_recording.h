/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Register-write recording API for suites whose host_stubs.cpp defines
 * HOST_STUBS_RECORD_BUS. _shared/host_stubs_common.inc defines the symbols.
 */

#pragma once

#include <unity.h>

#include "rurp_shield.h"

extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" rurp_register_t recorded_data(int i);
extern "C" bool bus_recording_saturated();

/* Asserts that no CONTROL_REGISTER write in the recording enables VPP.
 * Note: CTRL_VPP_VPE_DROP_ENABLE is 0x100 when HARDWARE_REVISION is defined.
 * Check only the 8-bit-fit VPP-enable bits: CTRL_VPP_REGULATOR_ENABLE (0x80)
 * and CTRL_VPP_P1_ENABLE (0x08). */
static inline void assert_no_vpp_in_recording(const char* ctx) {
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i), ctx);
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i), ctx);
        }
    }
}
