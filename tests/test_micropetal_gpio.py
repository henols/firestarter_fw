"""Exercise the actual ARM board backend without a connected shield."""
import pytest
from ._py32_build import arm_defines
from ._host_build import FAKE_ARDUINO_H, INCLUDE, REPO_ROOT, host_cxx, compile_ok, run_binary


@pytest.mark.parametrize("full", [False, True])
def test_provisional_arm_safe_idle_and_operation_lockout(tmp_path, full):
    defines = arm_defines("platform/py32f071/src/py32f071_rurp_shield.cpp")
    if full:
        if '-DRURP_BOARD_MICROPETAL_UNO=1' not in defines:
            pytest.skip('full operation mode is MicroPetal-only')
        defines = [d for d in defines if not d.startswith('-DRURP_PINMAP_PROVISIONAL=')]
        defines += ['-DRURP_PINMAP_PROVISIONAL=0']
    # Only the HAL and serial transport are fake; the mapping, setup and bus
    # functions under test are the production translation unit.
    hal = r'''
#pragma once
#include <stdint.h>
#include <cassert>
#define __NOP() ((void)0)
extern unsigned bsrr_count;
extern uint32_t bsrr_trace[2048];
struct TraceRegister {
    uint32_t value;
    operator uint32_t() const { return value; }
    TraceRegister& operator=(uint32_t v) {
        assert(bsrr_count < 2048);
        bsrr_trace[bsrr_count++] = v;
        value = v;
        return *this;
    }
};
struct GPIO_TypeDef { uint32_t MODER, OTYPER, PUPDR; TraceRegister BSRR; uint32_t IDR, ODR; };
extern GPIO_TypeDef a, b;
#define GPIOA (&a)
#define GPIOB (&b)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC_CLK_ENABLE() ((void)0)
struct GPIO_InitTypeDef { uint32_t Pin, Mode, Pull, Speed; };
enum GPIO_PinState { GPIO_PIN_RESET, GPIO_PIN_SET };
#define GPIO_MODE_OUTPUT_PP 1
#define GPIO_SPEED_FREQ_HIGH 3
extern unsigned gpio_writes, gpio_outputs;
inline void HAL_GPIO_WritePin(GPIO_TypeDef* p, uint16_t pin, GPIO_PinState state) {
#ifdef RURP_BOARD_MICROPETAL_UNO
    const unsigned pins[] = {1U<<13, 1U<<12, 1U<<8, 1U<<9, 1U<<15, 255, 1U<<15, 1U<<15};
    const GPIO_PinState states[] = {GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET,
        GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_SET, GPIO_PIN_RESET};
    assert(gpio_writes < 8 && p == GPIOB && pin == pins[gpio_writes] && state == states[gpio_writes]);
    if (gpio_writes == 6) {
        // At the control-latch edge all data drivers hold zero, CE/OE high,
        // both address latch enables low. No HV-on byte can be captured.
        assert((p->ODR & 0xB3FF) == ((1U<<13) | (1U<<12)));
        assert((p->MODER & 0xFFFF) == 0x5555);
        assert(gpio_outputs == 6);
    }
    ++gpio_writes;
    if (state == GPIO_PIN_SET) p->ODR |= pin; else p->ODR &= ~pin;
#else
    __builtin_trap();
#endif
}
#define GPIO_MODE_ANALOG 3
#define GPIO_MODE_INPUT 0
#define GPIO_NOPULL 0
#define GPIO_PULLUP 1
inline void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *c) {
#ifdef RURP_BOARD_MICROPETAL_UNO
    if (p == GPIOB && c->Mode == GPIO_MODE_OUTPUT_PP) {
        const unsigned pins[] = {1U<<13, 1U<<12, 1U<<8, 1U<<9, 1U<<15, 255};
        assert(gpio_outputs < 6 && c->Pin == pins[gpio_outputs]);
        assert(gpio_writes == gpio_outputs + 1); // preload before direction
        assert(c->Pull == GPIO_NOPULL);
        for (unsigned bit = 0; bit < 16; ++bit)
            if (c->Pin & (1U << bit))
                p->MODER = (p->MODER & ~(3U << (bit*2))) | (1U << (bit*2));
        ++gpio_outputs;
        return;
    }
#endif
    // Only the selected diagnostic ADC and optional button may be configured.
    extern unsigned adc_pins, button_pins;
    if (p == GPIOA && c->Pin ==
#ifdef RURP_BOARD_MICROPETAL_UNO
        (1U << 2)
#else
        (1U << 4)
#endif
         && c->Mode == GPIO_MODE_ANALOG) ++adc_pins;
#ifdef RURP_BOARD_MICROPETAL_UNO
    else if (p == GPIOA && c->Pin == (1U << 3) && c->Mode == GPIO_MODE_ANALOG) ++adc_pins;
#endif
    else if (p == GPIOB && c->Pin == (1U << 14) && c->Mode == GPIO_MODE_INPUT) ++button_pins;
    else __builtin_trap();
}
inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) {
    return (p->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
struct ADC_Init { uint32_t Resolution, DataAlign, ScanConvMode, ContinuousConvMode,
    NbrOfConversion, DiscontinuousConvMode, NbrOfDiscConversion, ExternalTrigConv; };
struct ADC_HandleTypeDef { void *Instance; ADC_Init Init; };
struct ADC_ChannelConfTypeDef { uint32_t Channel, Rank, SamplingTime; };
#define ADC1 nullptr
#define ADC_RESOLUTION_12B 0
#define ADC_DATAALIGN_RIGHT 0
#define ADC_SCAN_DISABLE 0
#define DISABLE 0
#define ADC_SOFTWARE_START 0
#define ADC_REGULAR_RANK_1 0
#define ADC_SAMPLETIME_239CYCLES_5 0
#define ADC_CHANNEL_VREFINT 17
#define ADC_CHANNEL_2 2
#define ADC_CHANNEL_3 3
#define ADC_CHANNEL_4 4
#define HAL_OK 0
inline int HAL_ADC_Init(ADC_HandleTypeDef*) { return HAL_OK; }
inline int HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef*) { return HAL_OK; }
extern unsigned adc_channel, adc_a2, adc_a3;
extern unsigned adc_error, adc_samples;
extern bool adc_mix_zero;
inline int HAL_ADC_ConfigChannel(ADC_HandleTypeDef*, ADC_ChannelConfTypeDef* c) { adc_channel = c->Channel; return adc_error == 1 ? 1 : HAL_OK; }
inline int HAL_ADC_Start(ADC_HandleTypeDef*) { return adc_error == 2 ? 1 : HAL_OK; }
inline int HAL_ADC_Stop(ADC_HandleTypeDef*) { return HAL_OK; }
inline int HAL_ADC_PollForConversion(ADC_HandleTypeDef*, unsigned) { return adc_error == 3 ? 1 : HAL_OK; }
inline unsigned HAL_ADC_GetValue(ADC_HandleTypeDef*) { if (adc_mix_zero) return (++adc_samples % 2) ? 0 : 2048;
    return adc_channel == 3 ? adc_a3 : adc_a2; }
'''
    for pin in range(16):
        hal += f'\n#define GPIO_PIN_{pin} (1U << {pin})\n'
    (tmp_path / 'py32f0xx_hal.h').write_text(hal)
    (tmp_path / 'Arduino.h').write_text(FAKE_ARDUINO_H + 'void delayMicroseconds(unsigned long);\n')
    driver = tmp_path / 'driver.cpp'
    driver.write_text(r'''
#include <cassert>
#include "boards/py32f071_rurp_shield.h"
#include "rurp_shield.h"
unsigned long millis() { return 0; }
void delayMicroseconds(unsigned long) {}
GPIO_TypeDef a = {}, b = {};
unsigned bsrr_count;
uint32_t bsrr_trace[2048];
unsigned gpio_writes, gpio_outputs;
unsigned adc_pins, button_pins;
unsigned adc_channel, adc_a2 = 2048, adc_a3;
unsigned adc_error, adc_samples;
bool adc_mix_zero;
extern "C" void rurp_log_id_u8(uint8_t, uint8_t) {}
void rurp_serial_begin(unsigned long) {}
extern "C" rurp_configuration_t *rurp_get_config() {
    static rurp_configuration_t c = {}; return &c;
}
int main() {
    // Simulate stale/random control latch inputs and a nonzero bus latch.
#ifdef RURP_BOARD_MICROPETAL_UNO
    b.ODR = 0xFFFF;
#endif
    // Exercise the early-main hook before ADC/serial setup, then its safe
    // reapplication in board setup (the runtime follows this same sequence).
    rurp_board_safe_idle();
#ifdef RURP_BOARD_MICROPETAL_UNO
    assert(gpio_writes == 8 && gpio_outputs == 6);
    assert(adc_pins == 0 && button_pins == 0);
    gpio_writes = gpio_outputs = 0;
#endif
    rurp_board_setup();
#ifdef RURP_BOARD_MICROPETAL_UNO
    assert(gpio_writes == 8 && gpio_outputs == 6);
    assert((b.ODR & 0xB3FF) == ((1U<<13) | (1U<<12)));
    assert((b.MODER & 0xFFFF) == 0); // bus released, control drivers stay active
#else
    assert(gpio_writes == 0 && gpio_outputs == 0);
#endif
#if !RURP_PINMAP_PROVISIONAL
    // Full mode must deliver all 256 data bytes unmodified to CTRL. This
    // distinguishes real operation enablement from the bench-zero clamp.
    for (unsigned n = 0; n < 256; ++n) {
        bsrr_count = 0;
        rurp_write_data_buffer(n);
        rurp_set_control_pin(CONTROL_REGISTER, 1);
        assert(bsrr_count == 2);
        assert(bsrr_trace[0] == (n | ((~n & 255U) << 16)));
        assert(bsrr_trace[1] == (1U << 15));
        rurp_set_control_pin(CONTROL_REGISTER, 0);
        assert(bsrr_trace[2] == (1U << 31));
    }
    return 0;
#endif
    const auto idle_mode = b.MODER;
    const auto idle_odr = b.ODR;
    assert(adc_pins == 1 + RURP_PY32F071_HAS_USER_BUTTON && button_pins == RURP_PY32F071_HAS_USER_BUTTON);
#ifdef RURP_BOARD_MICROPETAL_UNO
    rurp_get_config()->hardware_revision = 0xFF;
    for (unsigned n = 0; n < 1024; ++n) {
        adc_a3 = n << 2;
        rurp_detect_hardware_revision();
        const unsigned expected = n < 200 ? REVISION_2_0
            : n < 220 ? REVISION_UNKNOWN : n < 600 ? REVISION_2_3 : REVISION_1;
        assert(rurp_get_physical_hardware_revision() == expected);
    }
    adc_a2 = 4000;
    rurp_detect_hardware_revision();
    assert(rurp_get_physical_hardware_revision() == REVISION_0);
    for (unsigned stage = 1; stage <= 3; ++stage) {
        adc_error = stage;
        rurp_detect_hardware_revision();
        assert(rurp_get_physical_hardware_revision() == REVISION_UNKNOWN);
    }
    adc_error = 0;
    // Exhaust every logical register value on all supported revisions, plus
    // unknown/invalid overrides. A18 becomes the physical P1 switch on Rev2;
    // A16 moves to physical bit 5 and the logical drop bit moves to bit 0.
    const uint8_t revs[] = {REVISION_0, REVISION_1, REVISION_2_0, REVISION_2_1,
        REVISION_2_2, REVISION_2_3, REVISION_UNKNOWN, 0x7F};
    for (uint8_t rev : revs) {
        rurp_get_config()->hardware_revision = rev;
        for (unsigned n = 0; n < 512; ++n) {
            unsigned expected = 0;
            if (rev == REVISION_0 || rev == REVISION_1)
                expected = (n & 255) | ((n & 256) ? 1 : 0);
            else if (rev != REVISION_UNKNOWN && rev != 0x7F)
                expected = (n & 0xDE) | ((n & 256) ? 1 : 0)
                    | ((n & 1) ? 32 : 0) | ((n & 32) ? 8 : 0);
            assert(rurp_map_ctrl_reg_for_hardware_revision(n) == expected);
            rurp_write_to_register(CONTROL_REGISTER, n);
            assert(a.BSRR == 0 && b.BSRR == 0);
        }
    }
#endif
    for (unsigned n = 0; n < 256; ++n) {
        rurp_write_data_buffer(n);
        rurp_set_control_pin(n, 0);
        rurp_set_control_pin(n, 1);
        assert(b.BSRR == 0 && b.MODER == idle_mode && b.ODR == idle_odr && b.OTYPER == 0);
        assert(a.BSRR == 0);
        b.IDR = n;
        assert(rurp_read_data_buffer() == n);
    }
    // A genuine zero is a valid sample, not an ADC error. Every second
    // conversion is zero: averaging must still include it and retain 1024.
    adc_mix_zero = true;
    assert(rurp_read_divider_adc() == 1024);
    adc_mix_zero = false;
    for (unsigned stage = 1; stage <= 3; ++stage) {
        adc_error = stage;
        assert(rurp_read_divider_adc() == 0);
    }
    adc_error = 0;
    rurp_set_data_output();
    assert(b.MODER == idle_mode);
    assert(gpio_writes == 8 * RURP_PY32F071_HAS_USER_BUTTON);
#if RURP_PY32F071_HAS_USER_BUTTON
    b.IDR = 1U << 14;
    assert(!rurp_user_button_pressed());
    b.IDR = 0;
    assert(rurp_user_button_pressed());
#else
    assert(!rurp_user_button_pressed());
#endif
}
''')
    binary = tmp_path / 'test'
    compile_ok([host_cxx(), '-std=c++17', *defines, '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
                '-I', tmp_path, '-I', INCLUDE,
                driver, REPO_ROOT / 'platform/py32f071/src/py32f071_rurp_shield.cpp',
                '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
