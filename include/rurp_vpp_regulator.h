#ifndef RURP_VPP_REGULATOR_H
#define RURP_VPP_REGULATOR_H

/*
 * HAL-independent VPP regulation core. The MIC2288 regulates itself; this
 * code only moves the DAC code that biases its feedback node. A LOWER DAC
 * code gives a HIGHER rail. The idle (safe) state is the HIGH code.
 * All hardware access goes through rurp_vpp_regulator_ops_t so the logic
 * runs against a fake plant on a host.
 */

#include <stdint.h>
#include "rurp_vpp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    /* Return 0 and set *mv on success; non-zero on any ADC failure. */
    int (*adc_read_mv)(void *ctx, uint16_t *mv);
    /* Return 0 on success. */
    int (*dac_write)(void *ctx, uint16_t code);
    void (*boost_enable)(void *ctx, int on);
    uint32_t (*now_ms)(void *ctx);
    void (*delay_ms)(void *ctx, uint16_t ms);
    /* Return non-zero to abort. May be NULL. */
    int (*cancelled)(void *ctx);
} rurp_vpp_regulator_ops_t;

typedef struct {
    uint16_t dac_code_min;       /* lowest code allowed (highest rail) */
    uint16_t dac_code_max;       /* safe idle code (lowest rail) */
    uint16_t max_rail_mv;        /* targets above this are OUT_OF_RANGE */
    uint16_t overvoltage_mv;     /* absolute reading that aborts immediately */
    uint16_t min_settle_ms;      /* wait after every DAC change */
    uint8_t  required_good;      /* consecutive in-tolerance readings */
    uint8_t  max_step_codes;     /* largest single DAC move */
    uint16_t codes_per_volt;     /* initial gain estimate for step sizing */
} rurp_vpp_regulator_config_t;

rurp_vpp_result_t rurp_vpp_regulate(const rurp_vpp_regulator_ops_t *ops,
                                    const rurp_vpp_regulator_config_t *cfg,
                                    uint16_t target_mv, uint16_t tolerance_mv,
                                    uint16_t timeout_ms);

/* Boost off, DAC to safe idle code. Best effort, always attempts both. */
void rurp_vpp_regulator_disable(const rurp_vpp_regulator_ops_t *ops,
                                const rurp_vpp_regulator_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif  // RURP_VPP_REGULATOR_H
