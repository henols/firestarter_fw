/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The provisional-pin-map refusal.
 *
 * Two orthogonal gates, deliberately not merged: is_memory_cmd()
 * (include/firestarter.h) decides which commands may reach configure_memory()
 * at all; this one sits INSIDE configure_memory() and refuses to energise the
 * PROM bus on a board whose pin map has no verified PCB assignment behind it.
 *
 * The flag is platform-neutral because configure_memory() is compiled by every
 * target; shared code tests only the neutral name. It has ONE source: the
 * build. platform/py32f071/CMakeLists.txt passes
 * RURP_PINMAP_PROVISIONAL=${RURP_PROVISIONAL_<board>} to every translation
 * unit. Do not define it in a board or pin-map header: the shared handlers do
 * not include those, so a header value would release socket GPIO in the
 * backend while this refusal still blocks every memory command (or the
 * reverse). tests/test_py32_compile_db.py checks that every firmware
 * translation unit gets the same single value. It checks the build flags
 * only. It cannot see a #define in a header, so no test enforces the
 * header rule.
 *
 * The #ifndef default of 0 is load-bearing, not decorative: without it a build
 * that already defines the flag hits a macro-redefinition warning. On AVR,
 * where the flag is never defined, the guard compiles to nothing.
 *
 * The predicate DELEGATES to is_memory_cmd() rather than re-listing its seven
 * commands, so the refused set cannot drift from the admitted set.
 *
 * Identity/config commands (CMD_FW_VERSION, CMD_CONFIG, CMD_HW_VERSION) stay
 * allowed -- the board must remain discoverable while its pin map is
 * provisional. CMD_READ_VPP/CMD_READ_VPE stay allowed too, but their handlers request
 * regulator control writes. Provisional PY32 backends therefore suppress ALL
 * socket GPIO output writes, independently of this memory-command gate.
 *
 * The `#if` MUST live here, never inside is_memory_cmd()'s body, so that
 * predicate's truth table is the same in every env. static inline in a header for the same reason
 * is_memory_cmd() is -- [env:native]'s build_src_filter would not link a
 * definition placed anywhere else.
 */
#ifndef __RURP_PINMAP_GUARD_H__
#define __RURP_PINMAP_GUARD_H__

#include "firestarter.h"

#ifndef RURP_PINMAP_PROVISIONAL
#define RURP_PINMAP_PROVISIONAL 0
#endif

static inline bool rurp_pinmap_refuses(uint8_t cmd) {
#if RURP_PINMAP_PROVISIONAL
    return is_memory_cmd(cmd);
#else
    return false;
#endif
}

#endif /* __RURP_PINMAP_GUARD_H__ */
