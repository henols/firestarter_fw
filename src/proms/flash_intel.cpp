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
void flash_intel_erase_init(firestarter_handle_t* handle);
void flash_intel_write_execute(firestarter_handle_t* handle);
void flash_intel_erase_execute(firestarter_handle_t* handle);
void flash_intel_cleanup(firestarter_handle_t* handle);
void flash_intel_check_chip_id(firestarter_handle_t* handle);
static bool flash_intel_poll_sr(firestarter_handle_t* handle, uint16_t timeout_ms);

#define FLASH_INTEL_HV_MASK (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE)

// Brings the program voltage up for a write or an erase. Returns true when the
// caller may continue with the rail on socket pin 1, and false when it refused.
//
// The regulator alone is asserted first and measured. Only a reading inside the
// VPP window adds the pin 1 route. On a refusal both bits are cleared, because
// the END phase does not run after an init that fails.
//
// Rev 0 cannot measure the rail: the helper warns and asserts both bits without
// a reading, and skips the calibration refusal, as eprom_check_vpp does.
static bool flash_intel_vpp_up(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_VPP_INTEL);
#ifdef HARDWARE_REVISION
    if (rurp_get_hardware_revision() == REVISION_0) {
        LOG_WARN_ID(MSG_WARN_REV0_VPP_UNSUPPORTED);
        handle->response_code = RESPONSE_CODE_WARNING;
        handle->firestarter_set_control_register(handle, FLASH_INTEL_HV_MASK, 1);
        delay(500);
        return true;
    }
#endif
    if (mem_util_refuse_bad_calibration(handle)) {
        return false;
    }
    mem_util_warn_if_uncalibrated(handle);
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    delay(500);
    uint16_t vpp_mv = rurp_read_voltage_mv();
    LOG_DEBUG_ID_SUB_U16(DBG_CHECKING_VPP_VOLTAGE, vpp_mv);
    mem_util_check_vpp_window(handle, vpp_mv);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        handle->firestarter_set_control_register(handle, FLASH_INTEL_HV_MASK, 0);
        return false;
    }
    handle->firestarter_set_control_register(handle, CTRL_VPP_P1_ENABLE, 1);
    return true;
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
            handle->firestarter_operation_init = flash_intel_erase_init;
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
    if (!flash_intel_vpp_up(handle)) {
        return;
    }
    if (handle->chip_id > 0) {
        flash_intel_check_chip_id(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            handle->firestarter_set_control_register(handle, FLASH_INTEL_HV_MASK, 0);
            return;
        }
    }
    if (is_flag_set(FLAG_CAN_ERASE) && !is_flag_set(FLAG_SKIP_ERASE)) {
        flash_intel_erase_execute(handle);
    }
}

void flash_intel_erase_init(firestarter_handle_t* handle) {
    flash_intel_vpp_up(handle);
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
    handle->firestarter_set_data(handle, 0, 0x20);  // erase setup
    handle->firestarter_set_data(handle, 0, 0xD0);  // erase confirm
    if (!flash_intel_poll_sr(handle, 15000)) {
        return;
    }
    LOG_DEBUG_ID_SUB(DBG_ERASE_COMPLETE);
}

void flash_intel_cleanup(firestarter_handle_t* handle) {
    handle->firestarter_set_data(handle, 0, 0xFF);  // reset to read array mode
    handle->firestarter_set_control_register(handle, FLASH_INTEL_HV_MASK, 0);
}

static bool flash_intel_poll_sr(firestarter_handle_t* handle, uint16_t timeout_ms) {
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < timeout_ms) {
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
