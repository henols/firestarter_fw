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

#define NO_HARDWARE_REVISION_OVERRIDE 0xFF

static bool hardware_revision_known(uint8_t revision) {
    if (revision == NO_HARDWARE_REVISION_OVERRIDE) {
        return true;
    }
#ifdef HARDWARE_REVISION
    // REVISION_0 is 0, so the lower bound needs no test.
    return revision <= REVISION_2_3;
#else
    // A build without revision support has no revision to override with.
    return false;
#endif
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Every configuration this firmware has written carries "VER" and two digits.
static bool is_config_version(const char* version) {
    return version[0] == 'V' && version[1] == 'E' && version[2] == 'R' &&
           is_digit(version[3]) && is_digit(version[4]) && version[5] == '\0';
}

/* Find each field the firmware may not use and, when `correct` is true,
 * reset it to its default. Returns true when any field is unusable. Out of
 * line because the boot migration and the config command's validity check
 * both call it, and on AVR one copy of the 32-bit band tests is smaller than
 * two. With `correct` false it writes nothing. */
static bool __attribute__((noinline)) check_fields(rurp_configuration_t* config, bool correct) {
    bool unusable = false;

    if (config->r1 < VALUE_R1_MIN || config->r1 > VALUE_R1_MAX) {
        if (correct) config->r1 = VALUE_R1;
        unusable = true;
    }
    if (config->r2 < VALUE_R2_MIN || config->r2 > VALUE_R2_MAX) {
        if (correct) config->r2 = VALUE_R2;
        unusable = true;
    }
    if (config->bandgap_mv < RURP_BANDGAP_MIN_MV || config->bandgap_mv > RURP_BANDGAP_MAX_MV) {
        if (correct) config->bandgap_mv = (uint16_t)RURP_BANDGAP_NOMINAL_MV;
        unusable = true;
    }
    if (!hardware_revision_known(config->hardware_revision)) {
        if (correct) config->hardware_revision = NO_HARDWARE_REVISION_OVERRIDE;
        unusable = true;
    }
    return unusable;
}

bool rurp_config_is_valid(const rurp_configuration_t* config) {
    return !check_fields(const_cast<rurp_configuration_t*>(config), false);
}

bool rurp_config_commit(rurp_configuration_t* live, const rurp_configuration_t* candidate) {
    if (!rurp_config_is_valid(candidate)) {
        return false;
    }
    *live = *candidate;
    return true;
}

bool rurp_config_migrate(rurp_configuration_t* config) {
    if (!is_config_version(config->version)) {
        // Nothing in a foreign record is trusted, the shield override least
        // of all: a stray 0x00 there would select REVISION_0. Treat it as
        // blank EEPROM. Every value field then fails its band and takes its
        // default, and hardware_revision reads 0xFF, no override.
        memset(config, 0xFF, sizeof(*config));
    }

    bool changed = check_fields(config, true);

    if (strncmp(config->version, CONFIG_VERSION, sizeof(config->version)) != 0) {
        strcpy(config->version, CONFIG_VERSION);
        changed = true;
    }
    return changed;
}
