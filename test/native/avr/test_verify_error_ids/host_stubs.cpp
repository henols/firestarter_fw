/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extension:
 *   - HOST_STUBS_CUSTOM_READ_DATA_BUFFER: opt OUT of the shared .inc's
 *     default rurp_read_data_buffer (always 0) so this file can supply a
 *     settable, address-INDEPENDENT read-back byte instead.
 *
 * WHY ADDRESS-INDEPENDENT IS ENOUGH HERE, unlike test_val_eprom's own
 * opt-in: every case this suite drives asks "does the site raise the RIGHT
 * error id on mismatch / timeout", never "which address mismatched" or "how
 * many pulses did this specific byte need". A single settable byte, returned
 * for every read regardless of address, is sufficient to put the shared
 * final-pass verify, the 28C page read-back, the DQ7 data-poll wait and the
 * per-pulse budget exits into either their matching or their mismatching
 * arm, on demand, from the test body -- the recording-and-backward-scan
 * model test_val_eprom's suite needs exists to recover a per-address value,
 * which none of these cases requires.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Opt-IN. MUST precede the shared include -- the guard is read at include
 * time (host_stubs_common.inc's own docstring). */
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

static uint8_t s_verify_ids_readback = 0x00;

/* Settable from the test body, per case -- never per address. */
extern "C" void verify_ids_set_readback(uint8_t value) {
    s_verify_ids_readback = value;
}

extern "C" uint8_t rurp_read_data_buffer() {
    return s_verify_ids_readback;
}
