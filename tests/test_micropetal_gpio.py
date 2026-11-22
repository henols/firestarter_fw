"""Exercise the actual ARM board backend without a connected shield."""
from ._py32_build import arm_defines
from ._host_build import INCLUDE, REPO_ROOT, host_cxx, compile_ok, run_binary


def test_provisional_arm_never_drives_socket_gpio(tmp_path):
    defines = arm_defines("platform/py32f071/src/py32f071_rurp_shield.cpp")
    # Only the HAL and serial transport are fake; the mapping, setup and bus
    # functions under test are the production translation unit.
    hal = r'''
#pragma once
#include <stdint.h>
struct GPIO_TypeDef { uint32_t MODER, OTYPER, PUPDR, BSRR, IDR; };
extern GPIO_TypeDef a, b;
#define GPIOA (&a)
#define GPIOB (&b)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC_CLK_ENABLE() ((void)0)
struct GPIO_InitTypeDef { uint32_t Pin, Mode, Pull, Speed; };
enum GPIO_PinState { GPIO_PIN_RESET, GPIO_PIN_SET };
#define GPIO_MODE_ANALOG 3
#define GPIO_MODE_INPUT 0
#define GPIO_NOPULL 0
#define GPIO_PULLUP 1
inline void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *c) {
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
extern bool adc_error;
inline int HAL_ADC_ConfigChannel(ADC_HandleTypeDef*, ADC_ChannelConfTypeDef* c) { adc_channel = c->Channel; return HAL_OK; }
inline int HAL_ADC_Start(ADC_HandleTypeDef*) { return HAL_OK; }
inline int HAL_ADC_Stop(ADC_HandleTypeDef*) { return HAL_OK; }
inline int HAL_ADC_PollForConversion(ADC_HandleTypeDef*, unsigned) { return adc_error ? 1 : HAL_OK; }
inline unsigned HAL_ADC_GetValue(ADC_HandleTypeDef*) { return adc_channel == 3 ? adc_a3 : adc_a2; }
'''
    for pin in range(16):
        hal += f'\n#define GPIO_PIN_{pin} (1U << {pin})\n'
    (tmp_path / 'py32f0xx_hal.h').write_text(hal)
    (tmp_path / 'Arduino.h').write_text('#pragma once\n#include <stdint.h>\ninline unsigned long millis() { return 0; }\ninline void delayMicroseconds(unsigned) {}\n')
    driver = tmp_path / 'driver.cpp'
    driver.write_text(r'''
#include <cassert>
#include "boards/py32f071_rurp_shield.h"
#include "rurp_shield.h"
#include "rurp_pinmap_guard.h"
GPIO_TypeDef a = {}, b = {};
unsigned adc_pins, button_pins;
unsigned adc_channel, adc_a2 = 2048, adc_a3;
bool adc_error;
extern "C" void rurp_log_id_u8(uint8_t, uint8_t) {}
void rurp_serial_begin(unsigned long) {}
extern "C" rurp_configuration_t *rurp_get_config() {
    static rurp_configuration_t c = {}; return &c;
}
int main() {
    static_assert(RURP_PY32F071_SOCKET_GPIO_DISABLED == 1);
    static_assert(DATA_BUFFER_SIZE == 4096);
#ifdef RURP_BOARD_MICROPETAL_UNO
    static_assert(RURP_PY32F071_LSB_PIN == (1U << 8));
    static_assert(RURP_PY32F071_MSB_PIN == (1U << 9));
    static_assert(RURP_PY32F071_OE_PIN == (1U << 12));
    static_assert(RURP_PY32F071_CONTROL_PIN == (1U << 15));
    static_assert(RURP_PY32F071_CE_PIN == (1U << 13));
    static_assert(RURP_PY32F071_VPP_ADC_CHANNEL == 2);
#endif
    rurp_board_setup();
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
    adc_error = true;
    rurp_detect_hardware_revision();
    assert(rurp_get_physical_hardware_revision() == REVISION_UNKNOWN);
    adc_error = false;
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
        assert(b.BSRR == 0 && b.MODER == 0 && b.OTYPER == 0);
        assert(a.BSRR == 0);
        assert(rurp_pinmap_refuses(n) == is_memory_cmd(n));
        b.IDR = n;
        assert(rurp_read_data_buffer() == n);
    }
    rurp_set_data_output();
    assert(b.MODER == 0);
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
