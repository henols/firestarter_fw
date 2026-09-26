/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_voltage_math.h"

uint8_t rurp_calibration_is_plausible(uint32_t r1, uint32_t r2) {
    uint32_t sum;
    if (r2 == 0) {
        return 0;
    }
    sum = r1 + r2;
    if (sum > RURP_DIVIDER_SUM_MAX) {
        return 0;
    }
    if ((RURP_BANDGAP_NOMINAL_MV * sum) / r2 > RURP_SCALE_K_MAX) {
        return 0;
    }
    return 1;
}

uint16_t rurp_scale_voltage_mv(uint32_t adc, uint32_t bandgap_adc, uint32_t r1, uint32_t r2) {
    uint32_t k;
    if (bandgap_adc == 0 || !rurp_calibration_is_plausible(r1, r2)) {
        return 0;
    }
    k = (RURP_BANDGAP_NOMINAL_MV * (r1 + r2)) / r2;
    /* Round to nearest rather than truncating. */
    return (uint16_t)((adc * k + bandgap_adc / 2) / bandgap_adc);
}

uint16_t rurp_scale_vcc_mv(uint32_t bandgap_adc) {
    if (bandgap_adc == 0) {
        return 0;
    }
    /* Round to nearest rather than truncating. */
    return (uint16_t)((RURP_BANDGAP_NOMINAL_MV * RURP_ADC_FULL_SCALE + bandgap_adc / 2) / bandgap_adc);
}
