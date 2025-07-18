/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_type_2.h"

#include <Arduino.h>
#include "memory_utils.h"
#include "flash_utils.h"
#include "firestarter.h"
#include "logging.h"

typedef struct chip_info {
    union chip {
        uint16_t id;
        struct {
            uint8_t device;
            uint8_t manufacturer;
        };
    } chip;

    uint8_t high_boot_lockout;
    uint8_t low_boot_lockout;
} chip_info_t;


void flash2_generic_init(firestarter_handle_t* handle);
void flash2_erase_execute(firestarter_handle_t* handle);
void flash2_write_init(firestarter_handle_t* handle);
void flash2_write_end(firestarter_handle_t* handle);
void flash2_check_chip_id_execute(firestarter_handle_t* handle);

void flash2_set_data(firestarter_handle_t* handle, uint32_t address, uint8_t data);

void f2_get_chip_id(firestarter_handle_t* handle, chip_info_t* chip_info);
void f2_internal_erase(firestarter_handle_t* handle);

void f2_disable_protection(firestarter_handle_t* handle);
void f2_enable_protection(firestarter_handle_t* handle);

void (*f2_set_data)(struct firestarter_handle*, uint32_t, uint8_t);

void configure_flash2(firestarter_handle_t* handle) {
    debug("Configuring Flash type 2");
    handle->firestarter_operation_init = flash2_generic_init;
    switch (handle->cmd) {
    case CMD_WRITE:
        handle->firestarter_operation_init = flash2_write_init;
        // handle->firestarter_operation_main = flash2_write_execute;
        handle->firestarter_operation_end = flash2_write_end;
        break;
    case CMD_ERASE:
        handle->firestarter_operation_main = flash2_erase_execute;
        handle->firestarter_operation_end = mem_util_blank_check;
        break;
    case CMD_BLANK_CHECK:
        handle->firestarter_operation_main = mem_util_blank_check;
        break;
    case CMD_CHECK_CHIP_ID:
        handle->firestarter_operation_init = NULL;
        handle->firestarter_operation_main = flash2_check_chip_id_execute;
        break;
    }

    f2_set_data = handle->firestarter_set_data;
    handle->firestarter_set_data = flash2_set_data;
    handle->pulse_delay = 0;
}

void flash2_generic_init(firestarter_handle_t* handle) {
    if (handle->chip_id > 0) {
        flash2_check_chip_id_execute(handle);
    } // If chip_id is 0 or negative, it's intentionally ignored.
}

void flash2_write_init(firestarter_handle_t* handle) {
    flash2_generic_init(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        return;
    }

    f2_disable_protection(handle);

    if (is_flag_set(FLAG_CAN_ERASE)) {
        if (!is_flag_set(FLAG_SKIP_ERASE)) {
            f2_internal_erase(handle);
            delay(105); //Old chips, worst case assumed to 5% longer to erase 
            // verify operation?
        }
        else {
            debug("Skipping erase of memory");
            copy_to_buffer(handle->response_msg, "Skipping erase of memory");
        }
    }
#ifdef EPROM_BLANK_CHECK
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        m_util_blank_check(handle);
    }
#endif
}

void flash2_write_end(firestarter_handle_t* handle) {
    f2_enable_protection(handle);
}

void flash2_set_data(firestarter_handle_t* handle, uint32_t address, uint8_t data) {
    f2_set_data(handle, address, data);
    flash_util_verify_operation(handle, data);
}

void flash2_erase_execute(firestarter_handle_t* handle) {
    f2_disable_protection(handle);
    f2_internal_erase(handle);
    f2_enable_protection(handle);
}


void flash2_check_chip_id_execute(firestarter_handle_t* handle) {
    chip_info_t chip_info;
    f2_get_chip_id(handle, &chip_info);
    if (chip_info.chip.id != handle->chip_id) {
        handle->response_code = is_flag_set(FLAG_FORCE) ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR;
        format(handle->response_msg, "Chip ID %#x dont match expected ID %#x", chip_info.chip.id, handle->chip_id);
    }
    else {
        format(handle->response_msg, "Boot block lockout, low: %d, high: %d", chip_info.low_boot_lockout & 0x01, chip_info.high_boot_lockout & 0x01);
    }
}

void f2_disable_protection(firestarter_handle_t* handle) {
    flash_execute_command(FLASH_DISABLE_WRITE_PROTECTION);
    // flash_byte_flipping(handle, flash_disable_write_protection, sizeof(flash_disable_write_protection) / sizeof(flash_disable_write_protection[0]));
    delayMicroseconds(1);
}

void f2_enable_protection(firestarter_handle_t* handle) {
    flash_execute_command(FLASH_ENABLE_WRITE_PROTECTION);
}

void f2_internal_erase(firestarter_handle_t* handle) {
    flash_execute_command(FLASH_ERASE);
    delay(1); // Add a small delay to allow erase to complete
    flash_util_verify_operation(handle, 0xFF);
}

void f2_get_chip_id(firestarter_handle_t* handle, chip_info_t* chip_info) {
    flash_execute_command(FLASH_ENABLE_ID);

    chip_info->chip.device = handle->firestarter_get_data(handle, 0x0001);
    chip_info->chip.manufacturer = handle->firestarter_get_data(handle, 0x0000);
    chip_info->low_boot_lockout = handle->firestarter_get_data(handle, 0x0002);
    chip_info->high_boot_lockout = handle->firestarter_get_data(handle, handle->mem_size - 0x0e);
    // handle->firestarter_set_address(handle, 0x0000);
    flash_execute_command(FLASH_DISABLE_ID);
}