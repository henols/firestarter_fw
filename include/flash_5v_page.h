/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __FLASH_5V_PAGE_H__
#define __FLASH_5V_PAGE_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "firestarter.h"

    void configure_flash_5v_page(firestarter_handle_t* handle);

    // CMD_LOCK_STATUS operation for the 0x05 Winbond
    // Product-ID boot-block family. Declared here so the native suite (and
    // any future caller) can drive it directly without going through
    // configure_memory.
    void flash_5v_page_read_protection_execute(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_5V_PAGE_H__