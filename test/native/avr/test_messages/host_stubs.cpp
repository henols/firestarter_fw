/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * test_messages stubs the host-side AVR-only symbols that
 * boards/rurp_serial_utils.cpp indirectly references. The widened
 * [env:native] src_filter pulls the real TU into the test binary so the
 * production CRC8 table, MAGIC_PREAMBLE, and _firestarter_emit_frame are
 * exercised end-to-end. (messages.c was a 256-byte PROGMEM byte-count
 * table backing MSG_PARAM_COUNT; it had no firmware callers and was
 * deleted post-Phase-7 to reclaim ~256 B of Leonardo flash.)
 *
 * Suite-specific extensions: NONE — test_messages uses the canonical
 * default for every stub. This TU is a pure pass-through to the shared
 * include (with one extra header pull for ArduinoFake's Serial_ class
 * definition, required by the shared file's link-only `Serial_::operator
 * bool()`).
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
