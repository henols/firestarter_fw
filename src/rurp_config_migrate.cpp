/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_config_migrate.h"

#include <string.h>

#include "rurp_shield.h"
#include "rurp_voltage_math.h"

bool rurp_config_migrate(rurp_configuration_t* config) {
    bool changed = false;

    if (config->r1 < VALUE_R1_MIN || config->r1 > VALUE_R1_MAX) {
        config->r1 = VALUE_R1;
        changed = true;
    }
    if (config->r2 < VALUE_R2_MIN || config->r2 > VALUE_R2_MAX) {
        config->r2 = VALUE_R2;
        changed = true;
    }
    if (config->bandgap_mv < RURP_BANDGAP_MIN_MV || config->bandgap_mv > RURP_BANDGAP_MAX_MV) {
        config->bandgap_mv = (uint16_t)RURP_BANDGAP_NOMINAL_MV;
        changed = true;
    }
    if (strncmp(config->version, CONFIG_VERSION, sizeof(config->version)) != 0) {
        strcpy(config->version, CONFIG_VERSION);
        changed = true;
    }
    return changed;
}
