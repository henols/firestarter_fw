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
 * Each field is range-checked on its own, not gated on the version string, so
 * a stale out-of-range value is corrected on every call. A field that is out
 * of range is replaced by its default.
 *
 * hardware_revision keeps any known revision (0 to 5) and 0xFF ("no
 * override"). Any other value is replaced by 0xFF, so a version bump never
 * clears an operator's shield-revision override.
 */
bool rurp_config_migrate(rurp_configuration_t* config);

#ifdef __cplusplus
}
#endif

#endif  // RURP_CONFIG_MIGRATE_H
