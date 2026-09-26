/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <Arduino.h>
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "rurp_voltage_math.h"

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB) || defined(ARDUINO_AVR_LEONARDO)

/*
 * This file owns the ADC register access only. The count-to-millivolt
 * arithmetic lives in src/rurp_voltage_math.c, which compiles natively and
 * carries the test coverage -- this translation unit is gated on the three
 * ARDUINO_AVR_* macros and can be built for no native environment.
 */

/*
 * Samples averaged per reading. The divider presents roughly 38 kOhm to the
 * ADC and the rail is the output of a PWM boost converter, so a single
 * conversion samples whatever the ripple happened to be. Eight is the same
 * count rurp_hw_rev_utils.h already uses for revision detect, and the shift
 * below depends on it being a power of two.
 */
#define RURP_ADC_SAMPLES 8
#define RURP_ADC_SAMPLE_SHIFT 3

/*
 * Discard one conversion after a multiplexer change before averaging.
 *
 * The sample-and-hold capacitor still carries charge from the previous
 * channel when the mux switches, so the first conversion is taken while the
 * input is still settling (ATmega328P datasheet section 24.5 / ATmega32U4
 * section 24.5, "Analog Input Circuitry"). Both readings here alternate
 * between the divider pin and the bandgap channel, so every conversion this
 * file takes follows a channel change.
 */
static uint16_t adc_sample_average(uint8_t pin) {
    uint16_t sum = 0;
    (void)analogRead(pin);  // settle the S/H after the mux change
    for (uint8_t i = 0; i < RURP_ADC_SAMPLES; i++) {
        sum += (uint16_t)analogRead(pin);
    }
    return (uint16_t)(sum >> RURP_ADC_SAMPLE_SHIFT);
}

/**
 * @brief Reads the raw ADC value for the internal 1.1V bandgap reference.
 * This provides a basis for calculating the actual VCC, and using the raw
 * value in other calculations preserves precision.
 * @return The raw ADC reading as a long.
 */
long rurp_get_bandgap_adc_reading() {
    uint16_t sum = 0;
    uint8_t i;

    // Set the analog reference to the internal 1.1V and select the bandgap channel.
    // The MUX settings are different for Uno and Leonardo.
#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB)
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined(ARDUINO_AVR_LEONARDO)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#else
#error "Unsupported board"
#endif

    delay(2);  // Wait for the bandgap reference to stabilize.

    // One discarded conversion, then the average. See adc_sample_average --
    // the same settling argument applies, but the channel is selected by the
    // ADMUX write above rather than by analogRead.
    for (i = 0; i <= RURP_ADC_SAMPLES; i++) {
        ADCSRA |= _BV(ADSC);              // Start conversion
        while (bit_is_set(ADCSRA, ADSC))  // Wait for conversion to complete
            ;
        long result = ADCL;
        result |= ADCH << 8;
        if (i > 0) {  // i == 0 is the discarded settling conversion
            sum += (uint16_t)result;
        }
    }
    return (long)(sum >> RURP_ADC_SAMPLE_SHIFT);
}

uint16_t rurp_read_vcc_mv() {
    return rurp_scale_vcc_mv((uint32_t)rurp_get_bandgap_adc_reading());
}

uint16_t rurp_read_voltage_mv() {
    rurp_configuration_t* rurp_config = rurp_get_config();

    // Set analog reference to default (VCC) for the measurement
    analogReference(DEFAULT);
    uint32_t voltage_adc_reading = adc_sample_average(PIN_VPP_VOLTAGE_ADC);

    long bandgap_adc_reading = rurp_get_bandgap_adc_reading();

    return rurp_scale_voltage_mv(voltage_adc_reading, (uint32_t)bandgap_adc_reading,
                                 (uint32_t)rurp_config->r1, (uint32_t)rurp_config->r2);
}
#endif
