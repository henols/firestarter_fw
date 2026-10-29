/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_utils.h"
#include <Arduino.h>
#include "memory_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "logging_id.h"
#include "timeout_utils.h"
#include <stdio.h>
#include "rurp_platform_compat.h"

void fu_flash_flip_data(firestarter_handle_t* handle, uint32_t address, uint8_t data);

const byte_flip_t FLASH_ENABLE_ID[3] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x90},
};
const byte_flip_t FLASH_DISABLE_ID[3] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0xF0},
};
const byte_flip_t FLASH_ERASE[6] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x80},
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x10},
};
const byte_flip_t FLASH_ENABLE_WRITE[3] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0xA0},
};

const byte_flip_t FLASH_ENABLE_WRITE_PROTECTION[3] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0xA0},
};
const byte_flip_t FLASH_DISABLE_WRITE_PROTECTION[6] PROGMEM = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x80},
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x20},
};

// Copies the PROGMEM table to the stack first, so the emit loop below runs
// exactly as it does for a RAM table. A table longer than the buffer sends
// nothing: a cut-off command sequence is worse than none.
void flash_util_byte_flipping_P(firestarter_handle_t* handle, const byte_flip_t* byte_flips_P, size_t size) {
    byte_flip_t byte_flips[FLASH_COMMAND_MAX_LEN];
    if (size > FLASH_COMMAND_MAX_LEN) {
        return;
    }
    memcpy_P(byte_flips, byte_flips_P, size * sizeof(byte_flip_t));
    flash_util_byte_flipping(handle, byte_flips, size);
}

void flash_util_byte_flipping(firestarter_handle_t* handle, const byte_flip_t* byte_flips, size_t size) {

    handle->firestarter_set_control_register(handle, CTRL_READ_WRITE, 0);
    for (size_t i = 0; i < size; i++) {
        fu_flash_flip_data(handle, byte_flips[i].address, byte_flips[i].byte);
    }
    handle->firestarter_set_control_register(handle, CTRL_READ_WRITE, 0);
}

bool flash_util_wait_ready(firestarter_handle_t* handle, uint32_t address, uint8_t expected, uint16_t timeout_ms) {
    const uint32_t started_ms = (uint32_t)millis();
    uint8_t matches = 0;
    do {
        if (((handle->firestarter_get_data(handle, address) ^ expected) & FLASH_DQ7) == 0) {
            if (++matches == 2) {
                return true;
            }
        } else {
            matches = 0;
        }
    } while (!fs_timeout_expired(started_ms, timeout_ms, (uint32_t)millis()));
    return false;
}

// A failed program or erase can leave an AMD-style part outside read mode
// until it sees a reset. AA/55/F0 is that reset, and it is also the SST
// Software ID exit, so one sequence covers both families.
static void flash_util_fail(firestarter_handle_t* handle) {
    handle->response_code = RESPONSE_CODE_ERROR;
    flash_execute_command(FLASH_DISABLE_ID);
}

bool flash_util_wait_program(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    if (flash_util_wait_ready(handle, address, expected, FLASH_PROGRAM_TIMEOUT_MS)) {
        return true;
    }
    LOG_ERROR_ID(MSG_ERR_OP_TIMEOUT);
    flash_util_fail(handle);
    return false;
}

bool flash_util_wait_erase(firestarter_handle_t* handle, uint32_t address) {
    if (!flash_util_wait_ready(handle, address, 0xFF, FLASH_ERASE_TIMEOUT_MS)) {
        LOG_ERROR_ID(MSG_ERR_OP_TIMEOUT);
        flash_util_fail(handle);
        return false;
    }
    // DQ7 only says the algorithm ended. The polled byte must also read
    // erased, or the erase did not reach it (a protected sector, a locked
    // boot block, or a command the part did not accept).
    uint8_t value = handle->firestarter_get_data(handle, address);
    if (value == 0xFF) {
        return true;
    }
    uint8_t _b[4];
    _b[0] = (uint8_t)((address >> 16) & 0xFF);
    _b[1] = (uint8_t)((address >> 8) & 0xFF);
    _b[2] = (uint8_t)(address & 0xFF);
    _b[3] = value;
    LOG_ERROR_ID_BYTES(MSG_ERR_NOT_BLANK, _b, 4);
    flash_util_fail(handle);
    return false;
}

void flash_util_chip_erase(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHIP_ERASE);
    flash_execute_command(FLASH_ERASE);
    delay(FLASH_CHIP_ERASE_MIN_MS);
    flash_util_wait_erase(handle, 0);
}

// Command cycles take the same address path as a data write: the upper
// address lines and the chip's bus remapping both apply. Only the strobe is
// different -- a bare CE pulse, with no settle delay and no pulse width.
void fu_flash_flip_data(firestarter_handle_t* handle, uint32_t address, uint8_t data) {
    rurp_set_data_output();
    handle->firestarter_set_address(handle, mem_util_remap_address_bus(handle, address, WRITE_FLAG));
    rurp_write_data_buffer(data);
    rurp_chip_input();
    rurp_chip_enable();
    rurp_chip_disable();
}

/* Shared AMD/JEDEC chip-ID read: FLASH_ENABLE_ID → read 0x0000/0x0001
 * → FLASH_DISABLE_ID. Used by flash_nor_unlock and flash_5v_page (Option B
 * flash-budget mitigation). */
uint16_t flash_util_get_chip_id(firestarter_handle_t* handle) {
    flash_execute_command(FLASH_ENABLE_ID);
    uint16_t chip_id = handle->firestarter_get_data(handle, 0x0000) << 8;
    chip_id |= handle->firestarter_get_data(handle, 0x0001);
    flash_execute_command(FLASH_DISABLE_ID);
    return chip_id;
}

/* Read an arbitrary AMD/JEDEC ID-mode byte for protection checks.
 * The fixed two-byte chip-ID reader keeps one enter/exit sequence. */
uint8_t flash_util_read_in_id_mode(firestarter_handle_t* handle, uint32_t address) {
    flash_execute_command(FLASH_ENABLE_ID);
    uint8_t data = handle->firestarter_get_data(handle, address);
    flash_execute_command(FLASH_DISABLE_ID);
    return data;
}

void flash_util_check_chip_id_execute(firestarter_handle_t* handle) {
    uint16_t chip_id = flash_util_get_chip_id(handle);
    mem_util_report_chip_id(handle, chip_id, is_flag_set(FLAG_FORCE));
}

void flash_util_check_chip_id_init(firestarter_handle_t* handle) {
    if (handle->chip_id > 0) {
        flash_util_check_chip_id_execute(handle);
    }
}
