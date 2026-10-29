/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_voltage_math.h"

/* Narrow a 32-bit result to 16 bits. An over-range value saturates, so it still
 * reads as a high rail and cannot wrap into a small, plausible-looking number. */
static uint16_t saturate_u16(uint32_t v) {
    return v > 0xFFFFUL ? 0xFFFF : (uint16_t)v;
}

uint8_t rurp_calibration_is_plausible(uint32_t r1, uint32_t r2, uint32_t bandgap_mv) {
    uint32_t sum;
    if (r2 == 0) {
        return 0;
    }
    if (bandgap_mv < RURP_BANDGAP_MIN_MV || bandgap_mv > RURP_BANDGAP_MAX_MV) {
        return 0;
    }
    sum = r1 + r2;
    if (sum < r1 || sum > RURP_DIVIDER_SUM_MAX) {
        return 0;
    }
    if ((bandgap_mv * sum) / r2 > RURP_SCALE_K_MAX) {
        return 0;
    }
    return 1;
}

uint16_t rurp_scale_voltage_mv(uint32_t adc, uint32_t bandgap_adc, uint32_t r1, uint32_t r2,
                               uint32_t bandgap_mv) {
    uint32_t k;
    if (bandgap_adc == 0 || !rurp_calibration_is_plausible(r1, r2, bandgap_mv)) {
        return 0;
    }
    k = (bandgap_mv * (r1 + r2)) / r2;
    /* Round to nearest rather than truncating. */
    return saturate_u16((adc * k + bandgap_adc / 2) / bandgap_adc);
}

uint16_t rurp_scale_vcc_mv(uint32_t bandgap_adc, uint32_t bandgap_mv) {
    if (bandgap_adc == 0) {
        return 0;
    }
    /* Round to nearest rather than truncating. */
    return saturate_u16((bandgap_mv * RURP_ADC_FULL_SCALE + bandgap_adc / 2) / bandgap_adc);
}
