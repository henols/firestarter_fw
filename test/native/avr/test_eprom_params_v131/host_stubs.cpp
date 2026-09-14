/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Compiling firmware sources (the src/proms/ translation units) on
 * platform = native leaves the linker hungry for hardware-side symbols
 * defined in the AVR-only TUs (src/boards/*.cpp, src/logging.c). The shared
 * include provides no-op host implementations of every rurp_* symbol the
 * proms reference, plus the PROGMEM log-tag globals from src/logging.c, so
 * the params test binary can link.
 *
 * Suite-specific extensions: NONE — this suite calls configure_memory() /
 * configure_eprom() only, which perform no hardware I/O beyond
 * mem_util_set_address(handle, 0), so the canonical default for every stub
 * is correct here. This TU is a pure pass-through to the shared include; it
 * activates none of the opt-in stub-behavior guards the shared include
 * documents.
 *
 * Scope: only compiled into [env:native_params_v131] via PIO's automatic
 * discovery of files under test/. Production builds (env:uno, env:leonardo)
 * never see this file because their src_filter excludes test/.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#include "../_shared/host_stubs_common.inc"
