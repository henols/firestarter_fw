#pragma once

#include <stddef.h>
#include <stdint.h>

#include "py32f0xx_hal.h"
#include "rurp_platform.h"

#if !defined(RURP_PLATFORM_PY32F071)
#error "py32f071_rurp_shield.h included for the wrong platform"
#endif

/* CMake selects one board and one safety flag for every translation unit. */
#include "py32f071_pinmap_guard.h"
#ifndef RURP_PINMAP_PROVISIONAL
#error "Set RURP_PINMAP_PROVISIONAL explicitly for this board"
#endif
#ifndef RURP_PY32_PINMAP_HEADER
#error "Select a PY32 board pin-map header in CMake"
#endif
#include RURP_PY32_PINMAP_HEADER

#if RURP_PY32F071_DATA_SHIFT > 8
#error "The contiguous PY32F071 D0-D7 bus must fit within one 16-pin GPIO port"
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
