/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* shared flash helper utilities — used by configure_flash3(), configure_flash4(),
 * and configure_flash_intel() (protocols 0x06, 0x05, 0x10 respectively).
 *
 * This file holds the primitives that are common across multiple flash families:
 *   - Flash SDP (Software Data Protection) unlock/disable bus sequences shared
 *     between the 0x05 FLASH-AMD-STD and 0x0D EEPROM-POLL paths.
 *   - DQ7 / toggle-bit poll loop (data polling completion detection) shared by
 *     0x05 and 0x06 families.
 *   - Flash erase utility routines invoked from flash3_erase_execute() (0x06)
 *     and flash_intel_* (0x10) erase paths.
 *
 * No VPP regulator control lives here — VPP decisions are entirely within each
 * family's own configure_*() handler (see flash_intel.cpp for 12V mandatory VPP,
 * and sram.cpp / eprom.cpp for the BLOCKER-2 VPP isolation rationale).
 *
 * datasheets references: datasheets/0x05-FLASH-AMD-STD/W29C040.pdf (DQ7 poll timing);
 *                        datasheets/0x06-FLASH-AMD-ALT/SST39SF040.pdf (SDP unlock cycles);
 *                        datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf (SR poll loop).
 * Full protocol prose: firestarter/doc/PROTOCOLS.md §1.1, §1.2, §1.8.
 */

#include "flash_utils.h"
#include "primitives.h"
#include <Arduino.h>
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "logging_id.h"
#include <stdio.h>


void fu_flash_flip_data(firestarter_handle_t* handle, uint32_t address, uint8_t data);
void fu_flash_fast_address(firestarter_handle_t* handle, uint32_t address);
uint8_t fu_flash_data_poll();

void flash_util_byte_flipping(firestarter_handle_t* handle, const byte_flip_t* byte_flips, size_t size) {

    handle->firestarter_set_control_register(handle, CTRL_READ_WRITE, 0);
    for (size_t i = 0; i < size; i++) {
        fu_flash_flip_data(handle, byte_flips[i].address, byte_flips[i].byte);
    }
    handle->firestarter_set_control_register(handle, CTRL_READ_WRITE, 0);
}

void flash_util_verify_operation(firestarter_handle_t* handle, uint8_t expected_data) {

    handle->firestarter_set_control_register(handle, CTRL_READ_WRITE, 1);

    unsigned long timeout = millis() + 150;
    while (millis() < timeout) {
        // Data Polling: Read from the address and check DQ7.
        // When the write is complete, the data read back will have the same DQ7 as the data written.
        if ((fu_flash_data_poll() & 0x80) == (expected_data & 0x80)) {
            // Some datasheets recommend reading a second time to confirm the write has completed.
            if ((fu_flash_data_poll() & 0x80) == (expected_data & 0x80)) {
                rurp_set_data_output();
                rurp_chip_disable();
                rurp_chip_input();
                return;  // Operation completed successfully
            }
        }
    }
    LOG_ERROR_ID(MSG_ERR_OP_TIMEOUT);
    handle->response_code = RESPONSE_CODE_ERROR;
    return;
}

void fu_flash_flip_data(firestarter_handle_t* handle, uint32_t address, uint8_t data) {
    rurp_set_data_output();
    fu_flash_fast_address(handle, address);
    rurp_write_data_buffer(data);
    rurp_chip_input();
    rurp_chip_enable();
    rurp_chip_disable();
}

void fu_flash_fast_address(firestarter_handle_t* handle, uint32_t address) {
    uint8_t lsb = address & 0xFF;
    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, lsb);
    uint8_t msb = ((address >> 8) & 0xFF);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, msb);
}

uint8_t fu_flash_data_poll() {
    rurp_set_data_input();
    rurp_chip_enable();
    rurp_chip_output();
    uint8_t data = rurp_read_data_buffer();
    rurp_chip_disable();
    rurp_chip_input();
    return data;
}

/* Shared AMD/JEDEC chip-ID read: FLASH_ENABLE_ID → read 0x0000/0x0001
 * → FLASH_DISABLE_ID. Used by flash3 and flash4 (Option B flash-budget
 * mitigation — Phase 74 Plan 02). */
uint16_t flash_util_get_chip_id(firestarter_handle_t* handle) {
    flash_execute_command(FLASH_ENABLE_ID);
    uint16_t chip_id = handle->firestarter_get_data(handle, 0x0000) << 8;
    chip_id |= handle->firestarter_get_data(handle, 0x0001);
    flash_execute_command(FLASH_DISABLE_ID);
    return chip_id;
}

void flash_util_check_chip_id_execute(firestarter_handle_t* handle) {
    chip_id_report(handle, flash_util_get_chip_id(handle), is_flag_set(FLAG_FORCE));
}
