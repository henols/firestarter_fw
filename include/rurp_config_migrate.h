/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef RURP_CONFIG_MIGRATE_H
#define RURP_CONFIG_MIGRATE_H

#include <stdbool.h>

#include "rurp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bring a stored configuration up to date, field by field, and report whether
 * anything changed. Pure: it touches no storage, so it is natively testable.
 *
 * Range-checking each field rather than gating on the version string is what
 * makes this reach a stale value the old policy never could. A board whose
 * EEPROM carried the pre-Phase-44 r1 of 1000 kept it forever, because the
 * version string already matched and the wipe only ran on a mismatch.
 *
 * hardware_revision is preserved unconditionally. Every value including 0xFF
 * ("no override") is meaningful, so there is no band to test it against -- and
 * the previous policy reset it on every version bump, silently destroying an
 * operator's shield-revision override.
 */
bool rurp_config_migrate(rurp_configuration_t* config);

#ifdef __cplusplus
}
#endif

#endif  // RURP_CONFIG_MIGRATE_H
