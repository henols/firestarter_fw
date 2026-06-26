/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Dispatch entry point and main firmware loop.
 *
 * This file is the top-level orchestrator: it receives JSON commands over serial
 * (250000 baud, COBS-framed + CRC8-CCITT), parses them into firestarter_handle_t,
 * and calls configure_memory() which dispatches to the correct algorithm handler
 * based on handle->protocol (the algorithm field from chip_database.json).
 *
 * The INV-01..INV-09 invariant ordering established in firestarter/doc/PROTOCOLS.md §3
 * is grounded in the dispatch chain that originates here. The dispatch structure
 * (memory.cpp:configure_memory()) is the source-of-truth for handler routing — see
 * firestarter/CLAUDE.md §Protocol Dispatch for the step-by-step dispatch table.
 *
 * This dispatch structure is FROZEN for Phase 87 (naming/documentation pass).
 * No protocol routing, wire value, or handler assignment is changed this phase.
 * The INV ids (INV-01..INV-09) cited in each handler's header block are the SAFE-02
 * handoff contract to Phases 88 (golden traces) and 89 (recompose).
 *
 * Full protocol prose: firestarter/doc/PROTOCOLS.md.
 */

#include "firestarter.h"

#include <Arduino.h>
#include <stdlib.h>

#include "eprom_operations.h"
#include "hardware_operations.h"
#include "json_parser.h"
#include "logging_id.h"
#include "memory.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "version.h"
#ifdef DEV_TOOLS
#include "dev_tools.h"
#endif

#define RX 0
#define TX 1

bool init_programmer_framed(firestarter_handle_t* handle);
bool parse_json(firestarter_handle_t* handle);
void command_done(firestarter_handle_t* handle);

firestarter_handle_t handle;

unsigned long timeout = 0;

void setup() {
    // Phase 9: deleted the SERIAL_DEBUG bootstrap call (legacy soft-serial
    // debug path replaced by LOG_DEBUG_ID_SUB* from Phase 8 Plan 07).

    rurp_load_config();
#ifdef HARDWARE_REVISION
    rurp_detect_hardware_revision();
#endif
    rurp_board_setup();

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
    if (handle->cmd < CMD_READ_VPP) {
        json_parse(handle->data_buffer, tokens, token_count, handle);
#ifdef DEV_TOOLS
        if (handle->cmd < CMD_DEV_ADDRESS) {
#endif
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_FORCE, is_flag_set(FLAG_FORCE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CAN_ERASE, is_flag_set(FLAG_CAN_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_ERASE, is_flag_set(FLAG_SKIP_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_BLANK, is_flag_set(FLAG_SKIP_BLANK_CHECK));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_VPE_AS_VPP, is_flag_set(FLAG_VPE_AS_VPP));
            if (!op_execute_function(configure_memory, handle)) {
                LOG_ERROR_ID(MSG_ERR_SETUP);
                return false;
            }
#ifdef DEV_TOOLS
        } else {
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_OUTPUT_EN, is_flag_set(FLAG_OUTPUT_ENABLE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CHIP_EN, is_flag_set(FLAG_CHIP_ENABLE));
        }
#endif
    } else if (handle->cmd == CMD_CONFIG) {
        rurp_configuration_t* config = rurp_get_config();
        int res = json_parse_config(handle->data_buffer, tokens, token_count, config, handle);
        if (res < 0) {
            LOG_ERROR_ID(MSG_ERR_PARSE_CFG);
            return false;
        } else if (res == 1) {
            rurp_save_config(config);
        }
    }
    return true;
}

bool init_programmer_framed(firestarter_handle_t* handle) {
    handle->response_code = RESPONSE_CODE_OK;
    handle->operation_state = 0;

    /* data_buffer and data_size are pre-filled by the CMD_IDLE COBS decode
     * step (Phase 51 — the rurp_communication_read_bytes call is deleted).
     * data_buffer[data_size] is already NUL-terminated by the CMD_IDLE branch. */
    handle->ctrl_flags = 0x80;
    LOG_DEBUG_ID_SUB_U16(DBG_BUFFER_SIZE, (uint16_t)handle->data_size);
    if (handle->data_size == 0) {
        LOG_ERROR_ID(MSG_ERR_EMPTY_INPUT);
        return false;
    }
    LOG_DEBUG_ID_SUB(DBG_SETUP);

    if (!parse_json(handle)) {
        return false;
    };

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
    LOG_OK_ID_U16(MSG_OK_READY, (uint16_t)DATA_BUFFER_SIZE);
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
    if (handle.cmd != CMD_IDLE && timeout < millis()) {
        LOG_ERROR_ID_U8(MSG_ERR_CMD_TIMEOUT, handle.cmd);
        command_done(&handle);
    } else if (handle.cmd == CMD_IDLE) {
        if (rurp_communication_available() > 0) {
            /* Phase 51: COBS frame decode replaces the legacy '{'-peek /
             * discard-non-'{' loop (D-05 deleted).
             *
             * rurp_communication_read_data() reads through the 0x00 delimiter,
             * COBS-decodes in place, verifies CRC8 BEFORE any JSON parse byte
             * is examined (V5 / ADR §4.4 / T-51-01 mitigation), and on any
             * COBS/CRC/overflow failure calls _drain_to_delimiter() internally
             * and returns negative — NO additional drain logic needed here.
             *
             * Gate STRICTLY on n > 0: a zero-length decode is not a valid
             * command.  On n <= 0: log the frame error and stay CMD_IDLE
             * (bounded recovery is fully handled by the decoder; D-06). */
            int n = rurp_communication_read_data(handle.data_buffer, DATA_BUFFER_SIZE - 1);
            if (n > 0) {
                handle.data_size = (uint32_t)n;
                /* CR-01 belt-and-suspenders: the decoder caps n at
                 * DATA_BUFFER_SIZE-1 (PUSH guard), so n < DATA_BUFFER_SIZE
                 * always holds post-fix and data_buffer[n] is in-bounds.
                 * This guard documents the invariant at the write site and
                 * protects against any future caller that forgets the cap. */
                if (n < DATA_BUFFER_SIZE) {
                    handle.data_buffer[n] = '\0';
                }
                if (init_programmer_framed(&handle)) {
                    return;
                }
            } else {
                /* CRC mismatch, COBS violation, overflow, or read underrun.
                 * MSG_ERR_EMPTY_INPUT reused (messages.h is codegen; adding
                 * MSG_ERR_BAD_FRAME requires a TOML catalog update — deferred). */
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
        case CMD_VERIFY:
            finished = eprom_verify(&handle);
            break;
        case CMD_ERASE:
            finished = eprom_erase(&handle);
            break;
        case CMD_BLANK_CHECK:
            finished = eprom_blank_check(&handle);
            break;
        case CMD_CHECK_CHIP_ID:
            finished = eprom_check_chip_id(&handle);
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
#ifdef DEV_TOOLS
        case CMD_DEV_REGISTER:
            finished = dt_set_registers(&handle);
            break;
        case CMD_DEV_ADDRESS:
            finished = dt_set_address(&handle);
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
