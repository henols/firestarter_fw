/*
 * Project Name: Firestarter
 * Copyright (c) 2025 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#if DEV_TOOLS
#include "dev_tools.h"

#include <Arduino.h>

#include "firestarter.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_internal_register_utils.h"
#include "rurp_shield.h"

// void rurp_internal_write_to_register(uint8_t reg, rurp_register_t data);

void dt_decode_register(firestarter_handle_t* handle, const char* reg_name, uint16_t reg, uint8_t size) {
    {
        uint8_t _len = (uint8_t)strlen(reg_name);
        if (_len > 14) _len = 14;
        uint8_t _b[16];
        _b[0] = _len;
        memcpy(&_b[1], reg_name, _len);
        _b[1 + _len] = (uint8_t)reg;
        LOG_INFO_ID_BYTES(MSG_INFO_REG_HEADER, _b, 1 + _len + 1);
    }

    // Header
    {
        const char* _prefix = (size == 9 ? "|D8" : "");
        uint8_t _len = (uint8_t)strlen(_prefix);
        uint8_t _b[8];
        _b[0] = _len;
        memcpy(&_b[1], _prefix, _len);
        LOG_INFO_ID_BYTES(MSG_INFO_BIT_HEADER, _b, 1 + _len);
    }

    // Values - build string manually to be more efficient than many-arg sprintf
    char bit_str[40];  // "| 1" is 3 chars. 9 bits -> 3*9=27. 8 bits -> 3*8=24. 40 is safe.
    char* p = bit_str;

    if (size == 9) {
        *p++ = '|';
        *p++ = ' ';
        *p++ = ((reg >> 8) & 1) ? '1' : '0';
    }

    for (int i = 7; i >= 0; i--) {
        *p++ = '|';
        *p++ = ' ';
        *p++ = ((reg >> i) & 1) ? '1' : '0';
    }
    *p++ = '|';
    *p = '\0';

    {
        uint8_t _len = (uint8_t)strlen(bit_str);
        if (_len > 31) _len = 31;
        uint8_t _b[32];
        _b[0] = _len;
        memcpy(&_b[1], bit_str, _len);
        LOG_INFO_ID_BYTES(MSG_INFO_BIT_STR, _b, 1 + _len);
    }
}

bool dt_set_registers(firestarter_handle_t* handle) {
    // Wait for an ACK message that precedes the register data
    if (op_get_message(handle) != OP_MSG_ACK) {
        return false;
    };

    unsigned long payload_deadline = millis() + TIMEOUT_MS;
    while (rurp_communication_available() < 4) {
        if ((long)(millis() - payload_deadline) >= 0) {
            return false;
        }
    }

    uint8_t msb = rurp_communication_read();
    uint8_t lsb = rurp_communication_read();
    uint16_t ctrl_reg = rurp_communication_read() << 8;
    ctrl_reg |= rurp_communication_read();
    int firestarter_reg = ctrl_reg & 0x8000;
    ctrl_reg &= 0x01FF;

    {
        uint8_t _b[2] = {
            (uint8_t)is_flag_set(FLAG_CHIP_ENABLE),
            (uint8_t)is_flag_set(FLAG_OUTPUT_ENABLE),
        };
        LOG_INFO_ID_BYTES(MSG_INFO_CE_OE, _b, 2);
    }
    dt_decode_register(handle, "MSB", msb, 8);
    dt_decode_register(handle, "LSB", lsb, 8);
#ifdef HARDWARE_REVISION
    if (firestarter_reg) {
        dt_decode_register(handle, "CTRL", ctrl_reg, 9);
        uint8_t ctrl_version = rurp_map_ctrl_reg_for_hardware_revision(ctrl_reg);
        dt_decode_register(handle, "CTRL remapped", ctrl_version, 8);
    } else {
        dt_decode_register(handle, "CTRL", ctrl_reg, 8);
    }
#else
#endif
    LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);  // semantics ≈ "setup done, waiting on user button"
    rurp_set_programmer_mode();

    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, lsb);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, msb);
    if (firestarter_reg) {
        rurp_write_to_register(CONTROL_REGISTER, ctrl_reg);
    } else {
        rurp_internal_write_to_register(CONTROL_REGISTER, ctrl_reg);
    }

    rurp_set_chip_enable(!is_flag_set(FLAG_CHIP_ENABLE));
    rurp_set_chip_output(!is_flag_set(FLAG_OUTPUT_ENABLE));

    while (!rurp_user_button_pressed()) {
        delay(200);
    }
    rurp_set_communication_mode();

    return true;
}

bool dt_set_address(firestarter_handle_t* handle) {
    {
        uint8_t _b[2] = {
            (uint8_t)is_flag_set(FLAG_CHIP_ENABLE),
            (uint8_t)is_flag_set(FLAG_OUTPUT_ENABLE),
        };
        LOG_INFO_ID_BYTES(MSG_INFO_CE_OE, _b, 2);
    }
    LOG_INFO_ID_U24(MSG_INFO_ADDR, handle->address);
    uint32_t address = mem_util_remap_address_bus(handle, handle->address, is_flag_set(FLAG_OUTPUT_ENABLE));
    LOG_INFO_ID_U24(MSG_INFO_ADDR_REMAP, address);

    uint8_t msb = mem_util_calculate_msb_register(handle, address);
    uint8_t lsb = mem_util_calculate_lsb_register(handle, address);
    uint8_t top_address = mem_util_calculate_top_address_register(handle, address);
#ifdef HARDWARE_REVISION
    uint8_t ctrl_version = rurp_map_ctrl_reg_for_hardware_revision(top_address);
    dt_decode_register(handle, "MSB", msb, 8);
    dt_decode_register(handle, "LSB", lsb, 8);
    dt_decode_register(handle, "TOP addr", top_address, 8);
    dt_decode_register(handle, "CTRL remapped", ctrl_version, 8);
#else
    // dt_decode_ctrl(top_address);
#endif
    LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);  // semantics ≈ "setup done, waiting on user button"
    rurp_set_programmer_mode();
    mem_util_set_address(handle, address);
    rurp_set_chip_enable(!is_flag_set(FLAG_CHIP_ENABLE));
    rurp_set_chip_output(!is_flag_set(FLAG_OUTPUT_ENABLE));
    while (!rurp_user_button_pressed()) {
        delay(200);
    }
    rurp_set_communication_mode();

    return true;
}

#endif