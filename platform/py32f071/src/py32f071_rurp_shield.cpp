#include <Arduino.h>

#include "boards/py32f071_rurp_shield.h"
#include "rurp_register_utils.h"
#include "rurp_serial_utils.h"
#include "rurp_shield.h"

#if !defined(RURP_PLATFORM_PY32F071)
#error "py32f071_rurp_shield.cpp compiled for the wrong platform"
#endif

namespace
{
struct PhysicalControlPin
{
    GPIO_TypeDef *port;
    uint16_t pin;
};

ADC_HandleTypeDef adc_handle;

constexpr uint32_t data_pin_mask()
{
    return 0xFFUL << RURP_PY32F071_DATA_SHIFT;
}

constexpr uint32_t data_mode_mask()
{
    return 0xFFFFUL << (RURP_PY32F071_DATA_SHIFT * 2U);
}

PhysicalControlPin control_pin_to_gpio(uint8_t control_pin)
{
    switch (control_pin)
    {
        case LEAST_SIGNIFICANT_BYTE:
            return {RURP_PY32F071_LSB_PORT, RURP_PY32F071_LSB_PIN};

        case MOST_SIGNIFICANT_BYTE:
            return {RURP_PY32F071_MSB_PORT, RURP_PY32F071_MSB_PIN};

        case OUTPUT_ENABLE:
            return {RURP_PY32F071_OE_PORT, RURP_PY32F071_OE_PIN};

        case CONTROL_REGISTER:
            return {RURP_PY32F071_CONTROL_PORT, RURP_PY32F071_CONTROL_PIN};

        case CHIP_ENABLE:
            return {RURP_PY32F071_CE_PORT, RURP_PY32F071_CE_PIN};

        default:
            return {nullptr, 0U};
    }
}

void configure_output(
    GPIO_TypeDef *port,
    uint16_t pin,
    GPIO_PinState initial_state)
{
    HAL_GPIO_WritePin(port, pin, initial_state);

    GPIO_InitTypeDef configuration = {};
    configuration.Pin = pin;
    configuration.Mode = GPIO_MODE_OUTPUT_PP;
    configuration.Pull = GPIO_NOPULL;
    configuration.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &configuration);
}

void configure_adc()
{
    __HAL_RCC_ADC_CLK_ENABLE();

    GPIO_InitTypeDef analog_input = {};
    analog_input.Pin = RURP_PY32F071_VPP_ADC_PIN;
    analog_input.Mode = GPIO_MODE_ANALOG;
    analog_input.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(RURP_PY32F071_VPP_ADC_PORT, &analog_input);

    adc_handle.Instance = ADC1;
    adc_handle.Init.Resolution = ADC_RESOLUTION_12B;
    adc_handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc_handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc_handle.Init.ContinuousConvMode = DISABLE;
    adc_handle.Init.NbrOfConversion = 1U;
    adc_handle.Init.DiscontinuousConvMode = DISABLE;
    adc_handle.Init.NbrOfDiscConversion = 1U;
    adc_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;

    if (HAL_ADC_Init(&adc_handle) != HAL_OK)
    {
        for (;;)
        {
        }
    }

    if (HAL_ADCEx_Calibration_Start(&adc_handle) != HAL_OK)
    {
        for (;;)
        {
        }
    }
}

uint16_t read_adc_channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef channel_configuration = {};
    channel_configuration.Channel = channel;
    channel_configuration.Rank = ADC_REGULAR_RANK_1;
    channel_configuration.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&adc_handle, &channel_configuration) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_Start(&adc_handle) != HAL_OK)
    {
        return 0U;
    }

    if (HAL_ADC_PollForConversion(&adc_handle, 10U) != HAL_OK)
    {
        HAL_ADC_Stop(&adc_handle);
        return 0U;
    }

    const uint16_t value =
        static_cast<uint16_t>(HAL_ADC_GetValue(&adc_handle) & 0x0FFFU);
    HAL_ADC_Stop(&adc_handle);
    return value;
}

uint16_t read_adc_average(uint32_t channel, uint8_t sample_count)
{
    if (sample_count == 0U)
    {
        return 0U;
    }

    uint32_t sum = 0U;

    for (uint8_t sample = 0U; sample < sample_count; ++sample)
    {
        const uint16_t value = read_adc_channel(channel);
        if (value == 0U)
        {
            return 0U;
        }
        sum += value;
    }

    return static_cast<uint16_t>(sum / sample_count);
}
}

extern "C" void rurp_board_setup(void)
{
    RURP_PY32F071_ENABLE_GPIO_CLOCKS();

    /* Preload active-low controls HIGH before enabling their output drivers. */
    configure_output(
        RURP_PY32F071_CE_PORT,
        RURP_PY32F071_CE_PIN,
        GPIO_PIN_SET);
    configure_output(
        RURP_PY32F071_OE_PORT,
        RURP_PY32F071_OE_PIN,
        GPIO_PIN_SET);

    /* Register latch strobes are inactive LOW. */
    configure_output(
        RURP_PY32F071_LSB_PORT,
        RURP_PY32F071_LSB_PIN,
        GPIO_PIN_RESET);
    configure_output(
        RURP_PY32F071_MSB_PORT,
        RURP_PY32F071_MSB_PIN,
        GPIO_PIN_RESET);
    configure_output(
        RURP_PY32F071_CONTROL_PORT,
        RURP_PY32F071_CONTROL_PIN,
        GPIO_PIN_RESET);

    rurp_set_data_input();

#if RURP_PY32F071_HAS_USER_BUTTON
    GPIO_InitTypeDef button = {};
    button.Pin = RURP_PY32F071_BUTTON_PIN;
    button.Mode = GPIO_MODE_INPUT;
    button.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RURP_PY32F071_BUTTON_PORT, &button);
#endif

    configure_adc();

    rurp_chip_disable();
    rurp_chip_input();
    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, 0U);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, 0U);
    rurp_write_to_register(CONTROL_REGISTER, 0U);
    rurp_set_data_input();

    rurp_serial_begin(MONITOR_SPEED);
}

extern "C" void rurp_set_control_pin(uint8_t control_pin, uint8_t state)
{
    const PhysicalControlPin gpio = control_pin_to_gpio(control_pin);
    if (gpio.port == nullptr)
    {
        return;
    }

    gpio.port->BSRR = state != 0U
        ? static_cast<uint32_t>(gpio.pin)
        : static_cast<uint32_t>(gpio.pin) << 16U;
}

extern "C" void rurp_set_data_output(void)
{
    GPIO_TypeDef *const port = RURP_PY32F071_DATA_PORT;
    const uint32_t mode_mask = data_mode_mask();
    const uint32_t output_modes =
        0x5555UL << (RURP_PY32F071_DATA_SHIFT * 2U);

    port->MODER = (port->MODER & ~mode_mask) | output_modes;
    port->OTYPER &= ~data_pin_mask();
    port->PUPDR &= ~mode_mask;
}

extern "C" void rurp_set_data_input(void)
{
    GPIO_TypeDef *const port = RURP_PY32F071_DATA_PORT;
    const uint32_t mode_mask = data_mode_mask();

    port->MODER &= ~mode_mask;
    port->PUPDR &= ~mode_mask;
}

extern "C" void rurp_write_data_buffer(uint8_t data)
{
    rurp_set_data_output();

    const uint32_t set_mask =
        static_cast<uint32_t>(data) << RURP_PY32F071_DATA_SHIFT;
    const uint32_t clear_mask =
        (static_cast<uint32_t>(~data) & 0xFFU) <<
        (RURP_PY32F071_DATA_SHIFT + 16U);

    RURP_PY32F071_DATA_PORT->BSRR = set_mask | clear_mask;
}

extern "C" uint8_t rurp_read_data_buffer(void)
{
    const uint32_t gpio_snapshot = RURP_PY32F071_DATA_PORT->IDR;
    return static_cast<uint8_t>(
        (gpio_snapshot >> RURP_PY32F071_DATA_SHIFT) & 0xFFU);
}

extern "C" uint8_t rurp_user_button_pressed(void)
{
#if RURP_PY32F071_HAS_USER_BUTTON
    return HAL_GPIO_ReadPin(
               RURP_PY32F071_BUTTON_PORT,
               RURP_PY32F071_BUTTON_PIN) == GPIO_PIN_RESET;
#else
    return 0U;
#endif
}

extern "C" long rurp_get_bandgap_adc_reading(void)
{
    return static_cast<long>(read_adc_average(ADC_CHANNEL_VREFINT, 16U));
}

extern "C" uint16_t rurp_read_vcc_mv(void)
{
    const uint32_t reference_reading =
        static_cast<uint32_t>(rurp_get_bandgap_adc_reading());
    if (reference_reading == 0U)
    {
        return 0U;
    }

    return static_cast<uint16_t>(
        (4095UL * 1200UL + reference_reading / 2U) /
        reference_reading);
}

extern "C" uint16_t rurp_read_voltage_mv(void)
{
    const uint32_t vcc_mv = rurp_read_vcc_mv();
    const uint32_t adc_reading =
        read_adc_average(RURP_PY32F071_VPP_ADC_CHANNEL, 16U);
    const rurp_configuration_t *const configuration = rurp_get_config();

    if (vcc_mv == 0U || adc_reading == 0U || configuration->r2 == 0U)
    {
        return 0U;
    }

    const uint64_t numerator =
        static_cast<uint64_t>(adc_reading) *
        vcc_mv *
        (configuration->r1 + configuration->r2);
    const uint64_t denominator =
        static_cast<uint64_t>(4095U) * configuration->r2;

    const uint64_t measured_mv =
        (numerator + denominator / 2U) / denominator;

    return measured_mv > 0xFFFFU
        ? 0xFFFFU
        : static_cast<uint16_t>(measured_mv);
}
