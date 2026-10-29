/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __MARCH_H__
#define __MARCH_H__

/* March C- RAM test for SRAM (CMD_RAM_TEST), on four data backgrounds. The
 * test writes and reads every address through handle->firestarter_set_data/
 * get_data. It runs in bounded batches, so one main callback never blocks
 * loop() for long. */
#include "firestarter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Addresses processed in one main callback. An address costs at most two bus
// operations, so a batch stays well inside TIMEOUT_MS on every board.
#define MARCH_BATCH_SIZE 256

// The number of read and write operations per address in one March C- run.
#define MARCH_C_MINUS_OPS_PER_ADDRESS 10

// The number of data backgrounds. March C- runs once for each.
#define MARCH_BACKGROUNDS 4

    /* Reset the test state. configure_sram() calls this once per command. */
    void march_begin(firestarter_handle_t* handle);

    /* Main callback: process one batch. The operation stays in progress until
     * the last element ends. On the first mismatch it sends MSG_ERR_RAM_TEST
     * and sets RESPONSE_CODE_ERROR. */
    void march_execute(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif // __MARCH_H__
