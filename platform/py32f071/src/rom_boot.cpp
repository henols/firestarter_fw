#include "boards/py32f071_rurp_shield.h"
#include "rurp_rom_touch.h"

#if RURP_MICROPETAL_ROM_TOUCH
namespace {
constexpr uint32_t rom_base = 0x1FFF0000UL;

/* No C/C++ stack accesses after replacing MSP. Interrupts are re-enabled only
 * after all peripheral sources and pending NVIC/SysTick state have been cleared.
 */
__attribute__((naked, noreturn)) void enter_rom(uint32_t, uint32_t) {
    __asm volatile("msr msp, r0\n"
                   "cpsie i\n"
                   "bx r1\n");
}

void jump_to_rom() {
    // The experiment must prove software entry, not a strapped hardware boot.
    __HAL_RCC_GPIOF_CLK_ENABLE();
    if (HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_8) != GPIO_PIN_RESET) return;
    const uint32_t stack = *reinterpret_cast<volatile uint32_t *>(rom_base);
    const uint32_t reset = *reinterpret_cast<volatile uint32_t *>(rom_base + 4U);
    // Refuse a corrupt/unexpected ROM vector before touching USB or clocks.
    if (stack <= 0x20000000UL || stack > 0x20004000UL || (stack & 7U) ||
        !(reset & 1U) || reset < rom_base || reset >= rom_base + 0x3000UL)
        return;

    rurp_board_safe_idle();
    NVIC_DisableIRQ(USB_IRQn);
    __HAL_RCC_USB_FORCE_RESET();
    __HAL_RCC_USB_RELEASE_RESET();
    __HAL_RCC_USB_CLK_DISABLE();

    // Fixed 1.5k D+ pull-up: stopping USB alone does not signal detach.
    // Sink it briefly as open-drain GPIO (~2.2mA), D- stays high impedance.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
    GPIO_InitTypeDef dp = {};
    dp.Pin = GPIO_PIN_12;
    dp.Mode = GPIO_MODE_OUTPUT_OD;
    dp.Pull = GPIO_NOPULL;
    dp.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &dp);
    HAL_Delay(100U);

    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();
    __HAL_RCC_TIM3_CLK_DISABLE();
    if (HAL_RCC_DeInit() != HAL_OK) {
        // Recover through the known application startup; BOOT0 must be low.
        NVIC_SystemReset();
    }
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    MODIFY_REG(SYSCFG->CFGR1, SYSCFG_CFGR1_MEM_MODE, 1U);
    SCB->VTOR = rom_base;
    __set_CONTROL(0U);
    __DSB();
    __ISB();

    // Release D+ immediately before the ROM initializes its USB controller.
    // Shield control pins remain actively safe until ROM takes ownership.
    dp.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOA, &dp);
    enter_rom(stack, reset);
}
}
#endif

extern "C" void rurp_rom_boot_poll(void) {
#if RURP_MICROPETAL_ROM_TOUCH
    if (py32_usb_take_rom_request()) jump_to_rom();
#endif
}
