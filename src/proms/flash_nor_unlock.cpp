/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_nor_unlock.h"

#include <Arduino.h>
#include "memory_utils.h"
#include "flash_utils.h"
#include "firestarter.h"
#include "logging_id.h"
#include "operation_utils.h"

void flash_nor_unlock_erase_execute(firestarter_handle_t* handle);
void flash_nor_unlock_sector_erase(firestarter_handle_t* handle, uint32_t sector_address);
void flash_nor_unlock_write_init(firestarter_handle_t* handle);
void flash_nor_unlock_write_execute(firestarter_handle_t* handle);
void flash_nor_unlock_check_chip_id_execute(firestarter_handle_t* handle);

uint16_t flash_nor_unlock_get_chip_id(firestarter_handle_t* handle);

void flash_nor_unlock_generic_init(firestarter_handle_t* handle);

// Worst-case erase time for older flash chips can be up to 100ms.
// Add a 5ms buffer for safety.
const int FLASH_ERASE_DELAY_MS = 105;

void configure_flash_nor_unlock(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_FLASH);
    handle->firestarter_operation_init = flash_nor_unlock_generic_init;
    switch (handle->cmd) {
    case CMD_WRITE:
        handle->firestarter_operation_init = flash_nor_unlock_write_init;
        handle->firestarter_operation_main = flash_nor_unlock_write_execute;
        break;
    case CMD_ERASE:
        handle->firestarter_operation_main = flash_nor_unlock_erase_execute;
        break;
    case CMD_CHECK_CHIP_ID:
        handle->firestarter_operation_init = NULL;
        handle->firestarter_operation_main = flash_nor_unlock_check_chip_id_execute;
        break;
    // A second query arm, modelled on CMD_CHECK_CHIP_ID
    // just above. This file assigns flash_nor_unlock_generic_init before the
    // switch (see top of this function), so this arm must null it -- a
    // status read has no chip-ID precondition to run. The 0x01 force ctrl
    // flag is deliberately not read on this path: elsewhere in this file's
    // shared chip-ID helper that bit means "downgrade a chip-ID mismatch to
    // a warning", and this read performs no chip-ID check, so honouring the
    // bit here would give one flag two unrelated meanings (--force is a
    // host-side table-refusal bypass, never a firmware bit on this command).
    case CMD_LOCK_STATUS:
        handle->firestarter_operation_init = NULL;
        handle->firestarter_operation_main = flash_nor_unlock_read_protection_execute;
        break;
    }
}

void flash_nor_unlock_generic_init(firestarter_handle_t* handle) {
    if (handle->chip_id > 0) {
        flash_nor_unlock_check_chip_id_execute(handle);
    }
}


void flash_nor_unlock_write_init(firestarter_handle_t* handle) {
    // One-time init (chip-ID + erase + erase-settle delay), gated behind
    // is_operation_in_progress so a future multi-call operation could run
    // it exactly ONCE across repeated re-invocations of this function.
    // The last operation that needed this guard was the region-scoped
    // blank check; it left the firmware in 3.1.0 and nothing sets the
    // flag anymore, so this guard currently always reads true. Retained
    // as defence against a future multi-call operation, at zero flash
    // cost either way.
    if (!is_operation_in_progress(handle)) {
        if (handle->chip_id > 0) {
            flash_nor_unlock_check_chip_id_execute(handle);
            if (handle->response_code == RESPONSE_CODE_ERROR) {
                return;
            }
        }

        if (is_flag_set(FLAG_CAN_ERASE)) {
            if (!is_flag_set(FLAG_SKIP_ERASE)) {
                flash_nor_unlock_erase_execute(handle);
                delay(FLASH_ERASE_DELAY_MS);
            }
            else {
                LOG_DEBUG_ID_SUB(DBG_SKIPPING_ERASE_MEMORY);
                LOG_INFO_ID(MSG_INFO_SKIPPING_ERASE_MEM);
            }
        }
    }
}

void flash_nor_unlock_write_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        flash_execute_command(FLASH_ENABLE_WRITE);
        handle->firestarter_set_data(handle, handle->address + i, handle->data_buffer[i]);

        flash_util_verify_operation(handle, handle->data_buffer[i]);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
    }
}

void flash_nor_unlock_erase_execute(firestarter_handle_t* handle) {
    if (handle->address != 0) {
        LOG_DEBUG_ID_SUB(DBG_SECTOR_ERASE);
        flash_nor_unlock_sector_erase(handle, handle->address);
    } else {
        LOG_DEBUG_ID_SUB(DBG_CHIP_ERASE);
        flash_execute_command(FLASH_ERASE);
    }
}

void flash_nor_unlock_sector_erase(firestarter_handle_t* handle, uint32_t sector_address) {
    byte_flip_t sector_erase_seq[6] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x80},
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {sector_address, 0x30},
    };
    flash_util_byte_flipping(handle, sector_erase_seq, 6);
}


void flash_nor_unlock_check_chip_id_execute(firestarter_handle_t* handle) {
    flash_util_check_chip_id_execute(handle);
}

// CMD_LOCK_STATUS operation for the 0x06 AMD
// Autoselect family. Reads the Sector Group Protection Verify byte at
// FLASH_NOR_UNLOCK_PROTECT_VERIFY_ADDR through the shared AMD/JEDEC ID-mode
// helper and reports the raw silicon byte plus a firmware decode.
// This is a 5V read -- flash_util_read_in_id_mode only
// enters/exits ID mode via FLASH_ENABLE_ID/FLASH_DISABLE_ID, so no VPP/VPE
// control-register bit is ever written on this path.
void flash_nor_unlock_read_protection_execute(firestarter_handle_t* handle) {
    uint8_t raw = flash_util_read_in_id_mode(handle, FLASH_NOR_UNLOCK_PROTECT_VERIFY_ADDR);
    uint8_t _b[2];
    _b[0] = raw;
    if (raw == FLASH_NOR_UNLOCK_PROTECT_UNPROTECTED) {
        _b[1] = 0x00;
        handle->response_code = RESPONSE_CODE_OK;
    } else if (raw == FLASH_NOR_UNLOCK_PROTECT_PROTECTED) {
        _b[1] = 0x01;
        handle->response_code = RESPONSE_CODE_OK;
    } else {
        // Unrecognised raw value: an observation the host must classify,
        // not an error this firmware can adjudicate (151-DESIGN.md §1's
        // 0xFF sentinel convention, reusing hw_get_version's precedent).
        // The DATA frame is emitted either way so the raw byte still
        // reaches the host -- never coerce this into 0x00 or 0x01.
        _b[1] = 0xFF;
        handle->response_code = RESPONSE_CODE_WARNING;
    }
    LOG_DATA_ID_BYTES(MSG_DATA_PROTECTION_STATUS, _b, 2);
}

uint16_t flash_nor_unlock_get_chip_id(firestarter_handle_t* handle) {
    return flash_util_get_chip_id(handle);
}
