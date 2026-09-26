/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __DEV_TOOLS_H__
#define __DEV_TOOLS_H__

#if DEV_TOOLS
#include "firestarter.h"

#ifdef __cplusplus
extern "C" {
#endif

    bool dt_set_registers(firestarter_handle_t* handle);
    bool dt_set_address(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif
#endif
#endif // __DEV_TOOLS_H__