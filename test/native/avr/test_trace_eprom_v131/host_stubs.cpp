/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Activate the ordered strobe recorder (opt-IN). MUST precede the include. */
#define HOST_STUBS_REAL_REGISTER_UTILS
/* Activate the timing recorder (opt-IN, Task 1). MUST precede the include,
 * and requires HOST_STUBS_REAL_REGISTER_UTILS above (its sequence key is
 * s_strobe_count, which only exists in that block). */
#define HOST_STUBS_RECORD_TIMING
/* Opt OUT of the shared .inc's default rurp_read_data_buffer (always 0), so
 * this file can supply a stateful one instead. MUST precede the include. */
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

#include "rurp_register_utils.h"

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl) {
    lsb_address = lsb;
    msb_address = msb;
    control_register = ctrl;
}

/* stateful, index-keyed read-back model. Without this, the default
 * Byte-index derivation: the byte index is the LATCHED LSB register,
 * masked to the 4-byte block size. This is valid for a block based at
 * address 0 ONLY because all three derived bus_configs (AM27C512/AM27C020/
 * AM2716) leave address bits 0-7 IDENTITY-mapped -- verified from source,
 * not assumed: mem_util_remap_address_bus's only two ways to perturb a bit
 * below the LSB byte are (a) the per-line remap loop, which starts at
 * config.matching_lines and is >= 11 for all three chips (so it never
 * touches bits 0-7), and (b) config.static_high_mask / config.vpp_line,
 * whose set bits (AM2716: bit 13; AM27C020's vpp_line 21 is skipped
 * entirely because using_p1_as_vpp() is true for that chip -- see
 * memory_utils.h) are themselves all >= bit 8. So for our four addresses
 * (0-3), the physical (remapped) address's low byte always equals the
 * logical address's low byte exactly, and reading the cached LSB register
 * (via the real rurp_read_from_register -- the same PUBLIC API production
 * code writes through, not the raw global) recovers the byte index
 * unambiguously.
 */
struct trace_readback_state_t {
    uint8_t target;
    uint8_t converge_after;
    uint8_t read_count;
};
static trace_readback_state_t s_trace_readback[4];

extern "C" void trace_readback_reset() {
    for (int i = 0; i < 4; i++) {
        s_trace_readback[i].target = 0xFF;
        s_trace_readback[i].converge_after = 0;
        s_trace_readback[i].read_count = 0;
    }
}

extern "C" void trace_readback_seed(uint8_t idx, uint8_t target, uint8_t converge_after) {
    s_trace_readback[idx].target = target;
    s_trace_readback[idx].converge_after = converge_after;
    s_trace_readback[idx].read_count = 0;
}

extern "C" uint8_t rurp_read_data_buffer() {
    uint8_t idx = (uint8_t)(rurp_read_from_register(LEAST_SIGNIFICANT_BYTE) & 0x03);
    trace_readback_state_t* st = &s_trace_readback[idx];
    uint8_t result = (st->read_count < st->converge_after) ? 0xFF : st->target;
    st->read_count++;
    return result;
}
