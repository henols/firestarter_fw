/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extensions:
 *   - HOST_STUBS_RECORD_BUS: activate the recording buffer so the test can
 *     observe all rurp_write_to_register calls and assert VPP-enable CTL bits.
 *   - HOST_STUBS_CUSTOM_HW_REVISION: override hardware revision to return 1
 *     (non-REV0) so eprom_check_vpp does NOT take the REVISION_0 early-return
 *     path — the VPP write that the positive test asserts WILL fire.
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

/* Opt out of default hw-revision stub so we can return non-REVISION_0.
 * Without this, rurp_get_hardware_revision() returns 0 = REVISION_0 and
 * eprom_check_vpp takes the early-return path, never writing VPP bits. */
#define HOST_STUBS_CUSTOM_HW_REVISION

/* Debug session w27c512-write-slow-3x: opt OUT of the shared .inc's default
 * rurp_read_data_buffer (always 0) so this file can supply the stateful
 * read-back model at the bottom, which is what lets the write-cadence cases
 * drive the real eprom_write_execute loop to convergence. MUST precede the
 * include -- this guard, like the two above, is read at include time. */
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

/* Suite-local hw-revision mock: always return 1 (non-REV0) so the VPP write
 * path is reachable in eprom_check_vpp. */
#ifdef HARDWARE_REVISION
static uint8_t s_mock_hw_rev = 1;
extern "C" void set_mock_hw_rev_eprom(uint8_t r) { s_mock_hw_rev = r; }
extern "C" uint8_t rurp_get_hardware_revision() { return s_mock_hw_rev; }
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Debug session w27c512-write-slow-3x -- stateful read-back model, so this
 * suite can drive the REAL eprom_write_execute loop (not just its init) and
 * count program-route asserts.
 *
 * WHY IT LIVES IN THIS SUITE AND NOT IN test_loop_eprom_v131 / test_trace_
 * eprom_v131, which already have richer harnesses: CI (.github/workflows/
 * build.yml:142,155 and beta-build.yml:122,128) runs ONLY `pio test -e
 * native` and `-e native_nodevtools`, which are now the only native envs.
 * Neither runs check_size_baseline.py. An assertion placed outside those
 * envs is decorative -- a branch carrying the per-byte regression would
 * merge green. test_val_eprom is in BOTH pinned envs' test_filter, so the
 * cadence invariant is enforced here or nowhere.
 *
 * HOST_STUBS_CUSTOM_READ_DATA_BUFFER opt-out (declared above the shared
 * include, as that guard is read at include time): the shared default
 * returns 0 for every read, so any non-zero target byte never verifies and
 * the loop burns max_pulses on every byte -- which both saturates the
 * 256-entry recorder and destroys the pass-count arithmetic the cases below
 * assert on.
 *
 * BYTE-INDEX DERIVATION. Same reasoning as test_trace_eprom_v131's model,
 * different mechanism: that suite defines HOST_STUBS_REAL_REGISTER_UTILS and
 * so has a real register cache to read the latched LSB back out of. This
 * suite deliberately does NOT (it needs the plain recording
 * rurp_write_to_register, which records EVERY call with no cache-compare
 * elision -- exactly what makes a route-assert count trustworthy here), and
 * the shared stub's rurp_read_from_register always returns 0. So the index
 * is recovered by scanning the recording backwards for the most recent
 * LEAST_SIGNIFICANT_BYTE write. That is valid for a block based at address 0
 * because LOOP_BUS_CONFIG_0x07 leaves address bits 0-7 identity-mapped:
 * mem_util_remap_address_bus's per-line remap loop starts at
 * config.matching_lines (16), and that config's static_high_mask and
 * vpp_line are 0 and 0xFF, so nothing can perturb a bit below bit 8.
 *
 * The statics below are visible because host_stubs_common.inc is TEXTUALLY
 * included into this TU, so its file-static recorder is in scope here.
 * ───────────────────────────────────────────────────────────────────────── */
#define VAL_EPROM_READBACK_SLOTS 16

struct val_eprom_readback_t {
    uint8_t target;
    uint8_t converge_after;
    uint8_t read_count;
};
static val_eprom_readback_t s_val_readback[VAL_EPROM_READBACK_SLOTS];

extern "C" void val_readback_reset() {
    for (int i = 0; i < VAL_EPROM_READBACK_SLOTS; i++) {
        s_val_readback[i].target = 0xFF;
        s_val_readback[i].converge_after = 0;
        s_val_readback[i].read_count = 0;
    }
}

extern "C" void val_readback_seed(uint8_t idx, uint8_t target, uint8_t converge_after) {
    s_val_readback[idx].target = target;
    s_val_readback[idx].converge_after = converge_after;
    s_val_readback[idx].read_count = 0;
}

/* Saturation reporter. The shared recorder drops silently past
 * HOST_STUBS_MAX_RECORDING (256) with no overflow flag of its own, and a
 * dropped tail would both corrupt the backward index scan below and make an
 * assert-count assertion quietly wrong. Every case that counts must check
 * this. */
extern "C" int val_recording_saturated() {
    return s_bus_recording_count >= HOST_STUBS_MAX_RECORDING;
}

extern "C" uint8_t rurp_read_data_buffer() {
    uint8_t idx = 0;
    for (int i = s_bus_recording_count - 1; i >= 0; i--) {
        if (s_bus_recording[i].reg == LEAST_SIGNIFICANT_BYTE) {
            idx = (uint8_t)(s_bus_recording[i].data & (VAL_EPROM_READBACK_SLOTS - 1));
            break;
        }
    }
    val_eprom_readback_t* st = &s_val_readback[idx];
    uint8_t result = (st->read_count < st->converge_after) ? 0xFF : st->target;
    st->read_count++;
    return result;
}
