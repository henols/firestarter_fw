/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "firestarter.h"

#include <Arduino.h>
#include <stdlib.h>

#include "eprom_operations.h"
#include "hardware_operations.h"
#include "command_decoder.h"
#include "logging_id.h"
#include "memory.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "rurp_voltage_math.h"
#include "rurp_config_migrate.h"
#include "version.h"
#include "timeout_utils.h"
#if DEV_TOOLS
#include "dev_tools.h"
#endif

#define RX 0
#define TX 1

bool init_programmer_framed(firestarter_handle_t* handle);
bool parse_command(firestarter_handle_t* handle);
void command_done(firestarter_handle_t* handle);

firestarter_handle_t handle;

uint32_t timeout_started_ms = 0;

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

bool parse_command(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_PARSE_COMMAND);

    uint8_t res = cmd_decode(handle);
    if (res == CMD_DECODE_BAD_FRAME) {
        // A JSON-era host lands here too: its first byte is '{', not CF_MAGIC.
        LOG_DEBUG_ID_SUB_U8(DBG_BUF_VAL, handle->data_buffer[0]);
        LOG_ERROR_ID(MSG_ERR_BAD_FRAME);
        return false;
    }
    if (res == CMD_DECODE_BAD_VERSION) {
        // Send the version this firmware reads, so the host can tell which side is older.
        LOG_ERROR_ID_U8(MSG_ERR_FRAME_VERSION, CF_VERSION);
        return false;
    }
    if (res != CMD_DECODE_OK) {
        LOG_ERROR_ID_U8(MSG_ERR_BAD_COMMAND, res);
        return false;
    }

    LOG_DEBUG_ID_SUB_U8(DBG_CMD, (uint8_t)handle->cmd);
    if (is_memory_cmd(handle->cmd)) {
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_FORCE, is_flag_set(FLAG_FORCE));
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CAN_ERASE, is_flag_set(FLAG_CAN_ERASE));
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_ERASE, is_flag_set(FLAG_SKIP_ERASE));
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_VPE_AS_VPP, is_flag_set(FLAG_VPE_AS_VPP));
        if (!op_execute_function(configure_memory, handle)) {
            LOG_ERROR_ID(MSG_ERR_SETUP);
            return false;
        }
    } else if (handle->cmd < CMD_READ_VPP) {
#if DEV_TOOLS
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_OUTPUT_EN, is_flag_set(FLAG_OUTPUT_ENABLE));
        LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CHIP_EN, is_flag_set(FLAG_CHIP_ENABLE));
#endif
    } else if (handle->cmd == CMD_CONFIG) {
        // Parse into a copy. The live configuration changes only after every
        // field has parsed, any calibration has back-solved and the result
        // is valid, so a refused command leaves nothing behind in RAM.
        rurp_configuration_t config = *rurp_get_config();
        uint8_t cfg = cmd_apply_config(handle, &config);
#if RURP_HAS_BANDGAP_CAL
        if (cfg & CMD_CFG_CALIBRATE) {
            // Back-solve this MCU's bandgap from the operator's meter reading
            // of the supply pin. Refused, never clamped: a stored bandgap is
            // a value the firmware trusts to judge programming voltages.
            // Sample in PROGRAMMER mode, because that is the mode every
            // real measurement is taken in: hw_read_voltage and
            // eprom_check_vpp both switch to it before reading. The I/O state
            // differs between the two modes and moves the bandgap count by a
            // whole count on a SERIAL_ON_IO board -- about 0.5 %, stored
            // permanently. Calibrate under the conditions you measure under.
            rurp_set_programmer_mode();
            uint32_t bandgap_adc = (uint32_t)rurp_get_bandgap_adc_reading();
            rurp_set_communication_mode();
            uint16_t measured = rurp_bandgap_from_measured_vcc(
                (uint32_t)cmd_config_measured_vcc_mv(handle), bandgap_adc,
                RURP_ADC_FULL_SCALE);
            if (measured == 0) {
                LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
                return false;
            }
            config.bandgap_mv = measured;
            cfg |= CMD_CFG_FIELD_CHANGED;
        }
#endif
        if (cfg & CMD_CFG_FIELD_CHANGED) {
            // Refused, never corrected: a corrected value would be acked as
            // if the operator's value had been stored. Boot-time migration
            // still corrects, because no operator is there to tell.
            if (!rurp_config_commit(rurp_get_config(), &config)) {
                LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
                return false;
            }
            rurp_save_config(rurp_get_config());
        }
    }
    return true;
}

bool init_programmer_framed(firestarter_handle_t* handle) {
    handle->response_code = RESPONSE_CODE_OK;
    handle->operation_state = 0;

    /* CMD_IDLE already decoded the frame and NUL-terminated data_buffer.
 * Parse it without another serial read. */
    handle->ctrl_flags = 0x80;
    LOG_DEBUG_ID_SUB_U16(DBG_BUFFER_SIZE, (uint16_t)handle->data_size);
    if (handle->data_size == 0) {
        LOG_ERROR_ID(MSG_ERR_EMPTY_INPUT);
        return false;
    }
    LOG_DEBUG_ID_SUB(DBG_SETUP);

    if (!parse_command(handle)) {
        return false;
    };

    // This ordinal range gates diagnostic output only, not command admission.
    // CMD_LOCK_STATUS is intentionally outside the diagnostic range.
    if (handle->cmd > CMD_IDLE && handle->cmd < CMD_READ_VPP) {
        LOG_DEBUG_ID_SUB_U32(DBG_MEM_SIZE, (uint32_t)handle->mem_size);
        LOG_DEBUG_ID_SUB_U32(DBG_ADDR_MASK, (uint32_t)handle->bus_config.address_mask);
        LOG_DEBUG_ID_SUB_U16(DBG_MATCH_LINES, (uint16_t)handle->bus_config.matching_lines);
    }
    // Per-command identity echo (INFO severity, FLAG_VERBOSE-gated at the
    // macro level — visible in host verbose mode, filtered out of the
    // OK/ERROR ack chain).
    LOG_INFO_ID_ASTR(MSG_INFO_FW, FW_VERSION);
#ifdef HARDWARE_REVISION
    LOG_INFO_ID_U8(MSG_INFO_PHYSICAL_HW, (uint8_t)rurp_get_physical_hardware_revision());
    LOG_INFO_ID_U8(MSG_INFO_HW, (uint8_t)rurp_get_hardware_revision());
#endif
    LOG_INFO_ID_U8(MSG_INFO_CMD, (uint8_t)handle->cmd);
    // MSG_OK_READY parameters are length-discriminated:
    // [buffer_size u16 BE][hw_revision u8][ver_len u8][ver bytes].
    // Keep the shape identical for every command so hosts can decode by length.
    // The host calculates the write-time budget itself (write_budget.py).
    // This reply is sent before HV is enabled, so a host can reject safely.
    {
        const char* _ver = FW_VERSION;
        uint8_t _vlen = (uint8_t)strlen(_ver);
        if (_vlen > 32) _vlen = 32;
        uint8_t _ready[4 + 32];
        _ready[0] = (uint8_t)(((uint16_t)DATA_BUFFER_SIZE >> 8) & 0xFF);
        _ready[1] = (uint8_t)((uint16_t)DATA_BUFFER_SIZE & 0xFF);
#ifdef HARDWARE_REVISION
        _ready[2] = (uint8_t)rurp_get_hardware_revision();
#else
        _ready[2] = 0xFE;  // REVISION_UNKNOWN -- the symbol lives inside that same #ifdef
#endif
        _ready[3] = _vlen;
        memcpy(_ready + 4, _ver, _vlen);
        LOG_OK_ID_BYTES(MSG_OK_READY, _ready, (uint8_t)(4 + _vlen));
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
    rurp_set_communication_mode();
}

void loop() {
    if (handle.cmd != CMD_IDLE && fs_timeout_expired(timeout_started_ms, TIMEOUT_MS, (uint32_t)millis())) {
        LOG_ERROR_ID_U8(MSG_ERR_CMD_TIMEOUT, handle.cmd);
        command_done(&handle);
    } else if (handle.cmd == CMD_IDLE) {
        if (rurp_communication_available() > 0) {
            /* The transport reads complete COBS frames and checks CRC8 before command decoding.
 * It drains invalid frames to the next delimiter for bounded recovery;
 * a decoded payload must have positive length to become a command. */
            int n = rurp_communication_read_data(handle.data_buffer, DATA_BUFFER_SIZE - 1);
            if (n > 0) {
                handle.data_size = (uint32_t)n;
                /* Belt-and-suspenders: the decoder caps n at
                 * DATA_BUFFER_SIZE-1 (PUSH guard), so n < DATA_BUFFER_SIZE
                 * always holds and data_buffer[n] is in-bounds.
                 * This guard documents the invariant at the write site and
                 * protects against any future caller that forgets the cap. */
                if (n < DATA_BUFFER_SIZE) {
                    handle.data_buffer[n] = '\0';
                }
                if (init_programmer_framed(&handle)) {
                    return;
                }
                // Setup may have set cmd before rejecting the frame. Clear it
                // and disable outputs now, not on the next timeout tick.
                command_done(&handle);
            } else {
                /* Invalid COBS/CRC/overflow/underrun frames share MSG_ERR_EMPTY_INPUT. */
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
                // Standalone lock operations use the normal INIT/MAIN/END ACKs.
                // They have no data frames or write-path DONE handshake.
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
    timeout_started_ms = (uint32_t)millis();
}
