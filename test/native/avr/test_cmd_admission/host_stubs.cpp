/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Compiling firmware sources (src/proms/*.cpp) on platform = native leaves
 * the linker hungry for hardware-side symbols defined in the AVR-only TUs
 * (src/boards/*.cpp, src/logging.c). The shared include provides no-op host
 * implementations of every rurp_* symbol the proms reference, plus the
 * PROGMEM log-tag globals from src/logging.c, so the test_cmd_admission test
 * binary can link.
 *
 * Suite-specific extensions: NONE — test_cmd_admission uses the canonical
 * default for every stub, so this TU is a pure pass-through to the shared
 * include.
 *
 * Scope: only compiled into [env:native] / [env:native_nodevtools] via PIO's
 * automatic discovery of files under test/. Production builds (env:uno,
 * env:leonardo) never see this file because their src_filter excludes test/.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#include "../_shared/host_stubs_common.inc"
