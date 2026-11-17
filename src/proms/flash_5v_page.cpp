/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_5v_page.h"

#include <Arduino.h>

#include "firestarter.h"
#include "flash_utils.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_pinout.h"

#define FLASH_5V_PAGE_SIZE_MAX 512

// The page write cycle is 10 ms at most on the 0x05 parts (AT29C, W29C,
// SST29EE). The bound leaves room for that and for the polling reads.
#define FLASH_5V_PAGE_WRITE_TIMEOUT_MS 50U

// The internal write starts only when the byte-load window (tBLC, 200 us at
// most on these parts) closes with no new byte. Before that a read can return
// the old array byte, whose DQ7 may already equal the new one, so the poll
// starts after the window.
#define FLASH_5V_PAGE_TBLC_US 300U

static bool flash_5v_page_mask(uint16_t requested, uint32_t* out_mask) {
    if (requested == 0) {
        return false;
    }
    if (requested <= FLASH_5V_PAGE_SIZE_MAX && (requested & (requested - 1)) == 0) {
        *out_mask = (uint32_t)requested - 1;
        return true;
    }
    return false;
}

void flash_5v_page_write_init(firestarter_handle_t* handle);
void flash_5v_page_write_execute(firestarter_handle_t* handle);
void flash_5v_page_check_chip_id_execute(firestarter_handle_t* handle);
static bool flash_5v_page_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected);

uint16_t flash_5v_page_get_chip_id(firestarter_handle_t* handle);

void configure_flash_5v_page(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_FLASH4);
    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = flash_5v_page_write_init;
            handle->firestarter_operation_main = flash_5v_page_write_execute;
            break;
        case CMD_CHECK_CHIP_ID:
            handle->firestarter_operation_init = NULL;
            handle->firestarter_operation_main = flash_5v_page_check_chip_id_execute;
            break;
        // Software chip erase (#62): the W29C020C "Software Chip Erase"
        // command, which the AT29C and SST29EE parts share, at the same
        // 5555/2AAA unlock addresses as this protocol's page writes. Address 0
        // must read 0xFF afterwards: a part whose boot-block lockout is set
        // ignores the chip erase, and that check catches it when the locked
        // block is at address 0.
        case CMD_ERASE:
            handle->firestarter_operation_init = flash_util_check_chip_id_init;
            handle->firestarter_operation_main = flash_util_chip_erase;
            break;
        // A second query arm, modelled on CMD_CHECK_CHIP_ID just above.
        // Unlike flash_nor_unlock.cpp, this
        // file assigns no firestarter_operation_init before the switch, so
        // this arm does not null it -- there is nothing to null. The 0x01
        // force ctrl flag is deliberately not read on this path, for the
        // same reason flash_nor_unlock.cpp's CMD_LOCK_STATUS arm states:
        // elsewhere that bit means "downgrade a chip-ID mismatch to a
        // warning", this read performs no chip-ID check, and honouring the
        // bit here would give one flag two unrelated meanings.
        case CMD_LOCK_STATUS:
            handle->firestarter_operation_main = flash_5v_page_read_protection_execute;
            break;
    }
}

void flash_5v_page_write_init(firestarter_handle_t* handle) {
    if (!is_operation_in_progress(handle)) {
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
    }
    // Flash4 auto-erases each page, so requiring a blank chip before a
    // write would reject valid operations.
}

void flash_5v_page_write_execute(firestarter_handle_t* handle) {
    uint32_t page_mask;
    if (!flash_5v_page_mask(handle->page_size, &page_mask)) {
        LOG_ERROR_ID_U16(MSG_ERR_FL4_PAGE_SIZE, handle->page_size);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
    if ((handle->address & page_mask) != 0 || (handle->data_size & page_mask) != 0) {
        uint8_t _b[5];
        _b[0] = (uint8_t)((handle->address >> 16) & 0xFF);
        _b[1] = (uint8_t)((handle->address >> 8) & 0xFF);
        _b[2] = (uint8_t)(handle->address & 0xFF);
        _b[3] = (uint8_t)((handle->data_size >> 8) & 0xFF);
        _b[4] = (uint8_t)(handle->data_size & 0xFF);
        LOG_ERROR_ID_BYTES(MSG_ERR_FL4_PAGE_ALIGN, _b, 5);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint32_t address = handle->address + i;
        uint8_t expected = handle->data_buffer[i];

        /* SDP 3-byte unlock at the start of each page load (AMD/JEDEC SDP).
         * W29C040 ships with Software Data Protection enabled; without this
         * sequence the page-buffer write is silently rejected.
         * Call per-page-START (not per-byte) — calling per-byte would abort
         * the current page load and restart it after each byte. */
        bool is_page_start = (address & page_mask) == 0;
        bool is_first_byte = (i == 0);
        if (is_page_start || is_first_byte) {
            flash_execute_command(FLASH_ENABLE_WRITE);
        }

        handle->firestarter_set_data(handle, address, expected);

        bool reached_page_end = ((address + 1) & page_mask) == 0;
        bool is_last_byte = i == handle->data_size - 1;
        if (reached_page_end || is_last_byte) {
            if (!flash_5v_page_wait_for_page_write(handle, address, expected)) {
                return;
            }
        }
    }
}

// Waits for the page write, by DQ7 polling the last byte loaded. Only DQ7
// says the cycle is done; the other outputs may still be invalid when it
// flips, so the byte itself is not compared -- the host verify does that.
static bool flash_5v_page_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    delayMicroseconds(FLASH_5V_PAGE_TBLC_US);
    if (flash_util_wait_ready(handle, address, expected, FLASH_5V_PAGE_WRITE_TIMEOUT_MS)) {
        return true;
    }
    uint8_t _b[5];
    _b[0] = (uint8_t)expected;
    _b[1] = (uint8_t)((address >> 16) & 0xFF);
    _b[2] = (uint8_t)((address >> 8) & 0xFF);
    _b[3] = (uint8_t)(address & 0xFF);
    _b[4] = handle->firestarter_get_data(handle, address);
    LOG_ERROR_ID_BYTES(MSG_ERR_FL4_VERIFY_TIMEOUT, _b, 5);
    handle->response_code = RESPONSE_CODE_ERROR;
    return false;
}

void flash_5v_page_check_chip_id_execute(firestarter_handle_t* handle) {
    flash_util_check_chip_id_execute(handle);
}

// CMD_LOCK_STATUS for the 0x05 Winbond Product-ID boot-block family. Reads the
// boot-block status byte through the shared AMD/JEDEC ID-mode helper and
// reports the raw byte plus a firmware decode.
//
// A 5 V read: flash_util_read_in_id_mode only enters and exits ID mode, so no
// VPP/VPE control-register bit is written on this path.
//
// Deliberately does NOT emit MSG_WARN_FL4_BOOT_BLOCK_LOCKED on the
// reads-as-locked branch. That id's format string says "not programmable ...
// write forced" -- worded for a write-path failure, not a status read that
// wrote nothing. The DATA frame below already carries the observation.
void flash_5v_page_read_protection_execute(firestarter_handle_t* handle) {
    uint8_t raw = flash_util_read_in_id_mode(handle, FLASH_5V_PAGE_BOOT_BLOCK_STATUS_ADDR);
    uint8_t _b[2];
    _b[0] = raw;
    if (raw == FLASH_5V_PAGE_BOOT_BLOCK_UNLOCKED) {
        _b[1] = 0x00;
        handle->response_code = RESPONSE_CODE_OK;
    } else if (raw == FLASH_5V_PAGE_BOOT_BLOCK_LOCKED) {
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

uint16_t flash_5v_page_get_chip_id(firestarter_handle_t* handle) {
    return flash_util_get_chip_id(handle);
}
