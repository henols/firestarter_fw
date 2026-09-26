/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * This suite exercises src/rurp_voltage_math.c only, which touches no
 * hardware. These stubs exist solely to satisfy the link, because the shared
 * build_src_filter compiles src/proms/ into every native suite.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#include "../_shared/host_stubs_common.inc"
