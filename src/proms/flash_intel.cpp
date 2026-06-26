/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Handler: configure_flash_intel() — protocol 0x10 (FLASH-INTEL): Intel 28F command-register NOR flash
 *
 * Write algorithm: command-register architecture (no address-based unlock sequence).
 *   Byte program: write 0x40 (Setup Program) to any address, write data byte to target
 *   address PA, wait ~10 µs, write 0xC0 (Program Verify) to any address, read PA, compare.
 *   On mismatch: write 0x00 (Reset), retry up to 25 times per byte.
 *   Unlike AMD flash there is no 0x5555/0x2AAA address unlock; commands go directly to the
 *   command register.
 *   Citation: datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf p.9 §Quick-Pulse Programming Algorithm.
 *
 * Erase model: bulk chip erase only (no sector erase on original 28F010).
 *   Pre-condition: all bytes must be pre-programmed to 0x00 before erase.
 *   Erase: write 0x20 (Erase Setup) twice to any address, wait ~12 ms, write 0xA0 (Erase Verify),
 *   read all bytes (expect 0xFF). On failure: write 0x00, retry up to 1000 iterations.
 *   Citation: datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf p.10 §Quick-Erase Algorithm.
 *
 * VPP: 12V MANDATORY via CTRL_VPP_P1_ENABLE (0x08) + CTRL_VPP_REGULATOR_ENABLE.
 *   VPP ≤ 6.5V inhibits ALL program and erase operations (hardware write-protect).
 *   VPP must remain enabled for the entire program/erase cycle; it must NOT be released
 *   mid-cycle. This is distinct from EPROM-family VPP: 28F010 VPP is a mandatory supply
 *   rail, not a programming-pulse voltage.
 *   Citation: datasheets/0x10-FLASH-INTEL/Intel-28F010.pdf p.6 §VPP Characteristics (Vpp<6.5V=write-inhibit).
 *
 * Full prose: firestarter/doc/PROTOCOLS.md §1.8 (FLASH-INTEL).
 */

#include "flash_intel.h"
#include "primitives.h"

#include <Arduino.h>

#include "firestarter.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"

void flash_intel_write_init(firestarter_handle_t* handle);
void flash_intel_write_execute(firestarter_handle_t* handle);
void flash_intel_erase_execute(firestarter_handle_t* handle);
void flash_intel_cleanup(firestarter_handle_t* handle);
void flash_intel_check_chip_id(firestarter_handle_t* handle);
static bool flash_intel_poll_sr(firestarter_handle_t* handle, uint16_t timeout_ms);

static void flash_intel_check_vpp(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_VPP_INTEL);
#ifdef HARDWARE_REVISION
    if (rurp_get_hardware_revision() == REVISION_0) {
        LOG_WARN_ID(MSG_WARN_REV0_VPP_UNSUPPORTED);
        handle->response_code = RESPONSE_CODE_WARNING;
        return;
    }
#endif
    // Caller (flash_intel_write_init) already asserted CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE
    // and delayed 500ms; do not toggle the regulator here.
    uint16_t vpp_mv = rurp_read_voltage_mv();
    LOG_DEBUG_ID_SUB_U16(DBG_CHECKING_VPP_VOLTAGE, vpp_mv);
    if (vpp_mv > (uint32_t)handle->vpp_mv + 500) {
        {
            uint16_t _v0 = (uint16_t)((vpp_mv + 50) / 1000);
            uint16_t _v1 = (uint16_t)((((vpp_mv + 50) / 100) % 10));
            uint16_t _v2 = (uint16_t)((handle->vpp_mv + 50) / 1000);
            uint16_t _v3 = (uint16_t)((((handle->vpp_mv + 50) / 100) % 10));
            uint8_t _b[8];
            _b[0] = (uint8_t)((_v0 >> 8) & 0xFF);
            _b[1] = (uint8_t)(_v0 & 0xFF);
            _b[2] = (uint8_t)((_v1 >> 8) & 0xFF);
            _b[3] = (uint8_t)(_v1 & 0xFF);
            _b[4] = (uint8_t)((_v2 >> 8) & 0xFF);
            _b[5] = (uint8_t)(_v2 & 0xFF);
            _b[6] = (uint8_t)((_v3 >> 8) & 0xFF);
            _b[7] = (uint8_t)(_v3 & 0xFF);
            if (is_flag_set(FLAG_FORCE)) {
                LOG_WARN_ID_BYTES(MSG_WARN_VPP_HIGH, _b, 8);
                handle->response_code = RESPONSE_CODE_WARNING;
            } else {
                LOG_ERROR_ID_BYTES(MSG_ERR_VPP_HIGH, _b, 8);
                handle->response_code = RESPONSE_CODE_ERROR;
            }
        }
    } else if (vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100) {
        {
            uint16_t _v0 = (uint16_t)((vpp_mv + 50) / 1000);
            uint16_t _v1 = (uint16_t)((((vpp_mv + 50) / 100) % 10));
            uint16_t _v2 = (uint16_t)((handle->vpp_mv + 50) / 1000);
            uint16_t _v3 = (uint16_t)((((handle->vpp_mv + 50) / 100) % 10));
            uint8_t _b[8];
            _b[0] = (uint8_t)((_v0 >> 8) & 0xFF);
            _b[1] = (uint8_t)(_v0 & 0xFF);
            _b[2] = (uint8_t)((_v1 >> 8) & 0xFF);
            _b[3] = (uint8_t)(_v1 & 0xFF);
            _b[4] = (uint8_t)((_v2 >> 8) & 0xFF);
            _b[5] = (uint8_t)(_v2 & 0xFF);
            _b[6] = (uint8_t)((_v3 >> 8) & 0xFF);
            _b[7] = (uint8_t)(_v3 & 0xFF);
            LOG_WARN_ID_BYTES(MSG_WARN_VPP_LOW, _b, 8);
            handle->response_code = RESPONSE_CODE_WARNING;
        }
    }
    // NO regulator clear — caller continues to use CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE through the write pulse.
}

void configure_flash_intel(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_INTEL_FLASH);
    handle->firestarter_operation_end = flash_intel_cleanup;
    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = flash_intel_write_init;
            handle->firestarter_operation_main = flash_intel_write_execute;
            break;
        case CMD_ERASE:
            handle->firestarter_operation_main = flash_intel_erase_execute;
            break;
        case CMD_BLANK_CHECK:
            handle->firestarter_operation_main = mem_util_blank_check;
            break;
        case CMD_CHECK_CHIP_ID:
            handle->firestarter_operation_init = NULL;
            handle->firestarter_operation_end = NULL;
            handle->firestarter_operation_main = flash_intel_check_chip_id;
            break;
    }
}

void flash_intel_write_init(firestarter_handle_t* handle) {
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE, 1);
    delay(500);
    flash_intel_check_vpp(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        // Safety: clear VPP regulator before early-return so 12V is not left
        // applied to socket pin 1 after an unsafe-voltage detection. Without
        // this, flash_intel_cleanup (END phase) is never reached and the
        // hazard the check exists to prevent stays asserted.
        handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE, 0);
        return;
    }
    if (handle->chip_id > 0) {
        flash_intel_check_chip_id(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE, 0);
            return;
        }
    }
    if (is_flag_set(FLAG_CAN_ERASE) && !is_flag_set(FLAG_SKIP_ERASE)) {
        flash_intel_erase_execute(handle);
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

void flash_intel_write_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint32_t address = handle->address + i;
        uint8_t data = handle->data_buffer[i];
        handle->firestarter_set_data(handle, address, 0x40);  // program setup
        handle->firestarter_set_data(handle, address, data);  // program data
        if (!flash_intel_poll_sr(handle, 150)) {
            return;
        }
    }
}

void flash_intel_erase_execute(firestarter_handle_t* handle) {
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE, 1);
    delay(500);
    handle->firestarter_set_data(handle, 0, 0x20);  // erase setup
    handle->firestarter_set_data(handle, 0, 0xD0);  // erase confirm
    if (!flash_intel_poll_sr(handle, 15000)) {
        return;
    }
    LOG_DEBUG_ID_SUB(DBG_ERASE_COMPLETE);
}

void flash_intel_cleanup(firestarter_handle_t* handle) {
    handle->firestarter_set_data(handle, 0, 0xFF);  // reset to read array mode
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE, 0);
}

static bool flash_intel_poll_sr(firestarter_handle_t* handle, uint16_t timeout_ms) {
    unsigned long deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        uint8_t sr = handle->firestarter_get_data(handle, 0);
        if (sr & 0x80) {
            if (sr & 0x10) {
                LOG_ERROR_ID(MSG_ERR_INTEL_VPP);
                handle->response_code = RESPONSE_CODE_ERROR;
                handle->firestarter_set_data(handle, 0, 0xFF);
                return false;
            }
            if (sr & 0x08) {
                LOG_ERROR_ID(MSG_ERR_INTEL_PROGRAM);
                handle->response_code = RESPONSE_CODE_ERROR;
                handle->firestarter_set_data(handle, 0, 0xFF);
                return false;
            }
            return true;
        }
    }
    LOG_ERROR_ID(MSG_ERR_INTEL_SR_TIMEOUT);
    handle->response_code = RESPONSE_CODE_ERROR;
    handle->firestarter_set_data(handle, 0, 0xFF);
    return false;
}

void flash_intel_check_chip_id(firestarter_handle_t* handle) {
    handle->firestarter_set_data(handle, 0, 0x90);  // enter autoselect
    uint16_t chip_id = handle->firestarter_get_data(handle, 0x0000) << 8;
    chip_id |= handle->firestarter_get_data(handle, 0x0001);
    handle->firestarter_set_data(handle, 0, 0xFF);  // exit autoselect
    chip_id_report(handle, chip_id);
}
