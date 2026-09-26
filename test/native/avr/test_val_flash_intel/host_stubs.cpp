/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extensions:
 *   - HOST_STUBS_RECORD_BUS: activate the recording buffer so the test can
 *     observe all rurp_write_to_register calls and assert CTRL_VPP_P1_ENABLE bits.
 *   - HOST_STUBS_CUSTOM_VOLTAGE_MV: provide a 12V mock so flash_intel_check_vpp
 *     (called from flash_intel_write_init) sees a valid VPP reading and does not
 *     error out, allowing the test to proceed to the CTRL_VPP_P1_ENABLE write.
 *   - HOST_STUBS_CUSTOM_HW_REVISION: return non-REVISION_0 so flash_intel_check_vpp
 *     does NOT take the REV0 early-return path.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Activate recording bus stub (opt-IN). */
#define HOST_STUBS_RECORD_BUS

/* Opt out of shared voltage stub — we provide a 12V mock for the write path. */
#define HOST_STUBS_CUSTOM_VOLTAGE_MV

/* Opt out of shared hw-revision stub — return non-REV0 so VPP path is reachable. */
#define HOST_STUBS_CUSTOM_HW_REVISION

#include "../_shared/host_stubs_common.inc"

/* Suite-local mockable VPP voltage: default 12V so flash_intel_check_vpp passes. */
static uint16_t s_mock_vpp_mv = 12000;
extern "C" void set_mock_vpp_mv_intel(uint16_t mv) { s_mock_vpp_mv = mv; }
extern "C" uint16_t rurp_read_voltage_mv() { return s_mock_vpp_mv; }

#ifdef HARDWARE_REVISION
/* Suite-local hw-revision mock: default 1 (non-REV0). */
static uint8_t s_mock_hw_rev = 1;
extern "C" void set_mock_hw_rev_intel(uint8_t r) { s_mock_hw_rev = r; }
extern "C" uint8_t rurp_get_hardware_revision() { return s_mock_hw_rev; }
#endif
