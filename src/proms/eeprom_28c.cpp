/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Handler: configure_eeprom28c() — protocol 0x0D (EEPROM-POLL): 5V parallel EEPROM, SDP + DQ7 page poll
 *
 * Write algorithm: page write (64B page) with SDP (Software Data Protection) permanently disabled
 * before programming via a 6-cycle unlock sequence, then bytes within tBLC (150 µs inter-byte
 * window). After the last byte the chip's internal write cycle fires (~5–10 ms). DQ7 data
 * polling confirms completion: read the last-written address; when bit 7 matches the written
 * value, the cycle is done. DQ6 toggle is an alternative: alternates on each read during write.
 *   Citation: datasheets/0x0D-EEPROM-POLL/AT28C256.pdf p.6 §Software Data Protection;
 *             p.7 §Page Write; p.8 §Data Polling (DQ7).
 *
 * SDP-disable rationale: permanently disabling SDP before every write avoids having to
 * implement SDP-enable/disable state tracking in the firmware. The trade-off (SDP protection
 * lost for the session) is acceptable given the programming-only use case.
 *
 * VPP: None required (5V internal charge pump). The RURP VPP regulator is NOT enabled for
 * this bucket — 0x0D chips are 5V-only parallel EEPROMs.
 *   Citation: datasheets/0x0D-EEPROM-POLL/AT28C256.pdf p.4 §DC Characteristics (Vcc=5V).
 *
 * Full prose: firestarter/doc/PROTOCOLS.md §1.6 (EEPROM-POLL).
 */

#include "eeprom_28c.h"
#include "primitives.h"

#include <Arduino.h>

#include "firestarter.h"
#include "flash_utils.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_pinout.h"

#define PAGE_SIZE 64

void eeprom28c_write_init(firestarter_handle_t* handle);
void eeprom28c_write_execute(firestarter_handle_t* handle);
static bool eeprom28c_wait_for_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected);

void configure_eeprom28c(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_EEPROM_28C);
    // AT28C page write timing requires fast consecutive writes; no pulse delay needed
    handle->pulse_delay = 0;
    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = eeprom28c_write_init;
            handle->firestarter_operation_main = eeprom28c_write_execute;
            break;
        case CMD_BLANK_CHECK:
            handle->firestarter_operation_main = mem_util_blank_check;
            break;
    }
}

// A9-12V chip-identification check for AT28C EEPROM family (SAF-05).
// Mirrors eprom_get_chip_id (eprom.cpp:186-197) for the read mechanism and
// flash_intel_check_chip_id (flash_intel.cpp:146-155) for compare + response.
// Read addresses are derived from mem_size: AT28C256 = 0x7FC0/0x7FC1,
// AT28C64 = 0x1FC0/0x1FC1, etc. Caller-visible via response_code only;
// no declaration in eeprom_28c.h (static — internal linkage only).
static void eeprom28c_check_chip_id(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID_28C);
    // Underflow guard: mem_size < 64 would wrap mfr_addr to ~0xFFFFFFC0 and drive
    // 12V on A9 of an arbitrary address. Canonical DB entries are >= 2 KiB, but
    // hand-crafted JSON could reach here. Treat as configuration error.
    if (handle->mem_size < 64) {
        if (is_flag_set(FLAG_FORCE)) {
            LOG_WARN_ID_U32(MSG_WARN_MEM_SIZE_TOO_SMALL, (uint32_t)handle->mem_size);
            handle->response_code = RESPONSE_CODE_WARNING;
        } else {
            LOG_ERROR_ID_U32(MSG_ERR_MEM_SIZE_TOO_SMALL, (uint32_t)handle->mem_size);
            handle->response_code = RESPONSE_CODE_ERROR;
        }
        return;
    }
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    delay(50);
    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE, 1);
    delay(100);
    uint32_t mfr_addr = handle->mem_size - 64;  // 0x7FC0 (AT28C256) / 0x1FC0 (AT28C64) / ...
    uint16_t chip_id = handle->firestarter_get_data(handle, mfr_addr) << 8;
    chip_id |= handle->firestarter_get_data(handle, mfr_addr + 1);
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_A9_ENABLE, 0);
    chip_id_report(handle, chip_id);
}

void eeprom28c_write_init(firestarter_handle_t* handle) {
    // Check chip identity via A9-12V (SAF-05) BEFORE SDP-disable (D-08: fail-fast
    // on identity leaves the chip write-protected on mismatch).
    if (handle->chip_id > 0) {
        eeprom28c_check_chip_id(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
    }
    // Disable SDP (Software Data Protection) before writing.
    // The 6-write sequence must complete within the inter-byte timing window.
    // flash_util_byte_flipping uses fu_flash_flip_data which has no pulse_delay.
    flash_execute_command(FLASH_DISABLE_WRITE_PROTECTION);
    // Wait for SDP disable internal write cycle to complete
    if (!eeprom28c_wait_for_write(handle, 0x5555, 0x20)) {
        return;
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

void eeprom28c_write_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint32_t address = handle->address + i;
        uint8_t data = handle->data_buffer[i];
        handle->firestarter_set_data(handle, address, data);

        bool page_end = ((address + 1) % PAGE_SIZE) == 0;
        bool last_byte = (i == handle->data_size - 1);
        if (page_end || last_byte) {
            if (!eeprom28c_wait_for_write(handle, address, data)) {
                return;
            }
        }
    }
}

static bool eeprom28c_wait_for_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    uint8_t observed = 0;
    for (uint16_t j = 0; j < 2000; j++) {
        delayMicroseconds(10);
        observed = handle->firestarter_get_data(handle, address);
        if (observed == expected) {
            return true;
        }
    }
    {
        uint8_t _b[5];
        _b[0] = (uint8_t)((address >> 16) & 0xFF);
        _b[1] = (uint8_t)((address >> 8) & 0xFF);
        _b[2] = (uint8_t)(address & 0xFF);
        _b[3] = (uint8_t)expected;
        _b[4] = (uint8_t)observed;
        LOG_ERROR_ID_BYTES(MSG_ERR_EEPROM_TIMEOUT, _b, 5);
    }
    handle->response_code = RESPONSE_CODE_ERROR;
    return false;
}
