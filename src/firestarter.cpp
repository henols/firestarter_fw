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
#include "version.h"
#if DEV_TOOLS
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
    if (is_memory_cmd(handle->cmd) || handle->cmd < CMD_READ_VPP) {
        json_parse(handle->data_buffer, tokens, token_count, handle);
        // Neither this `if` nor its `else` carries a build conditional; only the two
        // debug log lines inside the `else` do, because the flags they name have no
        // meaning outside a DEV_TOOLS build. In a release build that body compiles
        // empty, which is intended.
        //
        // The test is predicate-first (`is_memory_cmd(handle->cmd) ||`) with the cheap
        // ordinal test as fallback, so a memory command above ordinal 11 is admitted
        // exactly as one below 11 already was. Re-ordering the CMD_* enum instead was
        // rejected: it breaks wire compatibility with every shipped firmware and host.
        if (is_memory_cmd(handle->cmd)) {
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_FORCE, is_flag_set(FLAG_FORCE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_CAN_ERASE, is_flag_set(FLAG_CAN_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_ERASE, is_flag_set(FLAG_SKIP_ERASE));
            LOG_DEBUG_ID_SUB_U8(DBG_FLAG_SKIP_BLANK, is_flag_set(FLAG_SKIP_BLANK_CHECK));
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
     * step (the rurp_communication_read_bytes call is deleted).
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

    // A SECOND, independent ordinal-range guard, deliberately NOT converted to
    // is_memory_cmd(). It gates diagnostic output only -- three DBG_* lines --
    // never hardware configuration, so the admission gate's safety argument does
    // not apply. Converting it would silently drop those lines for cmd 7/8 in a
    // DEV_TOOLS build for no safety gain.
    //
    // CMD_LOCK_STATUS (16) is above CMD_READ_VPP (11) and so falls outside this
    // range by construction -- this is a CHOICE, not an oversight, so
    // `dev lock-status` emits none of the three DBG_* diagnostic lines below.
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
    // Wire layout -- three length-discriminated extensions of one variable blob:
    //   [buffer_size u16 BE][hw_revision u8][ver_len u8][ver bytes][write_budget_s u16 BE]
    //
    // MSG_OK_READY's catalog entry is a variable-length byte blob, so extending it
    // needs no messages.toml edit and no codegen run.
    //
    // Emit this for EVERY command, not just CMD_WRITE: the ack's shape must not
    // vary by command, or a length-discriminating host decoder loses its only
    // discriminator. eprom_block_budget_s returns 0 for a non-EPROM protocol, and
    // the host's plausibility clamp then leaves its attribute unset and falls back
    // -- which also covers the non-memory-command case where configure_memory
    // never ran and pulse_delay is still 0.
    //
    // The budget is already PADDED by the firmware: only the firmware knows the
    // once-per-block VPE settle, the verify passes, the per-pulse settle and the
    // transport time, so the host applies no multiplier. include/eprom_budget.h
    // has the padding rule.
    //
    // Backward compatibility is a LENGTH test and degrades rather than misparses:
    // a host predating the identity extension tests `len(params) == 2`, misses, and
    // falls back to
    // its 512-byte chunk floor.
    //
    // Emitting identity HERE is safe: configure_memory has run, but every
    // configure_* handler is pure function-pointer assignment and the VPP
    // regulator is not engaged until firestarter_operation_init, behind
    // op_wait_for_ack(). A host that reads this ack and refuses stops with the rail
    // still DOWN -- the compatibility gate cannot energise the part it protects.
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
        _ready[2] = 0xFE;  // REVISION_UNKNOWN -- the symbol lives inside that same #ifdef
#endif
        _ready[3] = _vlen;
        memcpy(_ready + 4, _ver, _vlen);
        uint16_t _budget = eprom_block_budget_s(handle->protocol, handle->pulse_delay,
                                                 (uint32_t)DATA_BUFFER_SIZE);
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
    rurp_set_communication_mode();
}

void loop() {
    if (handle.cmd != CMD_IDLE && timeout < millis()) {
        LOG_ERROR_ID_U8(MSG_ERR_CMD_TIMEOUT, handle.cmd);
        command_done(&handle);
    } else if (handle.cmd == CMD_IDLE) {
        if (rurp_communication_available() > 0) {
            /* COBS frame decode; there is no '{'-peek /
             * discard-non-'{' loop.
             *
             * rurp_communication_read_data() reads through the 0x00 delimiter,
             * COBS-decodes in place, verifies CRC8 BEFORE any JSON parse byte
             * is examined (V5 / ADR section 4.4), and on any
             * COBS/CRC/overflow failure calls _drain_to_delimiter() internally
             * and returns negative — NO additional drain logic needed here.
             *
             * Gate STRICTLY on n > 0: a zero-length decode is not a valid
             * command.  On n <= 0: log the frame error and stay CMD_IDLE
             * (bounded recovery is fully handled by the decoder). */
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
        case CMD_ERASE:
            finished = eprom_erase(&handle);
            break;
        case CMD_BLANK_CHECK:
            finished = eprom_blank_check(&handle);
            break;
        case CMD_CHECK_CHIP_ID:
            finished = eprom_check_chip_id(&handle);
            break;
        // Corrected form: with init/end left NULL
        // (configure_eeprom28c), these phases are NOT skipped --
        // _execute_operation_house_keeping_func still calls op_wait_for_ack()
        // and still emits the INIT and END frame pairs, so each costs a host
        // ACK round-trip. What is genuinely absent for a payload-free command
        // is the DONE round-trip (which lives only in
        // eprom_operations.cpp::_process_incoming_data, the write path) and
        // any '#' data frame. Traced shape: 4 host ACKs, 7 framed lines, zero
        // '#' frames, zero DONE string -- CMD_ERASE (above) is the working
        // precedent and the host's generic _run_state_machine already
        // supplies all four ACKs today, so no host change is needed for this
        // firmware half. Both arms sit outside any preprocessor conditional.
        // op_wait_for_ack has a 1000 ms timeout and emits MSG_ERR_TIMEOUT on
        // expiry, so a standalone lock issued by a host that does not ACK
        // times out rather than hangs (not this call site's concern).
        case CMD_SDP_UNLOCK:
            finished = eprom_sdp_unlock(&handle);
            break;
        case CMD_SDP_LOCK:
            finished = eprom_sdp_lock(&handle);
            break;
        // CMD_LOCK_STATUS, in the same one-line shape
        // as every other arm in this switch. eprom_lock_status is the
        // eprom_blank_check shape with no LOG_DEBUG_ID_SUB line -- see that
        // function's own comment for why. This sits outside every
        // preprocessor conditional, exactly like CMD_SDP_UNLOCK/CMD_SDP_LOCK
        // above.
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
