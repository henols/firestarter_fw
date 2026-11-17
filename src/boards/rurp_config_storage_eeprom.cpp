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

// AVR EEPROM offset; the shared configuration policy is storage-agnostic.
#define CONFIG_START 48

bool rurp_config_storage_load(void* blob, size_t len) {
    // EEPROM.get()/put() transfer sizeof(rurp_configuration_t) bytes through
    // typed objects, not the caller-supplied len.
    rurp_configuration_t* config = static_cast<rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.get(CONFIG_START, *config);
    // EEPROM.get() has no read-status result; validate the bytes separately.
    return true;
}

bool rurp_config_storage_save(const void* blob, size_t len) {
    const rurp_configuration_t* config = static_cast<const rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.put(CONFIG_START, *config);
    return true;
}

#endif
