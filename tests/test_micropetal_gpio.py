"""Exercise the actual ARM board backend without a connected shield."""
import pytest
from ._py32_build import arm_defines, board_name, pinmap_flag, with_define
from ._host_build import FAKE_ARDUINO_H, INCLUDE, REPO_ROOT, host_cc, host_cxx, compile_ok, run_binary


@pytest.mark.parametrize("full", [False, True])
def test_provisional_arm_safe_idle_and_operation_lockout(tmp_path, full):
    defines = arm_defines("platform/py32f071/src/py32f071_rurp_shield.cpp")
    # MicroPetal powers up with random shield latches: its safe idle must
    # clear them. The C driver checks that sequence for this flag.
    if board_name(defines) == 'micropetal_uno':
        assert pinmap_flag(defines, 'RURP_PY32F071_SAFE_IDLE_AT_RESET') == 1
    if full and board_name(defines) != 'micropetal_uno':
        pytest.skip('CMake allows full operation mode only on micropetal_uno')
    # Set the mode explicitly. Then both cases run with the default and the
    # full compile database.
    defines = with_define(defines, 'RURP_PINMAP_PROVISIONAL', 0 if full else 1)
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
#define GPIO_MODE_ANALOG 3
#define GPIO_MODE_INPUT 0
#define GPIO_NOPULL 0
#define GPIO_PULLUP 1
// Defined in driver.cpp: the checks read the pin map, which the board
// header includes after this file.
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *c);
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
extern unsigned adc_channel, adc_a2, adc_a3, adc_vref;
extern unsigned adc_error, adc_samples;
extern bool adc_mix_zero;
inline int HAL_ADC_ConfigChannel(ADC_HandleTypeDef*, ADC_ChannelConfTypeDef* c) { adc_channel = c->Channel; return adc_error == 1 ? 1 : HAL_OK; }
inline int HAL_ADC_Start(ADC_HandleTypeDef*) { return adc_error == 2 ? 1 : HAL_OK; }
inline int HAL_ADC_Stop(ADC_HandleTypeDef*) { return HAL_OK; }
inline int HAL_ADC_PollForConversion(ADC_HandleTypeDef*, unsigned) { return adc_error == 3 ? 1 : HAL_OK; }
inline unsigned HAL_ADC_GetValue(ADC_HandleTypeDef*) { if (adc_mix_zero) return (++adc_samples % 2) ? 0 : 2048;
    return adc_channel == 3 ? adc_a3 : adc_channel == ADC_CHANNEL_VREFINT ? adc_vref : adc_a2; }
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
#include "rurp_voltage_math.h"
unsigned long millis() { return 0; }
void delayMicroseconds(unsigned long) {}
GPIO_TypeDef a = {}, b = {};
unsigned bsrr_count;
uint32_t bsrr_trace[2048];
unsigned gpio_writes, gpio_outputs;
unsigned adc_pins, button_pins;
unsigned adc_channel, adc_a2 = 2048, adc_a3, adc_vref = 1489;
unsigned adc_error, adc_samples;
bool adc_mix_zero;
extern "C" void rurp_log_id_u8(uint8_t, uint8_t) {}
void rurp_serial_begin(unsigned long) {}
extern "C" rurp_configuration_t *rurp_get_config() {
    static rurp_configuration_t c = {}; return &c;
}
constexpr uint32_t data_pins = 0xFFU << RURP_PY32F071_DATA_SHIFT;
struct Write { GPIO_TypeDef *port; uint32_t pin; GPIO_PinState state; };
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
// Safe idle: preload and drive /CE, /OE high, LSB, MSB, CTRL low, the data
// bus low, then pulse CTRL to clear the physical control latch.
const Write safe_idle_writes[] = {
    {RURP_PY32F071_CE_PORT, RURP_PY32F071_CE_PIN, GPIO_PIN_SET},
    {RURP_PY32F071_OE_PORT, RURP_PY32F071_OE_PIN, GPIO_PIN_SET},
    {RURP_PY32F071_LSB_PORT, RURP_PY32F071_LSB_PIN, GPIO_PIN_RESET},
    {RURP_PY32F071_MSB_PORT, RURP_PY32F071_MSB_PIN, GPIO_PIN_RESET},
    {RURP_PY32F071_CONTROL_PORT, RURP_PY32F071_CONTROL_PIN, GPIO_PIN_RESET},
    {RURP_PY32F071_DATA_PORT, data_pins, GPIO_PIN_RESET},
    {RURP_PY32F071_CONTROL_PORT, RURP_PY32F071_CONTROL_PIN, GPIO_PIN_SET},
    {RURP_PY32F071_CONTROL_PORT, RURP_PY32F071_CONTROL_PIN, GPIO_PIN_RESET},
};
constexpr unsigned safe_idle_count = 8, safe_idle_output_count = 6;
#else
constexpr unsigned safe_idle_count = 0, safe_idle_output_count = 0;
#endif
// /CE and /OE high, LSB, MSB, CTRL and the data bus low: no latch can
// capture an HV-on byte.
static bool controls_idle() {
    return (RURP_PY32F071_CE_PORT->ODR & RURP_PY32F071_CE_PIN)
        && (RURP_PY32F071_OE_PORT->ODR & RURP_PY32F071_OE_PIN)
        && !(RURP_PY32F071_LSB_PORT->ODR & RURP_PY32F071_LSB_PIN)
        && !(RURP_PY32F071_MSB_PORT->ODR & RURP_PY32F071_MSB_PIN)
        && !(RURP_PY32F071_CONTROL_PORT->ODR & RURP_PY32F071_CONTROL_PIN)
        && !(RURP_PY32F071_DATA_PORT->ODR & data_pins);
}
static uint32_t data_modes() {
    return (RURP_PY32F071_DATA_PORT->MODER >> (RURP_PY32F071_DATA_SHIFT * 2U)) & 0xFFFFU;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState state) {
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
    assert(gpio_writes < safe_idle_count);
    const Write &w = safe_idle_writes[gpio_writes];
    assert(p == w.port && pin == w.pin && state == w.state);
    if (gpio_writes == 6) {
        // At the control-latch edge all data drivers hold zero.
        assert(controls_idle());
        assert(data_modes() == 0x5555);
        assert(gpio_outputs == safe_idle_output_count);
    }
    ++gpio_writes;
    if (state == GPIO_PIN_SET) p->ODR |= pin; else p->ODR &= ~pin;
#else
    (void)p; (void)pin; (void)state;
    __builtin_trap();
#endif
}
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *c) {
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
    if (c->Mode == GPIO_MODE_OUTPUT_PP) {
        assert(gpio_outputs < safe_idle_output_count);
        const Write &w = safe_idle_writes[gpio_outputs];
        assert(p == w.port && c->Pin == w.pin);
        assert(gpio_writes == gpio_outputs + 1); // preload before direction
        assert(c->Pull == GPIO_NOPULL);
        for (unsigned bit = 0; bit < 16; ++bit)
            if (c->Pin & (1U << bit))
                p->MODER = (p->MODER & ~(3U << (bit*2))) | (1U << (bit*2));
        ++gpio_outputs;
        return;
    }
#endif
    // Only the pin-map ADC inputs and the optional button may be configured.
    if (p == RURP_PY32F071_VPP_ADC_PORT && c->Pin == RURP_PY32F071_VPP_ADC_PIN
        && c->Mode == GPIO_MODE_ANALOG) ++adc_pins;
#if RURP_PY32F071_HAS_REVISION_ADC
    else if (p == RURP_PY32F071_REV_ADC_PORT && c->Pin == RURP_PY32F071_REV_ADC_PIN
             && c->Mode == GPIO_MODE_ANALOG) ++adc_pins;
#endif
#if RURP_PY32F071_HAS_USER_BUTTON
    else if (p == RURP_PY32F071_BUTTON_PORT && c->Pin == RURP_PY32F071_BUTTON_PIN
             && c->Mode == GPIO_MODE_INPUT) ++button_pins;
#endif
    else __builtin_trap();
}
// No pin of the port is in output mode (MODER field 01).
static bool no_output_mode(const GPIO_TypeDef &p) {
    for (unsigned bit = 0; bit < 16; ++bit)
        if (((p.MODER >> (bit * 2)) & 3U) == 1U) return false;
    return true;
}
int main() {
    // Simulate stale/random control latch inputs and a nonzero bus latch.
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
    a.ODR = b.ODR = 0xFFFF;
#else
    // PY32 GPIO reset state is analog mode. The provisional generic board
    // may change pins to input, but never to output.
    a.MODER = b.MODER = 0xFFFFFFFFU;
#endif
    // Exercise the early-main hook before ADC/serial setup, then its safe
    // reapplication in board setup (the runtime follows this same sequence).
    rurp_board_safe_idle();
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
    assert(gpio_writes == safe_idle_count && gpio_outputs == safe_idle_output_count);
    assert(adc_pins == 0 && button_pins == 0);
    gpio_writes = gpio_outputs = 0;
#else
    assert(no_output_mode(a) && no_output_mode(b) && a.BSRR == 0 && b.BSRR == 0);
#endif
    rurp_board_setup();
#if RURP_PY32F071_SAFE_IDLE_AT_RESET
    assert(gpio_writes == safe_idle_count && gpio_outputs == safe_idle_output_count);
    assert(controls_idle());
    assert(data_modes() == 0); // bus released, control drivers stay active
#else
    assert(gpio_writes == 0 && gpio_outputs == 0);
    assert(no_output_mode(a) && no_output_mode(b));
#endif
#if !RURP_PINMAP_PROVISIONAL
    // Full mode must deliver all 256 data bytes unmodified to CTRL. This
    // distinguishes real operation enablement from the bench-zero clamp.
    for (unsigned n = 0; n < 256; ++n) {
        bsrr_count = 0;
        rurp_write_data_buffer(n);
        rurp_set_control_pin(CONTROL_REGISTER, 1);
        assert(bsrr_count == 2);
        assert(bsrr_trace[0] == ((n << RURP_PY32F071_DATA_SHIFT)
            | ((~n & 255U) << (RURP_PY32F071_DATA_SHIFT + 16U))));
        assert(bsrr_trace[1] == RURP_PY32F071_CONTROL_PIN);
        rurp_set_control_pin(CONTROL_REGISTER, 0);
        assert(bsrr_trace[2] == (RURP_PY32F071_CONTROL_PIN << 16U));
    }
    return 0;
#endif
    const auto idle_mode = b.MODER;
    const auto idle_odr = b.ODR;
    assert(adc_pins == 1 + RURP_PY32F071_HAS_REVISION_ADC && button_pins == RURP_PY32F071_HAS_USER_BUTTON);
#ifdef HARDWARE_REVISION
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
    // henols/firestarter#186: the rail readers use the shared conversions with
    // the fixed 1200 mV reference (4096 full scale, not 4095).
    rurp_configuration_t *cfg = rurp_get_config();
    cfg->r1 = VALUE_R1;
    cfg->r2 = VALUE_R2;
    cfg->bandgap_mv = 1100;  // AVR calibration field; PY32 must ignore it
    adc_vref = 1489;
    assert(rurp_read_vcc_mv() == 3301);
    const uint32_t ref = rurp_reference_mv(cfg->bandgap_mv);
    assert(ref == 1200);
    const unsigned vrefs[] = {0, 1, 1000, 1489, 1500, 4095};
    const unsigned dividers[] = {0, 1, 100, 1024, 2048, 4095};
    for (unsigned vref : vrefs) {
        for (unsigned div : dividers) {
            adc_vref = vref;
            adc_a2 = div;
            assert(rurp_read_vcc_mv() == rurp_scale_vcc_mv(vref, ref));
            assert(rurp_read_voltage_mv() ==
                   rurp_scale_voltage_mv(div, vref, VALUE_R1, VALUE_R2, ref));
        }
    }
    adc_vref = 1489;
    adc_a2 = 2048;
    assert(rurp_read_voltage_mv() == rurp_scale_voltage_mv(2048, 1489, VALUE_R1, VALUE_R2, 1200));
    assert(rurp_read_voltage_mv() == 11778);  // 2048 counts, VREFINT 1489, default divider
    cfg->r2 = 0;
    assert(rurp_read_voltage_mv() == 0);
    // A negative r1 or r2 wraps to a large uint32 and must also read 0.
    const long bad[][2] = {{-1, VALUE_R2}, {VALUE_R1, -5}, {INT32_MIN, VALUE_R2}};
    for (const auto &pair : bad) {
        cfg->r1 = pair[0];
        cfg->r2 = pair[1];
        assert(rurp_read_voltage_mv() == 0);
    }
    cfg->r1 = VALUE_R1;
    cfg->r2 = VALUE_R2;
    rurp_set_data_output();
    assert(b.MODER == idle_mode);
#if !RURP_PY32F071_SAFE_IDLE_AT_RESET
    assert(no_output_mode(a) && no_output_mode(b));
#endif
    assert(gpio_writes == safe_idle_count);
#if RURP_PY32F071_HAS_USER_BUTTON
    RURP_PY32F071_BUTTON_PORT->IDR = RURP_PY32F071_BUTTON_PIN;
    assert(!rurp_user_button_pressed());
    RURP_PY32F071_BUTTON_PORT->IDR = 0;
    assert(rurp_user_button_pressed());
#else
    assert(!rurp_user_button_pressed());
#endif
}
''')
    math = tmp_path / 'rurp_voltage_math.o'
    compile_ok([host_cc(), '-std=c11', *defines, '-UNDEBUG', '-I', INCLUDE,
                '-c', REPO_ROOT / 'src/rurp_voltage_math.c', '-o', math])
    binary = tmp_path / 'test'
    compile_ok([host_cxx(), '-std=c++17', *defines, '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
                '-I', tmp_path, '-I', INCLUDE,
                driver, REPO_ROOT / 'platform/py32f071/src/py32f071_rurp_shield.cpp', math,
                '-o', binary])
    result = run_binary(binary)
    assert result.returncode == 0, result.stderr
