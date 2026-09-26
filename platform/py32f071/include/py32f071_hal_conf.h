#ifndef PY32F071_HAL_CONF_H
#define PY32F071_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED

#ifndef HSI_VALUE
#define HSI_VALUE ((uint32_t)8000000U)
#endif

#ifndef HSE_VALUE
#define HSE_VALUE ((uint32_t)24000000U)
#endif

#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT ((uint32_t)200U)
#endif

#ifndef LSI_VALUE
#define LSI_VALUE ((uint32_t)32768U)
#endif

#ifndef LSE_VALUE
#define LSE_VALUE ((uint32_t)32768U)
#endif

#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT ((uint32_t)5000U)
#endif

#define VDD_VALUE ((uint32_t)3300U)
#define PRIORITY_HIGHEST 0U
#define PRIORITY_HIGH 1U
#define PRIORITY_LOW 2U
#define PRIORITY_LOWEST 3U
#define TICK_INT_PRIORITY ((uint32_t)PRIORITY_LOWEST)
#define USE_RTOS 0U
#define PREFETCH_ENABLE 0U

#include "py32f0xx_hal.h"
#include "py32f071_hal_rcc.h"
#include "py32f071_hal_flash.h"
#include "py32f071_hal_gpio.h"
#include "py32f071_hal_pwr.h"
#include "py32f071_hal_cortex.h"
#include "py32f071_hal_dma.h"
#include "py32f071_hal_adc.h"
#include "py32f071_hal_tim.h"

#ifndef USE_FULL_ASSERT
#define assert_param(expression) ((void)0U)
#else
#define assert_param(expression) \
    ((expression) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#endif

#ifdef __cplusplus
}
#endif

#endif
