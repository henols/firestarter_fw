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

void configure_sram(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_SRAM);
    // The RAM test runs on every SRAM protocol. The wire carries no volatility
    // field, so the firmware cannot tell a battery-backed part from a volatile
    // one. The host refuses non-volatile rows unless the operator gives --force.
    if (handle->cmd == CMD_RAM_TEST) {
        march_begin(handle);
        handle->firestarter_operation_main = march_execute;
    }
}
