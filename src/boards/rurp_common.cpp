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
#include "rurp_adc_mux.h"

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB) || defined(ARDUINO_AVR_LEONARDO)

/*
 * This file owns the ADC register access only. The count-to-millivolt
 * arithmetic lives in src/rurp_voltage_math.c, which compiles natively and
 * carries the test coverage -- this translation unit is gated on the three
 * ARDUINO_AVR_* macros and can be built for no native environment.
 */

/*
 * Samples averaged per reading, rounded to nearest rather than truncated.
 *
 * Truncating discards up to 7/8 of a count, and a count is worth about 0.5 %
 * on the bandgap channel -- which is the resolution a calibration is written
 * at. Rounding halves that for two tokens. It does NOT recover the sub-count
 * resolution oversampling could give: that needs the scaled sum carried
 * through the arithmetic, and the ~0.5 %-per-count floor stands until then.
 * The divider presents roughly 38 kOhm to the
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
    return (uint16_t)((sum + (RURP_ADC_SAMPLES / 2)) >> RURP_ADC_SAMPLE_SHIFT);
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

    // Select AVCC as the reference and the internal 1.1V bandgap as the
    // channel. The bandgap is 1.1V nominal on both parts; only its channel
    // NUMBER differs, so that is the only thing this branches on. The register
    // values are computed by rurp_adc_mux.h rather than hand-assembled from
    // bit names, which is what lets test_adc_mux assert them.
#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB)
    const uint8_t bandgap_channel = RURP_ADC_CHANNEL_BANDGAP_328P;
#elif defined(ARDUINO_AVR_LEONARDO)
    const uint8_t bandgap_channel = RURP_ADC_CHANNEL_BANDGAP_32U4;
#else
#error "Unsupported board"
#endif
    ADMUX = rurp_admux_value(RURP_ADC_REF_AVCC, bandgap_channel);
#if defined(ARDUINO_AVR_LEONARDO)
    // MUX5 is in ADCSRB, not ADMUX, so the write above cannot reach it and it
    // keeps whatever the previous conversion left. The bandgap is channel 30
    // and needs it CLEAR. Correct by construction rather than by the
    // coincidence that this firmware happens to read only ADC4 and ADC5 --
    // see rurp_adc_mux.h for what goes wrong otherwise.
    if (rurp_mux5_value(bandgap_channel)) {
        ADCSRB |= _BV(MUX5);
    } else {
        ADCSRB &= (uint8_t)~_BV(MUX5);
    }
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
    return (long)((sum + (RURP_ADC_SAMPLES / 2)) >> RURP_ADC_SAMPLE_SHIFT);
}

uint16_t rurp_read_vcc_mv() {
    return rurp_scale_vcc_mv((uint32_t)rurp_get_bandgap_adc_reading(),
                             rurp_get_config()->bandgap_mv);
}

uint16_t rurp_read_divider_adc() {
    // Reference back to AVCC for the divider pin. rurp_get_bandgap_adc_reading
    // leaves ADMUX on the bandgap channel, so this must select its own.
    analogReference(DEFAULT);
    return adc_sample_average(PIN_VPP_VOLTAGE_ADC);
}

uint16_t rurp_read_voltage_mv() {
    rurp_configuration_t* rurp_config = rurp_get_config();
    uint32_t voltage_adc_reading = rurp_read_divider_adc();
    long bandgap_adc_reading = rurp_get_bandgap_adc_reading();

    return rurp_scale_voltage_mv(voltage_adc_reading, (uint32_t)bandgap_adc_reading,
                                 (uint32_t)rurp_config->r1, (uint32_t)rurp_config->r2,
                                 rurp_config->bandgap_mv);
}
#endif
