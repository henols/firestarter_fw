/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __FLASH_NOR_UNLOCK_H__
#define __FLASH_NOR_UNLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "firestarter.h"

    void configure_flash_nor_unlock(firestarter_handle_t* handle);

    // CMD_LOCK_STATUS operation for the 0x06 AMD
    // Autoselect family. Declared here so the native suite (and any future
    // caller) can drive it directly without going through configure_memory.
    void flash_nor_unlock_read_protection_execute(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_NOR_UNLOCK_H__