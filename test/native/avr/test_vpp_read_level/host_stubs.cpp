/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extensions:
 *   - HOST_STUBS_REAL_REGISTER_UTILS: use the real register cache and the
 *     real rurp_map_ctrl_reg_for_hardware_revision(), so the suite sees the
 *     PHYSICAL control byte that a board revision gets.
 *   - HOST_STUBS_CUSTOM_READ_DATA_BUFFER: each data read takes a snapshot of
 *     the latched registers, so a test can see the bus state while the chip
 *     drives its outputs.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#define HOST_STUBS_REAL_REGISTER_UTILS
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

#include "rurp_register_utils.h"

#define READ_SNAPSHOT_MAX 64

struct read_snapshot_t {
    uint8_t msb;
    rurp_register_t ctrl_logical;
    uint8_t ctrl_physical;
};
static read_snapshot_t s_reads[READ_SNAPSHOT_MAX];
static int s_read_count = 0;

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl) {
    lsb_address = lsb;
    msb_address = msb;
    control_register = ctrl;
}

extern "C" void clear_read_snapshots() { s_read_count = 0; }
extern "C" int read_snapshot_count() { return s_read_count; }
extern "C" uint8_t read_snapshot_msb(int i) { return s_reads[i].msb; }
extern "C" rurp_register_t read_snapshot_ctrl_logical(int i) { return s_reads[i].ctrl_logical; }
extern "C" uint8_t read_snapshot_ctrl_physical(int i) { return s_reads[i].ctrl_physical; }

extern "C" uint8_t rurp_read_data_buffer() {
    if (s_read_count < READ_SNAPSHOT_MAX) {
        s_reads[s_read_count].msb = msb_address;
        s_reads[s_read_count].ctrl_logical = control_register;
        s_reads[s_read_count].ctrl_physical = rurp_map_ctrl_reg_for_hardware_revision(control_register);
        s_read_count++;
    }
    return 0xFF;
}
