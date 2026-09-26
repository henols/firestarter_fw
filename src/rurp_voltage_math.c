/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_voltage_math.h"

uint8_t rurp_calibration_is_plausible(uint32_t r1, uint32_t r2, uint32_t bandgap_mv) {
    uint32_t sum;
    if (r2 == 0) {
        return 0;
    }
    if (bandgap_mv < RURP_BANDGAP_MIN_MV || bandgap_mv > RURP_BANDGAP_MAX_MV) {
        return 0;
    }
    sum = r1 + r2;
    if (sum > RURP_DIVIDER_SUM_MAX) {
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
    return (uint16_t)((adc * k + bandgap_adc / 2) / bandgap_adc);
}

uint16_t rurp_scale_vcc_mv(uint32_t bandgap_adc, uint32_t bandgap_mv) {
    if (bandgap_adc == 0) {
        return 0;
    }
    /* Round to nearest rather than truncating. */
    return (uint16_t)((bandgap_mv * RURP_ADC_FULL_SCALE + bandgap_adc / 2) / bandgap_adc);
}

uint16_t rurp_bandgap_from_measured_vcc(uint32_t vcc_meter_mv, uint32_t bandgap_adc) {
    uint32_t bg;
    if (bandgap_adc == 0 || vcc_meter_mv == 0) {
        return 0;
    }
    /*
     * The ADC is ratiometric, so bandgap_adc = Vbg / VCC * 1024 and therefore
     * Vbg = VCC_meter * bandgap_adc / 1024. One meter reading on the supply
     * pin is the whole measurement -- no pot and no high voltage.
     */
    bg = (vcc_meter_mv * bandgap_adc + RURP_ADC_FULL_SCALE / 2) / RURP_ADC_FULL_SCALE;
    if (bg < RURP_BANDGAP_MIN_MV || bg > RURP_BANDGAP_MAX_MV) {
        return 0;  /* refuse, never clamp */
    }
    return (uint16_t)bg;
}
