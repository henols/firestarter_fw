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

#include <stdio.h>
#include <unity.h>

#include "rurp_pinout.h"
#include "rurp_shield.h"

extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" rurp_register_t recorded_data(int i);
extern "C" bool bus_recording_saturated();

/* The data-bus cycles: each rurp_write_data_buffer() call is a WRITE, and each
 * call of the default rurp_read_data_buffer() stub is a READ. Each entry holds
 * the LSB/MSB address and the CONTROL value that the register writes latched
 * before the cycle. clear_bus_recording() clears this log too. */
enum { BUS_CYCLE_WRITE = 1, BUS_CYCLE_READ = 2 };
extern "C" int bus_cycle_count();
extern "C" uint8_t bus_cycle_kind(int i);
extern "C" uint16_t bus_cycle_address(int i);
extern "C" rurp_register_t bus_cycle_control(int i);
extern "C" uint8_t bus_cycle_data(int i);

/* Every high-voltage control bit, at the full register width. On the wide
 * layout CTRL_VPP_VPE_DROP_ENABLE is above bit 7, so an 8-bit mask misses it. */
static const rurp_register_t kHvBits =
    (rurp_register_t)(RURP_SOCKET_VPP_ROUTE_BITS | CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE);

/* Asserts that the recording holds at least one CONTROL_REGISTER write, and
 * that no CONTROL_REGISTER write sets an HV bit. A path that writes no control
 * register at all must assert bus_recording_count() instead. */
static inline void assert_no_vpp_in_recording(const char* ctx) {
    int control_writes = 0;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            control_writes++;
            TEST_ASSERT_BITS_LOW_MESSAGE(kHvBits, recorded_data(i), ctx);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(control_writes > 0, ctx);
}

/* One expected data-bus cycle: the kind, the 16-bit address, and (for a
 * WRITE) the data byte. */
struct expected_cycle_t {
    uint8_t kind;
    uint16_t address;
    uint8_t data;
};

/* Asserts that the data-bus cycles from index `first` on are exactly `want`.
 * A READ entry compares the kind and the address only. */
static inline void assert_bus_cycles_from(int first, const expected_cycle_t* want, int n, const char* ctx) {
    char msg[160];
    TEST_ASSERT_TRUE_MESSAGE(bus_cycle_count() >= first + n, ctx);
    for (int k = 0; k < n; k++) {
        int i = first + k;
        snprintf(msg, sizeof(msg), "%s: cycle %d", ctx, k);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[k].kind, bus_cycle_kind(i), msg);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(want[k].address, bus_cycle_address(i), msg);
        if (want[k].kind == BUS_CYCLE_WRITE) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[k].data, bus_cycle_data(i), msg);
        }
    }
}

/* Asserts that the data-bus cycles are exactly `want`, with nothing more. */
static inline void assert_bus_cycles(const expected_cycle_t* want, int n, const char* ctx) {
    char msg[160];
    snprintf(msg, sizeof(msg), "%s: cycle count", ctx);
    TEST_ASSERT_EQUAL_INT_MESSAGE(n, bus_cycle_count(), msg);
    assert_bus_cycles_from(0, want, n, ctx);
}
