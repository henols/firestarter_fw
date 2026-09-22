/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "flash_intel.h"

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
        bool force = is_flag_set(FLAG_FORCE);
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv,
                                 force ? MSG_WARN_VPP_HIGH : MSG_ERR_VPP_HIGH,
                                 force ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
    } else if (vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100) {
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv, MSG_WARN_VPP_LOW, RESPONSE_CODE_WARNING);
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
    mem_util_report_chip_id(handle, chip_id, is_flag_set(FLAG_FORCE));
}
