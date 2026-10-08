/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "firestarter.h"

#include <Arduino.h>
#include <stdlib.h>

#include "eprom_budget.h"
#include "eprom_operations.h"
#include "hardware_operations.h"
#include "json_parser.h"
#include "logging_id.h"
#include "memory.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"
#include "rurp_config_migrate.h"
#include "version.h"
#if DEV_TOOLS
#include "dev_tools.h"
#endif

#define RX 0
#define TX 1

// Largest supply voltage, in millivolts, a calibration frame may report.
#define CONFIG_MEASURED_VCC_MAX_MV 10000

bool init_programmer_framed(firestarter_handle_t* handle);
bool parse_json(firestarter_handle_t* handle);
void command_done(firestarter_handle_t* handle);

firestarter_handle_t handle;

unsigned long timeout = 0;

void setup() {
    rurp_load_config();
    rurp_board_setup();
#ifdef HARDWARE_REVISION
    // Detect after rurp_board_setup: serial is then up, so the detection's
    // REVISION_UNKNOWN warning reaches the host, and every rail is off.
    // rurp_board_setup does not touch the two analog detect pins.
    rurp_detect_hardware_revision();
#endif

    handle.cmd = CMD_IDLE;
    LOG_DEBUG_ID_SUB(DBG_FIRESTARTER_STARTED);
    LOG_DEBUG_ID_SUB_ASTR(DBG_FIRMWARE_VERSION, FW_VERSION);
    LOG_DEBUG_ID_SUB_U8(DBG_HARDWARE_REVISION, (uint8_t)rurp_get_physical_hardware_revision());
}

bool parse_json(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_PARSE_JSON);

    jsmn_parser parser;
    static jsmntok_t tokens[NUMBER_JSNM_TOKENS];

    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, handle->data_buffer, handle->data_size, tokens, NUMBER_JSNM_TOKENS);
    if (token_count <= 0) {
        handle->ctrl_flags = 0x80;
        LOG_DEBUG_ID_SUB_U8(DBG_BUF_VAL, handle->data_buffer[0]);
        LOG_ERROR_ID(MSG_ERR_BAD_JSON);

        return false;
    }

    handle->cmd = json_get_cmd(handle->data_buffer, tokens, token_count, handle);
    LOG_DEBUG_ID_SUB_U16(DBG_TOKEN_COUNT, (uint16_t)token_count);
    if (handle->cmd == 0xFF) {
        LOG_ERROR_ID(MSG_ERR_NO_CMD);
        return false;
    }

    LOG_DEBUG_ID_SUB_U8(DBG_CMD, (uint8_t)handle->cmd);
    if (is_memory_cmd(handle->cmd) || handle->cmd < CMD_READ_VPP) {
        if (json_parse(handle->data_buffer, tokens, token_count, handle) < 0) {
            LOG_ERROR_ID(MSG_ERR_BAD_JSON);
            return false;
        }
        // Only the two debug log lines in the `else` carry a build conditional:
        // the flags they name have no meaning outside a DEV_TOOLS build.
        //
        // The test is predicate-first (`is_memory_cmd(handle->cmd) ||`) with the
        // ordinal test as fallback, so a memory command above ordinal 11 is
        // admitted like one below it.
        if (is_memory_cmd(handle->cmd)) {
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_FORCE, is_flag_set(FLAG_FORCE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CAN_ERASE, is_flag_set(FLAG_CAN_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_ERASE, is_flag_set(FLAG_SKIP_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_VPE_AS_VPP, is_flag_set(FLAG_VPE_AS_VPP));
            if (!op_execute_function(configure_memory, handle)) {
                LOG_ERROR_ID(MSG_ERR_SETUP);
                return false;
            }
        } else {
#if DEV_TOOLS
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_OUTPUT_EN, is_flag_set(FLAG_OUTPUT_ENABLE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CHIP_EN, is_flag_set(FLAG_CHIP_ENABLE));
#endif
        }
    } else if (handle->cmd == CMD_CONFIG) {
        // Edits go into a staged copy. The live configuration changes, and is
        // saved, only when the whole frame is accepted.
        rurp_configuration_t staged = *rurp_get_config();
        int res = json_parse_config(handle->data_buffer, tokens, token_count, &staged, handle);
        if (res < 0) {
            LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
            return false;
        }
        if (res & JSON_CFG_CALIBRATE) {
            // Back-solve this MCU's bandgap from the operator's meter reading
            // of the supply pin. Refused, never clamped: a stored bandgap is
            // a value the firmware trusts to judge programming voltages.
            // Sample in PROGRAMMER mode, as eprom_check_vpp does when it
            // judges a programming voltage. The I/O state differs between
            // the two modes and moves the bandgap count by a whole count on
            // a SERIAL_ON_IO board, about 0.5 %.
            long vcc_mv = json_config_measured_vcc_mv();
            if (vcc_mv <= 0 || vcc_mv > CONFIG_MEASURED_VCC_MAX_MV) {
                LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
                return false;
            }
            rurp_set_programmer_mode();
            uint32_t bandgap_adc = (uint32_t)rurp_get_bandgap_adc_reading();
            rurp_set_communication_mode();
            uint16_t measured = rurp_bandgap_from_measured_vcc(
                (uint32_t)vcc_mv, bandgap_adc, RURP_ADC_FULL_SCALE);
            if (measured == 0) {
                LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
                return false;
            }
            staged.bandgap_mv = measured;
            res |= JSON_CFG_FIELD_CHANGED;
        }
        if (res & JSON_CFG_FIELD_CHANGED) {
            // rurp_config_migrate corrects any out-of-range field. A change
            // means the frame carried a bad value: refuse the whole frame
            // and leave the live configuration as it was.
            if (rurp_config_migrate(&staged)) {
                LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
                return false;
            }
            *rurp_get_config() = staged;
            rurp_save_config(rurp_get_config());
        }
    }
    return true;
}

bool init_programmer_framed(firestarter_handle_t* handle) {
    handle->response_code = RESPONSE_CODE_OK;
    handle->operation_state = 0;

    /* data_buffer and data_size are pre-filled by the CMD_IDLE COBS decode
     * step. data_buffer[data_size] is already NUL-terminated there. */
    handle->ctrl_flags = 0x80;
    LOG_DEBUG_ID_SUB_U16(DBG_BUFFER_SIZE, (uint16_t)handle->data_size);
    if (handle->data_size == 0) {
        LOG_ERROR_ID(MSG_ERR_EMPTY_INPUT);
        return false;
    }
    LOG_DEBUG_ID_SUB(DBG_SETUP);

    if (!parse_json(handle)) {
        return false;
    }

    // This ordinal-range test is separate from is_memory_cmd(). It gates
    // diagnostic output only (three DBG_* lines), never hardware configuration.
    //
    // CMD_LOCK_STATUS (16) is above CMD_READ_VPP (11), so it falls outside
    // this range and emits none of the three DBG_* lines below.
    if (handle->cmd > CMD_IDLE && handle->cmd < CMD_READ_VPP) {
        LOG_DEBUG_ID_SUB_U32(DBG_MEM_SIZE, (uint32_t)handle->mem_size);
        LOG_DEBUG_ID_SUB_U32(DBG_ADDR_MASK, (uint32_t)handle->bus_config.address_mask);
        LOG_DEBUG_ID_SUB_U16(DBG_MATCH_LINES, (uint16_t)handle->bus_config.matching_lines);
    }
    // Per-command identity echo (INFO severity, FLAG_VERBOSE-gated at the
    // macro level; visible in host verbose mode, filtered out of the
    // OK/ERROR ack chain).
    LOG_INFO_ID_ASTR(MSG_INFO_FW, FW_VERSION);
#ifdef HARDWARE_REVISION
    LOG_INFO_ID_U8(MSG_INFO_PHYSICAL_HW, (uint8_t)rurp_get_physical_hardware_revision());
    LOG_INFO_ID_U8(MSG_INFO_HW, (uint8_t)rurp_get_hardware_revision());
#endif
    LOG_INFO_ID_U8(MSG_INFO_CMD, (uint8_t)handle->cmd);
    // Wire layout, three length-discriminated extensions of one variable blob:
    //   [buffer_size u16 BE][hw_revision u8][ver_len u8][ver bytes][write_budget_s u16 BE]
    //
    // Emit this for every command, so the ack has the same shape for each
    // command and a length-discriminating host decoder keeps its discriminator.
    // write_budget_s is 0 for a command that is not a memory command, and for
    // a protocol that is not an EPROM protocol. The host then leaves its
    // attribute unset and uses its fallback.
    //
    // The budget is already padded by the firmware: only the firmware knows the
    // once-per-block VPE settle, the verify passes, the per-pulse settle and the
    // transport time, so the host applies no multiplier. include/eprom_budget.h
    // has the padding rule.
    //
    // A host that tests `len(params) == 2` does not match the longer blob and
    // falls back to its 512-byte chunk floor.
    //
    // The identity is safe to emit here: configure_memory has run, but every
    // configure_* handler only assigns function pointers, and the VPP regulator
    // is not engaged until firestarter_operation_init, behind op_wait_for_ack().
    // A host that reads this ack and refuses stops with the rail still down.
    {
        const char* _ver = FW_VERSION;
        uint8_t _vlen = (uint8_t)strlen(_ver);
        if (_vlen > 32) _vlen = 32;
        uint8_t _ready[4 + 32 + 2];
        _ready[0] = (uint8_t)(((uint16_t)DATA_BUFFER_SIZE >> 8) & 0xFF);
        _ready[1] = (uint8_t)((uint16_t)DATA_BUFFER_SIZE & 0xFF);
#ifdef HARDWARE_REVISION
        _ready[2] = (uint8_t)rurp_get_hardware_revision();
#else
        _ready[2] = 0xFE;  // REVISION_UNKNOWN; its symbol is defined only under HARDWARE_REVISION
#endif
        _ready[3] = _vlen;
        memcpy(_ready + 4, _ver, _vlen);
        uint16_t _budget = is_memory_cmd(handle->cmd)
                               ? eprom_block_budget_s(handle->protocol, handle->pulse_delay,
                                                      (uint32_t)DATA_BUFFER_SIZE)
                               : 0;
        _ready[4 + _vlen]     = (uint8_t)((_budget >> 8) & 0xFF);
        _ready[4 + _vlen + 1] = (uint8_t)(_budget & 0xFF);
        LOG_OK_ID_BYTES(MSG_OK_READY, _ready, (uint8_t)(4 + _vlen + 2));
    }
    op_reset_timeout();
    return true;
}

void command_done(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CMD_FINISHED);
    rurp_set_programmer_mode();
    rurp_chip_disable();
    rurp_write_to_register(CONTROL_REGISTER, 0x00);
    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, 0x00);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, 0x00);
    handle->cmd = CMD_IDLE;
    handle->firestarter_operation_init = NULL;
    handle->firestarter_operation_main = NULL;
    handle->firestarter_operation_end = NULL;
    rurp_set_communication_mode();
}

void loop() {
    if (handle.cmd != CMD_IDLE && timeout < millis()) {
        LOG_ERROR_ID_U8(MSG_ERR_CMD_TIMEOUT, handle.cmd);
        command_done(&handle);
    } else if (handle.cmd == CMD_IDLE) {
        if (rurp_communication_available() > 0) {
            /* rurp_communication_read_data() reads through the 0x00 delimiter,
             * COBS-decodes in place, verifies CRC8 before any JSON byte is
             * examined, and on any COBS/CRC/overflow failure drains to the
             * delimiter and returns negative.
             *
             * Gate on n > 0: a zero-length decode is not a valid command.
             * On n <= 0: log the frame error and stay CMD_IDLE. */
            int n = rurp_communication_read_data(handle.data_buffer, DATA_BUFFER_SIZE - 1);
            if (n > 0) {
                handle.data_size = (uint32_t)n;
                /* The decoder caps n at DATA_BUFFER_SIZE-1, so n < DATA_BUFFER_SIZE
                 * always holds and data_buffer[n] is in bounds. This guard
                 * keeps the write in bounds if that cap ever changes. */
                if (n < DATA_BUFFER_SIZE) {
                    handle.data_buffer[n] = '\0';
                }
                if (init_programmer_framed(&handle)) {
                    return;
                }
                // A refused command must not run, and must not wait for the
                // timeout to be reaped.
                command_done(&handle);
            } else {
                /* CRC mismatch, COBS violation, overflow, or read underrun.
                 * There is no frame-error message id, so MSG_ERR_EMPTY_INPUT
                 * is used. */
                LOG_ERROR_ID(MSG_ERR_EMPTY_INPUT);
            }
        }
        return;
    }

    bool finished = false;
    handle.response_code = RESPONSE_CODE_OK;
    switch (handle.cmd) {
        case CMD_READ:
            finished = eprom_read(&handle);
            break;
        case CMD_WRITE:
            finished = eprom_write(&handle);
            break;
        case CMD_ERASE:
            finished = eprom_erase(&handle);
            break;
        case CMD_CHECK_CHIP_ID:
            finished = eprom_check_chip_id(&handle);
            break;
        // Payload-free commands (the SDP commands, whose protocol handler
        // leaves init and end NULL) still run the INIT and END phases:
        // _execute_operation_house_keeping_func calls op_wait_for_ack() and
        // emits the INIT and END frame pairs, so each phase costs a host ACK
        // round-trip. They send no DONE round-trip and no '#' data frame.
        // op_wait_for_ack has a 1000 ms timeout and emits MSG_ERR_TIMEOUT on
        // expiry, so a host that does not ACK times out instead of hanging.
        case CMD_SDP_UNLOCK:
            finished = eprom_sdp_unlock(&handle);
            break;
        case CMD_SDP_LOCK:
            finished = eprom_sdp_lock(&handle);
            break;
        case CMD_LOCK_STATUS:
            finished = eprom_lock_status(&handle);
            break;
        case CMD_READ_VPP:
        case CMD_READ_VPE:
            finished = hw_read_voltage(&handle);
            break;
        case CMD_IDLE:
            break;
        case CMD_FW_VERSION:
            finished = fw_get_version(&handle);
            break;
#ifdef HARDWARE_REVISION
        case CMD_HW_VERSION:
            finished = hw_get_version(&handle);
            break;
#endif
#if DEV_TOOLS
        case CMD_DEV_REGISTER:
            finished = dt_set_registers(&handle);
            break;
        case CMD_DEV_ADDRESS:
            finished = dt_set_address(&handle);
            break;
        case CMD_DEV_ADC:
            finished = dt_read_adc(&handle);
            break;
#endif

        case CMD_CONFIG:
            finished = hw_get_config(&handle);
            break;

        default:
            LOG_ERROR_ID_U8(MSG_ERR_UNKNOWN_CMD, handle.cmd);
            finished = true;
            break;
    }
    if (finished) {
        command_done(&handle);
    }
}

void op_reset_timeout() {
    timeout = millis() + TIMEOUT_MS;
}
