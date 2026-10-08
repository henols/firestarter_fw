/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#if defined(ARDUINO_AVR_UNO) || defined(ARDUINO_AVR_ATmega328PB) || defined(ARDUINO_AVR_LEONARDO)
#include "rurp_config_storage.h"
#include "rurp_shield.h"
#include <EEPROM.h>

// This is an EEPROM ADDRESS, meaningless on py32 -- which is precisely why
// it lives here, below the storage seam, instead of in the common policy
// layer or the seam header.
#define CONFIG_START 48

bool rurp_config_storage_load(void* blob, size_t len) {
    // EEPROM.get()/put() are templates whose transferred length comes from
    // sizeof(T), not from an explicit count, so the blob is cast to
    // rurp_configuration_t* and `len` is unused.
    rurp_configuration_t* config = static_cast<rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.get(CONFIG_START, *config);
    // EEPROM always yields bytes, so this returns true unconditionally.
    // rurp_validate_config() (in the common policy layer) decides whether
    // they are usable.
    return true;
}

bool rurp_config_storage_save(const void* blob, size_t len) {
    const rurp_configuration_t* config = static_cast<const rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.put(CONFIG_START, *config);
    return true;
}

#endif
