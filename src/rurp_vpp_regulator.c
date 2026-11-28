/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_vpp_regulator.h"

static rurp_vpp_result_t fail(const rurp_vpp_regulator_ops_t *ops,
                              const rurp_vpp_regulator_config_t *cfg,
                              rurp_vpp_result_t result) {
    rurp_vpp_regulator_disable(ops, cfg);
    return result;
}

void rurp_vpp_regulator_disable(const rurp_vpp_regulator_ops_t *ops,
                                const rurp_vpp_regulator_config_t *cfg) {
    ops->boost_enable(ops->ctx, 0);
    (void)ops->dac_write(ops->ctx, cfg->dac_code_max);
}

rurp_vpp_result_t rurp_vpp_regulate(const rurp_vpp_regulator_ops_t *ops,
                                    const rurp_vpp_regulator_config_t *cfg,
                                    uint16_t target_mv, uint16_t tolerance_mv,
                                    uint16_t timeout_ms) {
    if (target_mv == 0 || target_mv > cfg->max_rail_mv || cfg->dac_code_min > cfg->dac_code_max ||
        cfg->required_good == 0 || cfg->max_step_codes == 0) {
        return RURP_VPP_OUT_OF_RANGE;
    }

    uint16_t code = cfg->dac_code_max;
    if (ops->dac_write(ops->ctx, code) != 0) {
        return fail(ops, cfg, RURP_VPP_DAC_ERROR);
    }
    ops->boost_enable(ops->ctx, 1);

    const uint32_t start = ops->now_ms(ops->ctx);
    uint8_t good = 0;

    for (;;) {
        ops->delay_ms(ops->ctx, cfg->min_settle_ms);

        if (ops->cancelled != 0 && ops->cancelled(ops->ctx)) {
            return fail(ops, cfg, RURP_VPP_ABORTED);
        }

        uint16_t mv = 0;
        if (ops->adc_read_mv(ops->ctx, &mv) != 0 || mv == 0) {
            /* A failed or zero reading is never usable. */
            return fail(ops, cfg, RURP_VPP_ADC_ERROR);
        }
        if (mv >= cfg->overvoltage_mv) {
            return fail(ops, cfg, RURP_VPP_OVERVOLTAGE);
        }

        const int32_t error = (int32_t)target_mv - (int32_t)mv; /* >0: need more rail */
        const int32_t abs_error = error < 0 ? -error : error;

        if (abs_error <= (int32_t)tolerance_mv) {
            if (++good >= cfg->required_good) {
                return RURP_VPP_OK;
            }
        } else {
            good = 0;
            int32_t step = (abs_error * (int32_t)cfg->codes_per_volt) / 1000;
            if (step < 1) step = 1;
            if (step > cfg->max_step_codes) step = cfg->max_step_codes;
            /* Overshoot: take smaller steps so we creep back down. */
            if (error < 0 && step > 1) step /= 2;

            int32_t next = (int32_t)code + (error > 0 ? -step : step);
            if (next < (int32_t)cfg->dac_code_min) next = cfg->dac_code_min;
            if (next > (int32_t)cfg->dac_code_max) next = cfg->dac_code_max;

            if ((uint16_t)next == code) {
                /* Clamped: target unreachable with this DAC range. */
                return fail(ops, cfg, RURP_VPP_OUT_OF_RANGE);
            }
            code = (uint16_t)next;
            if (ops->dac_write(ops->ctx, code) != 0) {
                return fail(ops, cfg, RURP_VPP_DAC_ERROR);
            }
        }

        if ((uint32_t)(ops->now_ms(ops->ctx) - start) >= timeout_ms) {
            return fail(ops, cfg, RURP_VPP_TIMEOUT);
        }
    }
}
