/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Handler: configure_flash3() — protocol 0x06 (FLASH-AMD-ALT): AMD/SST unlock-sequence NOR flash
 *
 * Write algorithm: 3-cycle software unlock before each byte program:
 *   0xAA→0x5555, 0x55→0x2AAA, 0xA0→0x5555, then data byte to target address.
 *   The internal program state machine completes in ~10–20 µs/byte. DQ7 data polling
 *   confirms completion (DQ7 matches written bit = done; DQ5 high = timeout).
 *   Sector erase: 6-cycle sequence ending with 0x30→sector address.
 *   Citation: datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf p.7 §Byte-Program Operation;
 *             p.8 §Chip-Erase Operation.
 *
 * INV-09 — SST39SF040 keep-Flash/EEPROM classification:
 *   The dominant 0x06 chip (SST39SF040, 190 DB entries) is a NOR flash with sector erase
 *   and electrical reprogram — it is correctly classified as electrical.type "Flash/EEPROM"
 *   (FLAG_CAN_ERASE set, auto-erase before write active). This classification must NOT be
 *   confused with the UV-EPROM path (no erase, no FLAG_CAN_ERASE). The Phase-86 variant
 *   decode preserves this by routing 0x06 chips exclusively to configure_flash3(); the
 *   BLOCKER-2 invariant ensures they never reach configure_eprom() (which would enable
 *   the VPP regulator on a 5V-only part).
 *   Citation: datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf p.4 §DC Characteristics (Vcc=5V only).
 *   Full prose: firestarter/doc/PROTOCOLS.md §1.2 (FLASH-AMD-ALT) and §3 INV-09 row.
 */

#include "flash_type_3.h"

#include <Arduino.h>
#include "memory_utils.h"
#include "flash_utils.h"
#include "firestarter.h"
#include "logging_id.h"
#include "operation_utils.h"

void flash3_erase_execute(firestarter_handle_t* handle);
void flash3_sector_erase(firestarter_handle_t* handle, uint32_t sector_address);
void flash3_write_init(firestarter_handle_t* handle);
void flash3_write_execute(firestarter_handle_t* handle);
void flash3_check_chip_id_execute(firestarter_handle_t* handle);

uint16_t flash3_get_chip_id(firestarter_handle_t* handle);

void flash3_generic_init(firestarter_handle_t* handle);

// Worst-case erase time for older flash chips can be up to 100ms.
// Add a 5ms buffer for safety.
const int FLASH_ERASE_DELAY_MS = 105;

void configure_flash3(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_FLASH);
    handle->firestarter_operation_init = flash3_generic_init;
    switch (handle->cmd) {
    case CMD_WRITE:
        handle->firestarter_operation_init = flash3_write_init;
        handle->firestarter_operation_main = flash3_write_execute;
        break;
    case CMD_ERASE:
        handle->firestarter_operation_main = flash3_erase_execute;
        // handle->firestarter_operation_end = memory_blank_check;
        break;
    case CMD_BLANK_CHECK:
        handle->firestarter_operation_main = mem_util_blank_check;
        break;
    case CMD_CHECK_CHIP_ID:
        handle->firestarter_operation_init = NULL;
        handle->firestarter_operation_main = flash3_check_chip_id_execute;
        break;
    }
}

void flash3_generic_init(firestarter_handle_t* handle) {
    if (handle->chip_id > 0) {
        flash3_check_chip_id_execute(handle);
    }
}


void flash3_write_init(firestarter_handle_t* handle) {
    // Gate one-time init (chip-ID + erase + erase-settle delay) behind
    // is_operation_in_progress so it runs exactly ONCE per write command.
    // Without this guard the INIT-phase state machine re-invokes
    // flash3_write_init for every 2KB chunk of the stateful blank-check
    // (mem_util_blank_check progresses 2KB per call). For a 512KB chip
    // that is 256 re-runs of: chip-ID check + chip-erase command + 105ms
    // settle delay. Each chip-erase command starts an internal erase that
    // takes ~100ms; sending another erase command before the chip
    // completes the previous one leaves it in an undefined state. The
    // accumulated 27+ seconds of pointless settle delay also stalls INIT
    // dramatically. Matches the flash4_write_init pattern.
    if (!is_operation_in_progress(handle)) {
        if (handle->chip_id > 0) {
            flash3_check_chip_id_execute(handle);
            if (handle->response_code == RESPONSE_CODE_ERROR) {
                return;
            }
        }

        if (is_flag_set(FLAG_CAN_ERASE)) {
            if (!is_flag_set(FLAG_SKIP_ERASE)) {
                flash3_erase_execute(handle);
                delay(FLASH_ERASE_DELAY_MS);
            }
            else {
                LOG_DEBUG_ID_SUB(DBG_SKIPPING_ERASE_MEMORY);
                LOG_INFO_ID(MSG_INFO_SKIPPING_ERASE_MEM);
            }
        }
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

void flash3_write_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        flash_execute_command(FLASH_ENABLE_WRITE);
        handle->firestarter_set_data(handle, handle->address + i, handle->data_buffer[i]);

        flash_util_verify_operation(handle, handle->data_buffer[i]);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
    }
}

void flash3_erase_execute(firestarter_handle_t* handle) {
    if (handle->address != 0) {
        LOG_DEBUG_ID_SUB(DBG_SECTOR_ERASE);
        flash3_sector_erase(handle, handle->address);
    } else {
        LOG_DEBUG_ID_SUB(DBG_CHIP_ERASE);
        flash_execute_command(FLASH_ERASE);
    }
}

void flash3_sector_erase(firestarter_handle_t* handle, uint32_t sector_address) {
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


void flash3_check_chip_id_execute(firestarter_handle_t* handle) {
    flash_util_check_chip_id_execute(handle);
}

uint16_t flash3_get_chip_id(firestarter_handle_t* handle) {
    return flash_util_get_chip_id(handle);
}
