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

// Moved verbatim from src/rurp_config_utils.cpp.
// This is an EEPROM ADDRESS, meaningless on py32 -- which is precisely why
// it lives here, below the storage seam, instead of in the common policy
// layer or the seam header.
#define CONFIG_START 48

bool rurp_config_storage_load(void* blob, size_t len) {
    // The cast to rurp_configuration_t* -- rather than a byte loop over
    // `len` -- is what makes this emit the identical
    // (48, sizeof(rurp_configuration_t)) EEPROM.get() access the
    // pre-refactor code produced: EEPROM.get()/put() are templates whose
    // transferred length comes from sizeof(T), not from an explicit count.
    rurp_configuration_t* config = static_cast<rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.get(CONFIG_START, *config);
    // Returning true unconditionally is byte-identical to the pre-refactor
    // behaviour: EEPROM always yields bytes, and rurp_validate_config()
    // (in the common policy layer) decides whether they are usable. This
    // is exactly what this seam requires, and it is why the design rejected a
    // richer status enum for the AVR side.
    return true;
}

bool rurp_config_storage_save(const void* blob, size_t len) {
    const rurp_configuration_t* config = static_cast<const rurp_configuration_t*>(blob);
    (void)len;
    EEPROM.put(CONFIG_START, *config);
    return true;
}

#endif
