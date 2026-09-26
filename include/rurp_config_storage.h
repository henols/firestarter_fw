/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __RURP_CONFIG_STORAGE_H__
#define __RURP_CONFIG_STORAGE_H__

/*
 * The configuration-persistence storage seam: exactly two functions, a blob
 * load and a blob save. Everything above the seam -- rurp_get_config,
 * rurp_load_config, rurp_save_config, rurp_validate_config -- stays in the
 * common policy layer.
 *
 * Do NOT include this from rurp_shield.h. That header is reachable from 46
 * translation units, 14 of them native host_stubs.cpp files, and ONE added
 * #include there was measured to take `pio test -e native` from 141 cases
 * passing to 0.
 *
 * AVR behaviour is unchanged: the AVR load returns true unconditionally after
 * the typed EEPROM.get(), and the policy layer calls rurp_validate_config()
 * either way -- exactly as before the seam existed.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the persisted configuration byte blob from this platform's
 * backing store into `blob`.
 *
 * @param blob Destination buffer, at least `len` bytes.
 * @param len  Number of bytes to read -- callers pass
 *             sizeof(rurp_configuration_t).
 * @return true if the read was performed (AVR: always true -- EEPROM always
 *         yields bytes, and rurp_validate_config() decides whether they are
 *         usable); false if this platform can positively determine no valid
 *         record exists (py32: both slots blank or CRC-rejected).
 */
bool rurp_config_storage_load(void* blob, size_t len);

/**
 * @brief Persist `len` bytes from `blob` to this platform's backing store.
 *
 * @param blob Source buffer, at least `len` bytes.
 * @param len  Number of bytes to write -- callers pass
 *             sizeof(rurp_configuration_t).
 * @return true if the write was performed (AVR: always true). py32's
 *         dual-slot backend also returns true unconditionally
 *         here -- a write failure mode has no consumer today.
 */
bool rurp_config_storage_save(const void* blob, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __RURP_CONFIG_STORAGE_H__
