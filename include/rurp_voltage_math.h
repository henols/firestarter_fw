/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef RURP_VOLTAGE_MATH_H
#define RURP_VOLTAGE_MATH_H

#include <stdint.h>

/*
 * The ADC-count-to-millivolt arithmetic, with no hardware access.
 *
 * This unit deliberately includes no <Arduino.h>, touches no AVR register
 * and is gated on no board macro, so it compiles natively and is covered by
 * test/native/avr/test_voltage_math. The register access that feeds it stays
 * in src/boards/rurp_common.cpp.
 *
 * Both conversions are ratiometric against the same bandgap reading, so the
 * supply voltage cancels exactly and is not an error source. Only two terms
 * can be wrong: RURP_BANDGAP_NOMINAL_MV below, and the caller's r1/r2.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The assumed internal bandgap, in millivolts.
 *
 * This is a NOMINAL figure, not a measured one. The real bandgap of an
 * individual ATmega is specified only as 1.0 V to 1.2 V and the part carries
 * no factory trim word, so this constant is wrong by up to +/-9 % on any
 * given chip, multiplicatively, on every reading it feeds.
 *
 * The error is observable without any instrument: rurp_scale_vcc_mv() returns
 * supply x RURP_BANDGAP_NOMINAL_MV / bandgap_true, so a reported supply that
 * the board cannot physically be running at measures the discrepancy
 * directly. Do not correct this constant against a single board -- it is a
 * per-chip quantity and a per-board correction belongs in the config.
 */
#define RURP_BANDGAP_NOMINAL_MV 1100UL

/*
 * The plausible range for a MEASURED bandgap, from the ATmega datasheet's
 * 1.0-1.2 V specification. A calibration landing outside this is not a
 * calibration -- it is a typo or a bad meter reading -- and is REFUSED, never
 * clamped: a stored value the firmware trusts decides programming voltages.
 */
#define RURP_BANDGAP_MIN_MV 1000U
#define RURP_BANDGAP_MAX_MV 1200U

/*
 * ADC full scale, in counts: 2^n, not 2^n-1, because the transfer function is
 * count = V/Vref * 2^n.
 *
 * Platform-conditional because this header is linked into BOTH targets --
 * src/proms/memory.cpp and src/firestarter.cpp call into this unit, so it is
 * NOT an AVR-only translation unit however much the 10-bit arithmetic below
 * looks like one. Getting this wrong on ARM would make a calibration wrong by
 * a factor of four, silently.
 */
#if defined(RURP_PLATFORM_PY32F071)
#define RURP_ADC_FULL_SCALE 4096UL /* 12-bit */
#else
#define RURP_ADC_FULL_SCALE 1024UL /* 10-bit */
#endif

/*
 * Guards that keep both products inside uint32 in rurp_scale_voltage_mv:
 * RURP_BANDGAP_NOMINAL_MV * (r1 + r2) needs r1 + r2 <= 3904515, and
 * adc * k needs k <= 4198404 for adc <= 1023. The shipped bounds sit just
 * below each true limit; RURP_SCALE_K_MAX is 0x3FFFFF so it compiles to a
 * shift test.
 */
#define RURP_DIVIDER_SUM_MAX 3900000UL
#define RURP_SCALE_K_MAX 4194303UL

/*
 * Convert a divider ADC reading to millivolts at the divider's input.
 *
 *   Vin_mV = (adc * bandgap_mv * (r1 + r2)) / (bandgap_adc * r2)
 *
 * Evaluated entirely in 32-bit by folding the divider into one scale factor
 * FIRST, rather than forming a 64-bit numerator:
 *
 *   k   = bandgap_mv * (r1 + r2) / r2
 *   Vin = (adc * k + bandgap_adc / 2) / bandgap_adc
 *
 * At the shipped calibration k is 7850 exactly, so this is bit-identical to
 * the uint64 form. Off-nominal, the worst deviation is 5 mV and it is
 * ONE-DIRECTIONAL -- this form only ever under-reads. It therefore cannot
 * suppress a high-side VPP error (which fires on an over-read); the only
 * possible effect is a spurious low-side warning within 5 mV of the edge.
 *
 * Returns 0 when the calibration cannot be evaluated. A caller on a
 * high-voltage path must not read 0 as "the rail is low" -- use
 * rurp_calibration_is_plausible() to refuse before energising anything.
 */
uint16_t rurp_scale_voltage_mv(uint32_t adc, uint32_t bandgap_adc, uint32_t r1, uint32_t r2,
                               uint32_t bandgap_mv);

/*
 * Convert a bandgap ADC reading to the supply voltage in millivolts.
 *
 *   VCC_mV = (bandgap_mv * 1024) / bandgap_adc
 *
 * Returns 0 when bandgap_adc is 0.
 */
uint16_t rurp_scale_vcc_mv(uint32_t bandgap_adc, uint32_t bandgap_mv);

/*
 * True when r1/r2 can be evaluated by rurp_scale_voltage_mv without a guard
 * returning 0. This is the same test the conversion applies, exposed so a
 * pre-flight check can refuse an unusable calibration before any
 * high-voltage bit is set, instead of reading a 0 mV result as a low rail.
 */
uint8_t rurp_calibration_is_plausible(uint32_t r1, uint32_t r2, uint32_t bandgap_mv);

/*
 * Back-solve this MCU's bandgap from an operator meter reading of the supply.
 *
 *   Vbg = VCC_meter * bandgap_adc / adc_full_scale
 *
 * Returns 0 when the result is outside RURP_BANDGAP_MIN_MV..MAX_MV -- a
 * refusal, never a clamp. A stored bandgap is a value the firmware TRUSTS to
 * judge programming voltages, so an implausible one must not be written.
 */
uint16_t rurp_bandgap_from_measured_vcc(uint32_t vcc_meter_mv, uint32_t bandgap_adc,
                                        uint32_t adc_full_scale);

#ifdef __cplusplus
}
#endif

#endif  // RURP_VOLTAGE_MATH_H
