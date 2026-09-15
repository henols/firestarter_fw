/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * host stub TU for the test_cobs_cmd_frame suite.
 *
 * Provides no-op rurp_* symbol implementations so the test binary links
 * against boards/rurp_serial_utils.cpp (pulled in via build_src_filter) on
 * the host platform = native without a real AVR toolchain.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <Arduino.h>
#include <ArduinoFake.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#include "../_shared/host_stubs_common.inc"
