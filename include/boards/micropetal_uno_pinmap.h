#pragma once

#define RURP_PY32F071_ENABLE_GPIO_CLOCKS() \
    do                                             \
    {                                              \
        __HAL_RCC_GPIOA_CLK_ENABLE();              \
        __HAL_RCC_GPIOB_CLK_ENABLE();              \
    } while (0)

#define RURP_PY32F071_DATA_PORT GPIOB
#define RURP_PY32F071_DATA_SHIFT 0U

/* MicroPetal Uno R1, non-E PY32F071C1BU6. See platform/py32f071/DESIGN.md.
 * U4 CAN transceiver is DNP on the F071 assembly: D8/D9 are available.
 * Keep socket operations disabled until the board backend is validated.
 * Startup must still drive latch enables low, /CE and /OE high, and clear
 * the physical control latch: floating controls are not electrically safe.
 */
/* 1: rurp_board_safe_idle() drives the controls and clears the latches. */
#define RURP_PY32F071_SAFE_IDLE_AT_RESET 1
/* 1: the 1200-baud touch can enter the factory ROM USB loader (PA12 D+ has
 * a fixed pull-up, BOOT0 is on PF8). */
#define RURP_PY32F071_HAS_ROM_TOUCH 1
#define RURP_PY32F071_LSB_PORT GPIOB
#define RURP_PY32F071_LSB_PIN GPIO_PIN_8
#define RURP_PY32F071_MSB_PORT GPIOB
#define RURP_PY32F071_MSB_PIN GPIO_PIN_9
#define RURP_PY32F071_OE_PORT GPIOB
#define RURP_PY32F071_OE_PIN GPIO_PIN_12
#define RURP_PY32F071_CONTROL_PORT GPIOB
#define RURP_PY32F071_CONTROL_PIN GPIO_PIN_15
#define RURP_PY32F071_VPP_ADC_PORT GPIOA
#define RURP_PY32F071_VPP_ADC_PIN GPIO_PIN_2
#define RURP_PY32F071_VPP_ADC_CHANNEL ADC_CHANNEL_2
#define RURP_PY32F071_HAS_REVISION_ADC 1
#define RURP_PY32F071_REV_ADC_PORT GPIOA
#define RURP_PY32F071_REV_ADC_PIN GPIO_PIN_3
#define RURP_PY32F071_REV_ADC_CHANNEL ADC_CHANNEL_3
#define RURP_PY32F071_CE_PORT GPIOB
#define RURP_PY32F071_CE_PIN GPIO_PIN_13
#define RURP_PY32F071_HAS_USER_BUTTON 1
#define RURP_PY32F071_BUTTON_PORT GPIOB
#define RURP_PY32F071_BUTTON_PIN GPIO_PIN_14
