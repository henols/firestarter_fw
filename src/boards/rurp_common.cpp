/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <Arduino.h>
#include "rurp_shield.h"
#include "rurp_pinout.h"

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB) || defined(ARDUINO_AVR_LEONARDO)


/**
 * @brief Reads the raw ADC value for the internal 1.1V bandgap reference.
 * This provides a basis for calculating the actual VCC, and using the raw
 * value in other calculations preserves precision.
 * @return The raw ADC reading as a long.
 */
long rurp_get_bandgap_adc_reading() {
    // Set the analog reference to the internal 1.1V and select the bandgap channel.
    // The MUX settings are different for Uno and Leonardo.
#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB)
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined(ARDUINO_AVR_LEONARDO)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#else
#error "Unsupported board"
#endif

    delay(2); // Wait for voltage to stabilize
    ADCSRA |= _BV(ADSC);              // Start conversion
    while (bit_is_set(ADCSRA, ADSC))  // Wait for conversion to complete
        ;

    long result = ADCL;
    result |= ADCH << 8;
    return result;
}

uint16_t rurp_read_vcc_mv() {
    long result = rurp_get_bandgap_adc_reading();
    // Calculate Vcc (supply voltage) in millivolts
    // VCC_mV = (V_bandgap * ADC_resolution * 1000) / ADC_reading
    // VCC_mV = (1.1V * 1024 steps * 1000 mV/V) / ADC_reading = 1126400 / ADC_reading
    if (result == 0) return 0;  // Avoid division by zero
    // Add half of the divisor to the numerator to round the result
    return (1126400L + result / 2) / result;
}

uint16_t rurp_read_voltage_mv() {
    rurp_configuration_t* rurp_config = rurp_get_config();
    uint32_t r1 = rurp_config->r1;
    uint32_t r2 = rurp_config->r2;

    // Set analog reference to default (VCC) for the measurement
    analogReference(DEFAULT);
    uint32_t voltage_adc_reading = analogRead(PIN_VPP_VOLTAGE_ADC);

    long bandgap_adc_reading = rurp_get_bandgap_adc_reading();
    if (bandgap_adc_reading == 0 || r2 == 0) return 0; // Avoid division by zero

    // Vin_mV = (adc * 1100 * (R1 + R2)) / (bandgap_adc * R2)
    //
    // Evaluated entirely in 32-bit by folding the divider into one scale factor
    // FIRST, rather than forming a 64-bit numerator:
    //
    //     k   = 1100 * (R1 + R2) / R2
    //     Vin = (adc * k + bandgap/2) / bandgap
    //
    // At the shipped calibration k is 7850 exactly, so this is bit-identical to
    // the uint64 form. Off-nominal, the worst deviation is 5 mV and it is
    // ONE-DIRECTIONAL -- this form only ever under-reads. It therefore cannot
    // suppress a high-side VPP error (which fires on an over-read); the only
    // possible effect is a spurious low-side warning within 5 mV of the edge.
    //
    // The guards below keep both products inside uint32: 1100*(R1+R2) needs
    // R1+R2 <= 3904515, hence the 3900000 guard; adc*k needs k <= 4198404 for
    // adc <= 1023, hence 4194303 -- 0x3FFFFF, so it compiles to a shift test. An
    // implausible calibration returns 0, as r2 == 0 already does.
    //
    // This TU is outside every native build_src_filter, so this function has no
    // native and no bench coverage -- only a host-side numerical oracle bound to
    // the shipped C by a source-contract scan.
    uint32_t sum = r1 + r2;
    if (sum > 3900000UL) {
        return 0;
    }
    uint32_t k = (1100UL * sum) / r2;
    if (k > 4194303UL) {
        return 0;
    }
    uint32_t bg = (uint32_t)bandgap_adc_reading;
    return (uint16_t)((voltage_adc_reading * k + bg / 2) / bg);
}
#endif