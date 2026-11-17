/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extension:
 *   - HOST_STUBS_CUSTOM_READ_DATA_BUFFER: the test sets the byte that the
 *     DQ7 data poll in the flash waits reads back.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

static uint8_t (*s_read_data_fn)(void) = NULL;
extern "C" void set_mock_read_data_fn(uint8_t (*fn)(void)) { s_read_data_fn = fn; }
extern "C" uint8_t rurp_read_data_buffer() { return s_read_data_fn != NULL ? s_read_data_fn() : 0; }
