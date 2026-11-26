#pragma once

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

#define RURP_PY32F071_SAFE_IDLE_AT_RESET 0
#define RURP_PY32F071_HAS_ROM_TOUCH 0
#define RURP_PY32F071_HAS_USER_BUTTON 0
#define RURP_PY32F071_HAS_REVISION_ADC 0

