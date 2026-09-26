#include "boards/py32f071_rurp_shield.h"

namespace
{
TIM_HandleTypeDef microsecond_timer;
}

extern "C" void rurp_timing_init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    const uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    if (pclk < 1000000U || (pclk % 1000000U) != 0U)
    {
        for (;;)
        {
        }
    }

    microsecond_timer.Instance = TIM3;
    microsecond_timer.Init.Prescaler = (pclk / 1000000U) - 1U;
    microsecond_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    microsecond_timer.Init.Period = 0xFFFFU;
    microsecond_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    microsecond_timer.Init.RepetitionCounter = 0U;
    microsecond_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&microsecond_timer) != HAL_OK)
    {
        for (;;)
        {
        }
    }

    if (HAL_TIM_Base_Start(&microsecond_timer) != HAL_OK)
    {
        for (;;)
        {
        }
    }
}

extern "C" uint32_t rurp_millis(void)
{
    return HAL_GetTick();
}

extern "C" uint32_t rurp_micros(void)
{
    const uint32_t interrupt_state = __get_PRIMASK();
    __disable_irq();

    uint32_t milliseconds = HAL_GetTick();
    const uint32_t pending_before =
        SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    uint32_t counter = SysTick->VAL;
    const uint32_t pending_after =
        SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    const uint32_t reload = SysTick->LOAD + 1U;

    if (pending_after != 0U)
    {
        ++milliseconds;

        /* If SysTick wrapped during this sample, use a post-wrap counter. */
        if (pending_before == 0U)
        {
            counter = SysTick->VAL;
        }
    }

    if (interrupt_state == 0U)
    {
        __enable_irq();
    }

    if (reload == 0U)
    {
        return milliseconds * 1000U;
    }

    const uint32_t elapsed_ticks = reload - counter;
    const uint32_t elapsed_microseconds =
        (elapsed_ticks * 1000U) / reload;

    return (milliseconds * 1000U) + elapsed_microseconds;
}

extern "C" void rurp_delay_us(uint32_t microseconds)
{
    while (microseconds != 0U)
    {
        const uint16_t chunk =
            static_cast<uint16_t>(microseconds > 60000U ? 60000U : microseconds);
        const uint16_t start =
            static_cast<uint16_t>(__HAL_TIM_GET_COUNTER(&microsecond_timer));

        while (static_cast<uint16_t>(
                   static_cast<uint16_t>(__HAL_TIM_GET_COUNTER(&microsecond_timer)) - start) < chunk)
        {
        }

        microseconds -= chunk;
    }
}

extern "C" void rurp_delay_ms(uint32_t milliseconds)
{
    HAL_Delay(milliseconds);
}

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}
