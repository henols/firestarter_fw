/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extensions:
 *   - HOST_STUBS_RECORD_BUS: record every register write at the full
 *     register width.
 *   - HOST_STUBS_CUSTOM_VOLTAGE_MV / HOST_STUBS_CUSTOM_HW_REVISION: the suite
 *     injects the measured rail voltage and the board revision.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#define HOST_STUBS_RECORD_BUS
#define HOST_STUBS_CUSTOM_VOLTAGE_MV
#define HOST_STUBS_CUSTOM_HW_REVISION

#include "../_shared/host_stubs_common.inc"

static uint16_t s_mock_vpp_mv = 0;
extern "C" void set_mock_vpp_mv(uint16_t mv) { s_mock_vpp_mv = mv; }
extern "C" uint16_t rurp_read_voltage_mv() { return s_mock_vpp_mv; }

#ifdef HARDWARE_REVISION
static uint8_t s_mock_hw_rev = 1;
extern "C" void set_mock_hw_rev(uint8_t r) { s_mock_hw_rev = r; }
extern "C" uint8_t rurp_get_hardware_revision() { return s_mock_hw_rev; }
#endif
