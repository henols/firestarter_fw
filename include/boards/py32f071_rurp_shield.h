#pragma once

#include <stddef.h>
#include <stdint.h>

#include "py32f0xx_hal.h"
#include "rurp_platform.h"

#if !defined(RURP_PLATFORM_PY32F071)
#error "py32f071_rurp_shield.h included for the wrong platform"
#endif

/*
 * Provisional example pin map for early firmware builds.
 *
 * IMPORTANT: This is NOT a verified Firestarter PCB assignment. It is a
 * replaceable example chosen to keep the eight-bit data bus contiguous and to
 * use a vendor-documented ADC input. Update every definition below when the
 * final schematic/pinout is available, then validate the signals on hardware
 * before connecting a PROM or enabling programming voltage.
 *
 * Example mapping:
 *
 *   PROM D0-D7       PB0-PB7
 *   LSB latch        PA0
 *   MSB latch        PA1
 *   /OE              PA2
 *   control latch    PA3
 *   VPP measurement  PA4 / ADC channel 4
 *   /CE              PA5
 *   user button      not fitted
 *
 * PA4 / ADC channel 4 follows the official Puya PY32F071 ADC example. The
 * remaining assignments are provisional and describe no existing PCB.
 */

#define RURP_PY32F071_PINMAP_PROVISIONAL 1

/*
 * Bridges the board-specific provisional flag to the platform-neutral
 * RURP_PINMAP_PROVISIONAL that rurp_pinmap_guard.h's refusal predicate tests.
 * This block does two jobs:
 *
 *   1. Its `#if` is a real CONSUMER of RURP_PY32F071_PINMAP_PROVISIONAL, which
 *      is what check_orphan_provisional.py requires -- a flag with zero
 *      consumers enforces nothing. That checker does not scan tests/, so a
 *      pytest cannot serve as the consumer.
 *   2. It DEFINES the neutral flag, in its own #ifndef so a command-line
 *      definition still wins without a macro-redefinition warning.
 *
 * REMOVING it orphans the board flag AND stops defining the neutral one, which
 * silently compiles away configure_memory()'s refusal on this board. Do not
 * remove without replacing both jobs.
 */
#if RURP_PY32F071_PINMAP_PROVISIONAL
#ifndef RURP_PINMAP_PROVISIONAL
#define RURP_PINMAP_PROVISIONAL 1
#endif
#endif

/*
 * The "is this pin map configured for a real build" guard is hoisted into
 * a dependency-free fragment header so a
 * host preprocessor can evaluate it standalone (this file cannot be
 * preprocessed locally -- it includes py32f0xx_hal.h a few lines above).
 * RURP_PY32F071_PINMAP_CONFIGURED is no longer #define'd in this file at
 * all; it is supplied ONLY by platform/py32f071/CMakeLists.txt's
 * target_compile_definitions. This header now only TESTS what the build
 * supplies, evaluated here, before the pin definitions it protects. See
 * the fragment header included directly below, and
 * tests/test_pinmap_guard_fires.py for the fire-proof.
 */
#include "py32f071_pinmap_guard.h"

#define RURP_PY32F071_ENABLE_GPIO_CLOCKS() \
    do                                             \
    {                                              \
        __HAL_RCC_GPIOA_CLK_ENABLE();              \
        __HAL_RCC_GPIOB_CLK_ENABLE();              \
    } while (0)

#define RURP_PY32F071_DATA_PORT GPIOB
#define RURP_PY32F071_DATA_SHIFT 0U

#define RURP_PY32F071_LSB_PORT GPIOA
#define RURP_PY32F071_LSB_PIN GPIO_PIN_0

#define RURP_PY32F071_MSB_PORT GPIOA
#define RURP_PY32F071_MSB_PIN GPIO_PIN_1

#define RURP_PY32F071_OE_PORT GPIOA
#define RURP_PY32F071_OE_PIN GPIO_PIN_2

#define RURP_PY32F071_CONTROL_PORT GPIOA
#define RURP_PY32F071_CONTROL_PIN GPIO_PIN_3

#define RURP_PY32F071_VPP_ADC_PORT GPIOA
#define RURP_PY32F071_VPP_ADC_PIN GPIO_PIN_4
#define RURP_PY32F071_VPP_ADC_CHANNEL ADC_CHANNEL_4

#define RURP_PY32F071_CE_PORT GPIOA
#define RURP_PY32F071_CE_PIN GPIO_PIN_5

#define RURP_PY32F071_HAS_USER_BUTTON 0

#if RURP_PY32F071_DATA_SHIFT > 8
#error "The contiguous PY32F071 D0-D7 bus must fit within one 16-pin GPIO port"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void rurp_communication_begin(void);
void rurp_timing_init(void);

#ifdef __cplusplus
}
#endif
