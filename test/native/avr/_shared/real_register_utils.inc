/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The real register cache, for suites whose host_stubs.cpp defines
 * HOST_STUBS_REAL_REGISTER_UTILS. Include it AFTER host_stubs_common.inc.
 */

#include "rurp_register_utils.h"

/* Seeds the production register cache, so a test starts from a known
 * latched state. */
extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl) {
    lsb_address = lsb;
    msb_address = msb;
    control_register = ctrl;
}
