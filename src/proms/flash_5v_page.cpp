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
#include "rurp_pinout.h"

#define FLASH_5V_PAGE_SIZE_MAX 512

// Data-polling bit. During an internal page write a read of the LAST BYTE
// WRITTEN returns the complement of that byte's DQ7; on completion DQ7
// matches. Compare only this bit, never the whole byte.
#define FLASH_5V_PAGE_DQ7_MASK 0x80

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
        // Unlike flash_nor_unlock.cpp, this file assigns no
        // firestarter_operation_init before the switch, so this arm has
        // nothing to null. The 0x01 force ctrl flag is deliberately not read
        // on this path: elsewhere that bit means "downgrade a chip-ID
        // mismatch to a warning", this read performs no chip-ID check, and
        // honouring the bit here would give one flag two unrelated meanings.
        case CMD_LOCK_STATUS:
            handle->firestarter_operation_main = flash_5v_page_read_protection_execute;
            break;
    }
}

// Nothing to prepare: the chip erases each page during the write, so there is
// no erase step, and the host owns the refusal to write a non-blank part. The
// init hook stays so the write has the same init/main/end shape as the other
// protocols.
void flash_5v_page_write_init(firestarter_handle_t* handle) {
    (void)handle;
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

static bool flash_5v_page_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    // Poll the last byte written until DQ7 matches. The match must hold on two
    // consecutive reads, so one transient sample cannot end the poll early. The
    // data itself is not compared: this answers "is the cycle done".
    uint8_t observed = 0;
    for (uint16_t j = 0; j < 1024; j++) {
        delayMicroseconds(10);
        observed = handle->firestarter_get_data(handle, address);
        if ((observed & FLASH_5V_PAGE_DQ7_MASK) == (expected & FLASH_5V_PAGE_DQ7_MASK)) {
            uint8_t confirm = handle->firestarter_get_data(handle, address);
            if ((confirm & FLASH_5V_PAGE_DQ7_MASK) == (expected & FLASH_5V_PAGE_DQ7_MASK)) {
                return true;
            }
            observed = confirm;
        }
    }

    {
        uint8_t _b[5];
        _b[0] = (uint8_t)expected;
        _b[1] = (uint8_t)((address >> 16) & 0xFF);
        _b[2] = (uint8_t)((address >> 8) & 0xFF);
        _b[3] = (uint8_t)(address & 0xFF);
        _b[4] = (uint8_t)observed;
        LOG_ERROR_ID_BYTES(MSG_ERR_FL4_VERIFY_TIMEOUT, _b, 5);
        handle->response_code = RESPONSE_CODE_ERROR;
    }
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
        // Unrecognised raw value: an observation the host must classify, not
        // an error this firmware can adjudicate. 0xFF is the sentinel, as in
        // hw_get_version. The DATA frame is emitted either way so the raw byte
        // still reaches the host -- never coerce this into 0x00 or 0x01.
        _b[1] = 0xFF;
        handle->response_code = RESPONSE_CODE_WARNING;
    }
    LOG_DATA_ID_BYTES(MSG_DATA_PROTECTION_STATUS, _b, 2);
}
