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

#define HOST_STUBS_CUSTOM_VOLTAGE_MV
#define HOST_STUBS_CUSTOM_HW_REVISION

#include "../_shared/host_stubs_common.inc"

static unsigned s_voltage_read_count = 0;
static uint16_t s_voltage_mv = 0;

extern "C" uint16_t rurp_read_voltage_mv() {
    s_voltage_read_count++;
    return s_voltage_mv;
}

extern "C" void hv_route_ceiling_set_voltage_mv(uint16_t mv) {
    s_voltage_mv = mv;
}

/* Rev 1: Rev 0 cannot measure the rail, so eprom_check_vpp would skip the
 * reading that the D-22 cases drive. */
extern "C" uint8_t rurp_get_hardware_revision() {
    return REVISION_1;
}

extern "C" unsigned hv_route_ceiling_voltage_read_count() {
    return s_voltage_read_count;
}

extern "C" void hv_route_ceiling_reset_voltage_read_count() {
    s_voltage_read_count = 0;
}
