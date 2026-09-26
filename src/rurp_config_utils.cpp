/*
 * Project Name: Firestarter
 * Copyright (c) 2025 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_shield.h" // For CONFIG_VERSION, VALUE_R1, VALUE_R2
#include "rurp_config_storage.h"

// Define the global configuration variable here.
// This is the single definition that the linker will use.
rurp_configuration_t rurp_config;

// Define the functions here. Their declarations are in rurp_shield.h
rurp_configuration_t* rurp_get_config() {
    return &rurp_config;
}

void rurp_load_config() {
    rurp_configuration_t* config = rurp_get_config();
    // The bool result is deliberately discarded: rurp_validate_config()
    // below decides usability of whatever bytes came back (or were left
    // untouched), which is what keeps one validate policy on both
    // platforms rather than branching per platform here.
    (void)rurp_config_storage_load(config, sizeof(*config));
    rurp_validate_config(config);
}

void rurp_save_config(rurp_configuration_t* config) {
    // Result deliberately discarded -- see rurp_load_config() above.
    (void)rurp_config_storage_save(config, sizeof(*config));
}

void rurp_validate_config(rurp_configuration_t* config) {
    if (strcmp(config->version, CONFIG_VERSION) != 0) {
        strcpy(config->version, CONFIG_VERSION);
        config->r1 = VALUE_R1;
        config->r2 = VALUE_R2;
        config->hardware_revision = 0xFF;
        rurp_save_config(config);
    }
}