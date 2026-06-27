/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_type_4.h"
#include "primitives.h"

#include <Arduino.h>

#include "firestarter.h"
#include "flash_utils.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_pinout.h"

/* Handler: configure_flash4() — protocol 0x05 (FLASH-AMD-STD): 5V page-write flash (EEPROM-like)
 * Phantom dispatch arms 0x35 (FLASH_EEPROM) and 0x39 (FLASH_EEPROM2) are routed here by
 * memory.cpp for forward-compat but have zero DB chips; the host excludes them from
 * KNOWN_PROTOCOLS and routes them to not_implemented before a serial byte is sent.
 *
 * INV-04 — flash4 256B page boundary (data-driven from handle->mem_size or handle->page_size):
 *   FLASH-AMD-STD chips write in fixed-size pages; all bytes in a page must be written
 *   within tBLC (150 µs inter-byte window). The page size varies by capacity:
 *   W29C040 (512K = 524288) → 256B; SST29EE010 (128K = 131072) → 128B;
 *   AT29C256 (32K = 32768) → 64B. Flash4 DB chips span 32KB–512KB.
 *   A fixed 256 would over-run smaller chips' 64-byte page buffers;
 *   the old fixed 64 polled mid-page on W29C040 (original bug).
 *   PGSZ-02 / CR-01: flash4_write_execute now uses handle->page_size (datasheet-sourced,
 *   sent over the wire from the host DB) when non-zero, with flash4_page_size(mem_size)
 *   as the safe heuristic fallback when the field is absent/0.
 *   (Worked examples of heuristic: ≤65536→64, ≤262144→128, else→256.)
 *   Citation: datasheets/0x05-FLASH-AMD-STD/W29C040.pdf §6.2 (256B page);
 *             datasheets/0x05-FLASH-AMD-STD/W29C020.pdf §6.2 (128B page).
 *   Full prose: firestarter/doc/PROTOCOLS.md §1.1 (FLASH-AMD-STD) and §3 INV-04 row. */
static uint32_t flash4_page_size(uint32_t mem_size) {
    if (mem_size <= 65536)  return 64;
    if (mem_size <= 262144) return 128;
    return 256;
}

void flash4_erase_execute(firestarter_handle_t* handle);
void flash4_write_init(firestarter_handle_t* handle);
void flash4_write_execute(firestarter_handle_t* handle);
void flash4_check_chip_id_execute(firestarter_handle_t* handle);
static bool flash4_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected);

uint16_t flash4_get_chip_id(firestarter_handle_t* handle);

void configure_flash4(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_FLASH4);
    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = flash4_write_init;
            handle->firestarter_operation_main = flash4_write_execute;
            break;
        case CMD_ERASE:
            handle->firestarter_operation_main = flash4_erase_execute;
            break;
        case CMD_BLANK_CHECK:
            handle->firestarter_operation_main = mem_util_blank_check;
            break;
        case CMD_CHECK_CHIP_ID:
            handle->firestarter_operation_init = NULL;
            handle->firestarter_operation_main = flash4_check_chip_id_execute;
            break;
    }
}

void flash4_write_init(firestarter_handle_t* handle) {
    if (!is_operation_in_progress(handle)) {
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }

        if (is_flag_set(FLAG_CAN_ERASE)) {
            if (!is_flag_set(FLAG_SKIP_ERASE)) {
                /* FIX-01a / T-93-CANERASE / D-06 defense-in-depth:
                 * Protocol 0x05 (FLASH_AMD_STD) is a 5V-only page-write flash;
                 * it auto-erases per page during the page-write, so no separate
                 * 12V bulk erase is ever needed or safe.
                 * Guard keyed on handle->protocol == 0x05 per D-06 boundary
                 * ("regulator routing keyed on protocol, never on electrical.type
                 * or vpp_mv"). vpp_mv=12000 on the W29C040 is a chip-ID datum,
                 * NOT a program rail — a voltage heuristic would never fire here.
                 * This guard is defense-in-depth: the host already omits
                 * FLAG_CAN_ERASE for algorithm==5, but a stale or hand-crafted
                 * JSON command carrying the flag must also be blocked here. */
                if (handle->protocol != 0x05) {
                    flash4_erase_execute(handle);
                }
            } else {
                LOG_INFO_ID(MSG_INFO_SKIPPING_ERASE);
            }
        }
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

void flash4_write_execute(firestarter_handle_t* handle) {
    /* PGSZ-02 / CR-01: use per-chip datasheet-sourced page_size when supplied by
     * the host (non-zero); fall back to the capacity heuristic when absent (0).
     * handle->page_size == 0 → new wire field not sent → degrade gracefully.
     * For W29C040 (mem_size=524288): heuristic yields 256, datasheet = 256 → identical.
     * For W29C020 (mem_size=262144): heuristic yields 128, datasheet = 128 → identical.
     * A different chip where datasheet != heuristic (e.g. AT29C020 = 256B, heuristic 128B)
     * would benefit from this field once its datasheet is cited in _PAGE_SIZE_BY_PART. */
    uint32_t page_size = handle->page_size ? handle->page_size : flash4_page_size(handle->mem_size);
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint32_t address = handle->address + i;
        uint8_t expected = handle->data_buffer[i];

        /* SDP 3-byte unlock at the start of each page load (AMD/JEDEC SDP).
         * W29C040 ships with Software Data Protection enabled; without this
         * sequence the page-buffer write is silently rejected.
         * Call per-page-START (not per-byte) — calling per-byte would abort
         * the current page load and restart it after each byte. */
        bool is_page_start = (address % page_size) == 0;
        bool is_first_byte = (i == 0);
        if (is_page_start || is_first_byte) {
            flash_execute_command(FLASH_ENABLE_WRITE);
        }

        handle->firestarter_set_data(handle, address, expected);

        bool reached_page_end = ((address + 1) % page_size) == 0;
        bool is_last_byte = i == handle->data_size - 1;
        if (reached_page_end || is_last_byte) {
            if (!flash4_wait_for_page_write(handle, address, expected)) {
                return;
            }
        }
    }
}

static bool flash4_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    // poll the last byte written until it's correct.
    uint8_t observed = 0;
    if (poll_readback(handle, address, expected, 1024, &observed)) {
        return true;
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

void flash4_check_chip_id_execute(firestarter_handle_t* handle) {
    flash_util_check_chip_id_execute(handle);
}

uint16_t flash4_get_chip_id(firestarter_handle_t* handle) {
    return flash_util_get_chip_id(handle);
}

void flash4_erase_execute(firestarter_handle_t* handle) {
    uint32_t address;

    // Intial state:
    address = mem_util_remap_address_bus(handle, 0, READ_FLAG);
    handle->firestarter_set_address(handle, address);
    rurp_chip_disable();
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPE_ENABLE, 0);

    delay(2);

    //^CE -> LOW
    rurp_chip_enable();

    //^OE -> 12v
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPE_ENABLE, 1);

    delay(2);
    //^WE -> LOW
    address = mem_util_remap_address_bus(handle, 0, WRITE_FLAG);
    handle->firestarter_set_address(handle, address);
    delay(20);
    //^WE -> HIGH
    address = mem_util_remap_address_bus(handle, 0, READ_FLAG);
    handle->firestarter_set_address(handle, address);
    delay(2);

    //^CE -> LOW
    rurp_chip_disable();

    //^OE -> 12v
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPE_ENABLE, 0);
}
