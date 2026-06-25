/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Phase 12 Wave 1 — host stub TU for the test_dispatch suite.
 * Phase 6 WR-06 — shared stub body lives in ../_shared/host_stubs_common.inc.
 * Phase 84 Plan 01 — VPP-skip gate tests (D-11).
 *
 * Compiling firmware sources (src/proms/*.cpp) on platform = native leaves
 * the linker hungry for hardware-side symbols defined in the AVR-only TUs
 * (src/boards/*.cpp, src/logging.c). The shared include provides no-op host
 * implementations of every rurp_* symbol the proms reference, plus the
 * PROGMEM log-tag globals from src/logging.c, so the dispatch test binary
 * can link.
 *
 * Suite-specific extensions (Phase 84):
 *   HOST_STUBS_CUSTOM_HW_REVISION — override so eprom_check_vpp() does NOT
 *   early-return on REVISION_0 (the default stub returns 0 = REVISION_0,
 *   which causes an early return before the regulator-enable call, making
 *   the VPP-skip assertions vacuously true).  The VPP-skip tests need the
 *   full eprom_check_vpp() path to execute so they can verify the regulator-
 *   enable is suppressed for CMD_READ/CMD_BLANK_CHECK and NOT suppressed for
 *   CMD_WRITE/CMD_ERASE/CMD_CHECK_CHIP_ID.  We return REVISION_1 (1) here,
 *   which is the same value the rev-detection returns for any non-rev-0
 *   shield.  Existing dispatch tests (which never call firestarter_operation_init)
 *   are unaffected.
 *
 * Scope: only compiled into [env:native] via PIO's automatic discovery of
 * files under test/. Production builds (env:uno, env:leonardo) never see
 * this file because their src_filter excludes test/.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Override hardware revision so eprom_check_vpp() bypasses the REVISION_0
 * early-return and exercises the full regulator-enable path.  The VPP-skip
 * gate tests rely on observing this enable call. */
#define HOST_STUBS_CUSTOM_HW_REVISION

#include "../_shared/host_stubs_common.inc"

/* Return REVISION_1 (1) — any non-zero revision — so eprom_check_vpp skips
 * the REVISION_0 warn-and-return branch and proceeds to measure VPP. */
extern "C" uint8_t rurp_get_hardware_revision() {
    return 1; /* REVISION_1 */
}
