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
 * target. Any board header can opt in by defining it; shared code tests only
 * the neutral name.
 *
 * The #ifndef default of 0 is load-bearing, not decorative: without it a build
 * that already defines the flag hits a macro-redefinition warning, which
 * check_build_warnings.py counts against a watermark with no headroom. On AVR,
 * where the flag is never defined, the guard compiles to nothing.
 *
 * The predicate DELEGATES to is_memory_cmd() rather than re-listing its eight
 * commands, so the refused set cannot drift from the admitted set.
 *
 * Identity/config commands (CMD_FW_VERSION, CMD_CONFIG, CMD_HW_VERSION) stay
 * allowed -- the board must remain discoverable while its pin map is
 * provisional. CMD_READ_VPP/CMD_READ_VPE stay allowed too: they are read-only
 * ADC monitor taps that do not route voltage to the socket.
 *
 * The `#if` MUST live here, never inside is_memory_cmd()'s body: a checker
 * forbids any preprocessor conditional in that predicate, so its two-env truth
 * table stays meaningful. static inline in a header for the same reason
 * is_memory_cmd() is -- [env:native]'s build_src_filter would not link a
 * definition placed anywhere else.
 *
 * The `#if` is also what makes this header a real CONSUMER of the flag for
 * check_orphan_provisional.py: a flag defined but never tested enforces
 * nothing.
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
