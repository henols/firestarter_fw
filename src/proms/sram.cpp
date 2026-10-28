/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "sram.h"
#include "firestarter.h"
#include "logging_id.h"
#include "march.h"
#include "proto_constants.h"

void configure_sram(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_SRAM);
    // 0x0E reaches only battery-backed NVRAM read/write rows, never a RAM-test
    // row, so the RAM test stays refused there (NULL main -> NOT_SUPPORTED).
    if (handle->cmd == CMD_RAM_TEST && handle->protocol != PROTO_SRAM_32PIN) {
        march_begin(handle);
        handle->firestarter_operation_main = march_execute;
    }
}
