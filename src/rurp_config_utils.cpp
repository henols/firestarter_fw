/*
 * Project Name: Firestarter
 * Copyright (c) 2025 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_shield.h" // For CONFIG_VERSION, VALUE_R1, VALUE_R2
#include "rurp_config_storage.h"
#include "rurp_config_migrate.h"

// Define the global configuration variable here.
// This is the single definition that the linker will use.
// hardware_revision starts at 0xFF ("no override"): board setup writes the
// control register before rurp_load_config() runs, and 0 would read as a
// REVISION_0 override.
rurp_configuration_t rurp_config = {"", 0, 0, 0xFF, 0};

// Define the functions here. Their declarations are in rurp_shield.h
rurp_configuration_t* rurp_get_config() {
    return &rurp_config;
}

void rurp_load_config() {
    rurp_configuration_t* config = rurp_get_config();
    // A false load copied nothing: the buffer still holds what it held
    // before. Clear the version so rurp_validate_config() treats the buffer
    // as foreign and resets every field, rather than keeping a stale
    // hardware_revision as an override.
    if (!rurp_config_storage_load(config, sizeof(*config))) {
        config->version[0] = '\0';
    }
    rurp_validate_config(config);
}

void rurp_save_config(rurp_configuration_t* config) {
    // Result deliberately discarded: no caller has a recovery for a failed write.
    (void)rurp_config_storage_save(config, sizeof(*config));
}

void rurp_validate_config(rurp_configuration_t* config) {
    if (rurp_config_migrate(config)) {
        rurp_save_config(config);
    }
}
