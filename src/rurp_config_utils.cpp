/*
 * Project Name: Firestarter
 * Copyright (c) 2025 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_shield.h" // For CONFIG_VERSION, VALUE_R1, VALUE_R2
#include "rurp_config_storage.h"
#include "rurp_config_migrate.h"

#include <string.h>

// Define the global configuration variable here.
// This is the single definition that the linker will use.
rurp_configuration_t rurp_config;

// The functions below are declared in rurp_shield.h.
rurp_configuration_t* rurp_get_config() {
    return &rurp_config;
}

void rurp_load_config() {
    rurp_configuration_t* config = rurp_get_config();
    // A failed load leaves no usable record: start from zeroed fields with no
    // hardware-revision override (0xFF), so that zeroed memory is never read
    // as a Rev 0 override. rurp_validate_config() then fills in the defaults.
    // After a successful load it decides, for each field, whether the stored
    // value is usable.
    if (!rurp_config_storage_load(config, sizeof(*config))) {
        memset(config, 0, sizeof(*config));
        config->hardware_revision = 0xFF;
    }
    rurp_validate_config(config);
}

void rurp_save_config(rurp_configuration_t* config) {
    // The write result is not used: no caller can act on a failed save.
    (void)rurp_config_storage_save(config, sizeof(*config));
}

void rurp_validate_config(rurp_configuration_t* config) {
    if (rurp_config_migrate(config)) {
        rurp_save_config(config);
    }
}
