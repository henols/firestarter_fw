#include "boards/py32f071_rurp_shield.h"

extern void setup(void);
extern void loop(void);

namespace
{
[[noreturn]] void error_handler()
{
    for (;;)
    {
    }
}

void configure_system_clock()
{
    RCC_OscInitTypeDef oscillator = {};
    RCC_ClkInitTypeDef clocks = {};

    oscillator.OscillatorType =
        RCC_OSCILLATORTYPE_HSE |
        RCC_OSCILLATORTYPE_HSI |
        RCC_OSCILLATORTYPE_LSI |
        RCC_OSCILLATORTYPE_LSE;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSIDiv = RCC_HSI_DIV1;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_24MHz;
    oscillator.HSEState = RCC_HSE_OFF;
    oscillator.LSIState = RCC_LSI_OFF;
    oscillator.LSEState = RCC_LSE_OFF;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL.PLLMUL = RCC_PLL_MUL2;

    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        error_handler();
    }

    clocks.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;

    /* FLASH_LATENCY_1 (one wait state, 24 MHz < SYSCLK <= 48 MHz) and FLASH_LATENCY_2 (two wait
     * states, 48 MHz < SYSCLK <= 72 MHz) are the SDK's named flash-latency constants. The ACR
     * bit-mask this call used to pass numerically equals FLASH_LATENCY_2. SDK
     * 0ed2f4b4d3391eccfd4491006a30295fd78e32c2,
     * Drivers/PY32F071_HAL_Driver/Inc/py32f071_hal_flash.h:133-135.
     *
     * Commit 91c6e45 ("Use PY32 flash latency constant", 2026-07-21) swapped this call's
     * argument from FLASH_LATENCY_1 to that ACR mask as a deliberate workaround: at that commit,
     * py32f071_hal_conf.h did not define HAL_FLASH_MODULE_ENABLED and did not include
     * py32f071_hal_flash.h, so FLASH_LATENCY_1 was not yet in scope and the CMSIS device-header
     * mask was the only thing that compiled. Commit d76910c ("Complete PY32 HAL module
     * configuration"), three minutes later, added both the #define and the #include -- but the
     * workaround was never reverted. FLASH_LATENCY_1 is in scope on this tree. This is not a
     * typo: it is a superseded workaround left in place.
     *
     * Severity: the previous argument selected two wait states at 48 MHz instead of the required
     * one. More wait states than required is functionally safe -- an over-conservative setting,
     * not a correctness fault.
     *
     * The preferred proof shape (a static_assert tying the chosen latency to the configured
     * clock) is not achievable: RCC_HSICALIBRATION_24MHz expands to a runtime dereference of a
     * factory-trim address (((0x4<<13) | ((*(uint32_t *)(0x1FFF3220)) & 0x1FFF))), so it can
     * never appear in a static_assert; RCC_PLL_MUL2 is a register-field value, not a multiplier.
     * The SDK does not expose the configured system clock as a compile-time constant. The guard
     * below is the honest maximum: it can only be evaluated by the ARM toolchain in CI, since no
     * local ARM compiler exists here. */
    static_assert(FLASH_LATENCY_1 != FLASH_ACR_LATENCY_1, "the ACR mask equals FLASH_LATENCY_2 (two wait states) - do not reintroduce it");

    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_1) != HAL_OK)
    {
        error_handler();
    }
}
}

extern "C" int main(void)
{
    HAL_Init();
    configure_system_clock();
    rurp_timing_init();

    setup();

    for (;;)
    {
        loop();
    }
}
