/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef RURP_ADC_MUX_H
#define RURP_ADC_MUX_H

#include <stdint.h>

/*
 * ADC multiplexer arithmetic, with no register access.
 *
 * These are static inline and pure so the values the AVR code writes are
 * COMPUTED from the channel number rather than hand-assembled from bit names,
 * and so test/native/avr/test_adc_mux can assert them. They cost no flash: the
 * arguments are compile-time constants at every call site.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* REFS[1:0] = 01 -- AVCC with an external capacitor at AREF. The same value on
 * ATmega328P/PB and ATmega32U4.
 *
 * REFS[1:0] = 11 is deliberately NOT used, and it is where the two parts
 * disagree: 1.1 V on the 328P, 2.56 V on the 32U4. Selecting it would make the
 * Leonardo behave unlike the Unos for no accuracy gain -- the 2.56 V reference
 * is untrimmed too -- and the divider puts 3102 mV on the pin at maximum VPE,
 * which saturates either internal reference.
 */
#define RURP_ADC_REF_AVCC 1u

/*
 * The internal bandgap's channel NUMBER, which differs between the parts.
 * Both parts' bandgap is 1.1 V nominal; only its position in the mux moves.
 */
#define RURP_ADC_CHANNEL_BANDGAP_328P 14u  /* MUX[3:0]   = 1110   */
#define RURP_ADC_CHANNEL_BANDGAP_32U4 30u  /* MUX[5:0] = 011110   */

/* ADMUX carries the reference in bits 7:6 and the low five channel bits. */
static inline uint8_t rurp_admux_value(uint8_t refs, uint8_t channel) {
    return (uint8_t)(((refs & 0x03u) << 6) | (channel & 0x1Fu));
}

/*
 * The sixth channel bit. On the ATmega32U4 it is MUX5 in ADCSRB, NOT in ADMUX,
 * so writing ADMUX alone leaves it at whatever the previous conversion set.
 *
 * This is why it matters: the bandgap is channel 30, which needs MUX5 clear.
 * Channels 8..13 (Leonardo A6..A11) set it. This firmware reads only A2 and A3
 * -- ADC5 and ADC4 -- so MUX5 is clear today by coincidence, not by
 * construction. A future read of any higher channel would silently redirect
 * the next bandgap conversion, and on a calibrated board that corrupts every
 * voltage reading, a stored calibration included.
 */
static inline uint8_t rurp_mux5_value(uint8_t channel) {
    return (uint8_t)((channel >> 5) & 0x01u);
}

#ifdef __cplusplus
}
#endif

#endif  // RURP_ADC_MUX_H
