#pragma once

#include <stddef.h>
#include <stdint.h>

#include "py32f0xx_hal.h"
#include "rurp_platform.h"

#if !defined(RURP_PLATFORM_PY32F071)
#error "py32f071_rurp_shield.h included for the wrong platform"
#endif

/* CMake selects one board and one safety flag for every translation unit.
 * RURP_PINMAP_PROVISIONAL comes only from CMake (RURP_PROVISIONAL_<board>);
 * a pin-map header must not define it. See rurp_pinmap_guard.h. */
#include "py32f071_pinmap_guard.h"
#ifndef RURP_PINMAP_PROVISIONAL
#error "Set RURP_PINMAP_PROVISIONAL explicitly for this board"
#endif
#ifndef RURP_PY32_PINMAP_HEADER
#error "Select a PY32 board pin-map header in CMake"
#endif
#include RURP_PY32_PINMAP_HEADER

/* Capability flags select board behaviour; board names do not. Every flag is
 * required (0 or 1), so a new pin-map header that misses one fails here
 * instead of silently taking another board's behaviour. */
#if !defined(RURP_PY32F071_HAS_USER_BUTTON) || !defined(RURP_PY32F071_HAS_REVISION_ADC)
#error "A PY32 pin-map header must define RURP_PY32F071_HAS_USER_BUTTON and RURP_PY32F071_HAS_REVISION_ADC (0 or 1)"
#endif
#if defined(HARDWARE_REVISION) && !RURP_PY32F071_HAS_REVISION_ADC
#error "HARDWARE_REVISION detection needs a revision-detect ADC input (RURP_PY32F071_HAS_REVISION_ADC 1)"
#endif
#if RURP_PY32F071_HAS_REVISION_ADC && !defined(HARDWARE_REVISION)
#error "A revision-detect ADC input needs HARDWARE_REVISION (set RURP_HARDWARE_REVISION_<board> 1 in CMake)"
#endif

#if RURP_PY32F071_DATA_SHIFT > 8
#error "The contiguous PY32F071 D0-D7 bus must fit within one 16-pin GPIO port"
#endif

/* The same rule applies to the safe-idle and ROM-touch flags. */
#if !defined(RURP_PY32F071_SAFE_IDLE_AT_RESET) || !defined(RURP_PY32F071_HAS_ROM_TOUCH)
#error "A PY32 pin-map header must define RURP_PY32F071_SAFE_IDLE_AT_RESET and RURP_PY32F071_HAS_ROM_TOUCH (0 or 1)"
#endif
#if RURP_MICROPETAL_ROM_TOUCH && !RURP_PY32F071_HAS_ROM_TOUCH
#error "RURP_MICROPETAL_ROM_TOUCH needs a board with RURP_PY32F071_HAS_ROM_TOUCH 1"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void rurp_board_safe_idle(void);
void rurp_communication_begin(void);
void rurp_timing_init(void);

#ifdef __cplusplus
}
#endif
