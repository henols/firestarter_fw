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
#ifndef DISABLE_JSON_SUPPORT
#include "json_parser.h"
#endif
#ifndef DISABLE_BINARY_PROTOCOL
#include "binary_protocol.h"
#endif
#include "logging.h"
#include "memory.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "version.h"
#ifdef DEV_TOOLS
#include "dev_tools.h"
#endif

#define RX 0
#define TX 1

bool init_programmer(firestarter_handle_t* handle);
#ifndef DISABLE_JSON_SUPPORT
bool parse_json(firestarter_handle_t* handle);
#endif
bool parse_protocol(firestarter_handle_t* handle);
void command_done(firestarter_handle_t* handle);

firestarter_handle_t handle;
#ifndef DISABLE_BINARY_PROTOCOL
binary_protocol_context_t binary_ctx;
bool use_binary_protocol = false;
#endif

unsigned long timeout = 0;

void setup() {
#ifdef SERIAL_DEBUG
    debug_msg_buffer = (char*)malloc(80);
    debug_setup();
#endif

    rurp_load_config();
#ifdef HARDWARE_REVISION
    rurp_detect_hardware_revision();
#endif
    rurp_board_setup();

    handle.cmd = CMD_IDLE;
#ifndef DISABLE_BINARY_PROTOCOL
    binary_protocol_init(&binary_ctx, (uint8_t*)handle.data_buffer, DATA_BUFFER_SIZE);
#endif
    debug("Firestarter started");
    debug_format("Firmware version: %s", VERSION);
    debug_format("Hardware revision: %d", rurp_get_physical_hardware_revision());
}

#ifndef DISABLE_JSON_SUPPORT
bool parse_json(firestarter_handle_t* handle) {
    debug("Parse JSON");
#ifdef EXTRA_INFO_LOGGING
    // log_info_format("'%s'", handle->data_buffer);

#endif

    jsmn_parser parser;
    static jsmntok_t tokens[NUMBER_JSNM_TOKENS];

    jsmn_init(&parser);
    int token_count = jsmn_parse(&parser, handle->data_buffer, handle->data_size, tokens, NUMBER_JSNM_TOKENS);
    handle->response_msg[0] = '\0';
    if (token_count <= 0) {
        handle->ctrl_flags = 0x80;
        log_info_format("Buf val: 0x%02x", handle->data_buffer[0]);
        log_error_const("Bad JSON");

        return false;
    }

    handle->cmd = json_get_cmd(handle->data_buffer, tokens, token_count, handle);
    log_info_format("Token count: %d", token_count);
    if (handle->cmd == 0xFF) {
        log_error_const("No cmd");
        return false;
    }

    debug_format("Cmd: %d", handle->cmd);
    if (handle->cmd < CMD_READ_VPP) {
        json_parse(handle->data_buffer, tokens, token_count, handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            log_error(handle->response_msg);
            return false;
        }
#ifdef DEV_TOOLS
        if (handle->cmd < CMD_DEV_ADDRESS) {
#endif
#ifdef EXTRA_INFO_LOGGING
            log_info_format("Force: %d", is_flag_set(FLAG_FORCE));
            log_info_format("Can erase: %d", is_flag_set(FLAG_CAN_ERASE));
            log_info_format("Skip erase: %d", is_flag_set(FLAG_SKIP_ERASE));
            log_info_format("Skip blank check: %d", is_flag_set(FLAG_SKIP_BLANK_CHECK));
            log_info_format("VPE as VPP: %d", is_flag_set(FLAG_VPE_AS_VPP));
#endif
            if (!op_execute_function(configure_memory, handle)) {
                log_error_const("Setup error");
                return false;
            }
#ifdef DEV_TOOLS
#ifdef EXTRA_INFO_LOGGING
        } else {
            log_info_format("Output enable: %d", is_flag_set(FLAG_OUTPUT_ENABLE));
            log_info_format("Chip enable: %d", is_flag_set(FLAG_CHIP_ENABLE));
#endif
        }
#endif
    } else if (handle->cmd == CMD_CONFIG) {
        rurp_configuration_t* config = rurp_get_config();
        int res = json_parse_config(handle->data_buffer, tokens, token_count, config, handle);
        if (res < 0) {
            log_error_const("Failed parsing config");
            return false;
        } else if (res == 1) {
            rurp_save_config(config);
        }
    }
    return true;
}
#endif

bool parse_protocol(firestarter_handle_t* handle) {
#if defined(DISABLE_JSON_SUPPORT) && defined(DISABLE_BINARY_PROTOCOL)
    #error "Cannot disable both JSON and binary protocols - at least one must be enabled"
#endif

#ifndef DISABLE_BINARY_PROTOCOL
    if (use_binary_protocol) {
        debug("Parse Binary Protocol");
        
        size_t msg_len;
        const uint8_t* message = binary_protocol_get_message(&binary_ctx, &msg_len);
        
        if (!message) {
            log_error_const("Failed to get binary message");
            return false;
        }
        
        // Check if this is a config command by accessing the header directly from context
        if (binary_ctx.header.cmd == CMD_CONFIG) {
            rurp_configuration_t* config = rurp_get_config();
            // For config commands, we need to parse with header info
            handle->cmd = binary_ctx.header.cmd;
            if (msg_len >= sizeof(binary_config_payload_t)) {
                const binary_config_payload_t* cfg_payload = (const binary_config_payload_t*)message;
                handle->ctrl_flags = cfg_payload->ctrl_flags;
                config->hardware_revision = cfg_payload->hardware_revision;
                config->r1 = cfg_payload->r1;
                config->r2 = cfg_payload->r2;
                rurp_save_config(config);
                return true;
            } else {
                log_error_const("Failed parsing binary config");
                return false;
            }
        }
        
        // Parse regular command - extract command from header and payload from message
        handle->cmd = binary_ctx.header.cmd;
        if (msg_len >= sizeof(binary_cmd_payload_t)) {
            const binary_cmd_payload_t* cmd_payload = (const binary_cmd_payload_t*)message;
            handle->address = cmd_payload->address;
            handle->mem_size = cmd_payload->mem_size;
            handle->chip_id = cmd_payload->chip_id;
            handle->vpp_mv = cmd_payload->vpp_mv;
            handle->pulse_delay = cmd_payload->pulse_delay;
            handle->pins = cmd_payload->pins;
            handle->mem_type = cmd_payload->mem_type;
            handle->ctrl_flags = cmd_payload->ctrl_flags;
            memcpy(&handle->bus_config, &cmd_payload->bus_config, sizeof(bus_config_t));
        } else {
            // No payload or smaller payload - use defaults
            handle->address = 0;
            handle->ctrl_flags = 0;
            handle->bus_config.rw_line = 0xFF;
            handle->bus_config.vpp_line = 0xFF;
            handle->bus_config.address_lines[0] = 0xFF;
            handle->bus_config.address_mask = 0xFFFF;
            handle->chip_id = 0;
            handle->mem_size = 0;
            handle->vpp_mv = 12000;
            handle->pulse_delay = 1000;
            handle->pins = 28;
            handle->mem_type = 0;
        }
        
        if (handle->cmd < CMD_READ_VPP) {
            if (!op_execute_function(configure_memory, handle)) {
                log_error_const("Setup error");
                return false;
            }
        }
        
        return true;
    } else 
#endif
    {
#ifndef DISABLE_JSON_SUPPORT
        return parse_json(handle);
#else
        log_error_const("JSON protocol disabled in this build");
        return false;
#endif
    }
}

bool init_programmer(firestarter_handle_t* handle) {
    handle->response_code = RESPONSE_CODE_OK;
    handle->operation_state = 0;

#ifndef DISABLE_BINARY_PROTOCOL
    if (use_binary_protocol) {
        // For binary protocol, the message is already processed
        handle->data_size = 0; // Binary protocol doesn't use data_buffer for parsing
    } else 
#endif
    {
        handle->data_size = rurp_communication_read_bytes(handle->data_buffer, DATA_BUFFER_SIZE);
    }
#ifdef EXTRA_INFO_LOGGING
    handle->ctrl_flags = 0x80;
    log_info_format("Buffer size: %d", handle->data_size);
#endif
#ifndef DISABLE_BINARY_PROTOCOL
    if (!use_binary_protocol && handle->data_size == 0) {
#else
    if (handle->data_size == 0) {
#endif
        log_error_const("Empty input");
        return false;
    }
    debug("Setup");
#ifndef DISABLE_BINARY_PROTOCOL
    if (!use_binary_protocol) {
#endif
        handle->data_buffer[handle->data_size] = '\0';
#ifndef DISABLE_BINARY_PROTOCOL
    }
#endif

    if (!parse_protocol(handle)) {
        return false;
    };

#ifdef EXTRA_INFO_LOGGING
    if (handle->cmd > CMD_IDLE && handle->cmd < CMD_READ_VPP) {
        log_info_format("Memory size 0x%lx", handle->mem_size);
        log_info_format("Address mask 0x%lx", handle->bus_config.address_mask);
        log_info_format("Matching lines %u", handle->bus_config.matching_lines);
    }
#endif
#ifdef HARDWARE_REVISION
#define PARSE_RESPONSE "FW: " FW_VERSION ", HW: Rev%d, Cmd: 0x%02x"
    send_ack_format(PARSE_RESPONSE, rurp_get_hardware_revision(), handle->cmd);
#else
#define PARSE_RESPONSE "FW: " FW_VERSION ", Cmd: 0x%02x"
    send_ack_format(PARSE_RESPONSE, handle->cmd);
#endif
    op_reset_timeout();
    return true;
}

void command_done(firestarter_handle_t* handle) {
    debug("Cmd finished");
    rurp_set_programmer_mode();
    rurp_chip_disable();
    rurp_write_to_register(CONTROL_REGISTER, 0x00);
    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, 0x00);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, 0x00);
    handle->cmd = CMD_IDLE;
    rurp_set_communication_mode();
    handle->response_msg[0] = '\0';
}

void loop() {
    if (handle.cmd != CMD_IDLE && timeout < millis()) {
        log_error_format_buf(handle.response_msg, "Cmd: %d, timeout", handle.cmd);
        command_done(&handle);
    } else if (handle.cmd == CMD_IDLE) {
        if (rurp_communication_available() > 0) {
            uint8_t byte = rurp_communication_peak();
            
            // Check if this looks like binary protocol
#ifndef DISABLE_BINARY_PROTOCOL
            if (byte == BINARY_MAGIC_BYTE) {
                // Try to collect a complete binary message
                while (rurp_communication_available() > 0) {
                    byte = rurp_communication_read();
                    if (binary_protocol_process_byte(&binary_ctx, byte)) {
                        // Complete message received
                        use_binary_protocol = true;
                        if (init_programmer(&handle)) {
                            return;
                        }
                        binary_protocol_reset(&binary_ctx);
                        break;
                    }
                }
            }
#endif
            // Look for the start of a JSON object '{' before trying to parse.
            // This makes the command reception more robust against spurious
            // characters on the serial line.
#ifndef DISABLE_BINARY_PROTOCOL
            else 
#endif
            if (byte == '{') {
#ifndef DISABLE_JSON_SUPPORT
#ifndef DISABLE_BINARY_PROTOCOL
                use_binary_protocol = false;
#endif
                if (init_programmer(&handle)) {
                    return;
                }
#else
                // JSON support disabled, discard JSON messages
                rurp_communication_read();
#endif
            } else {
                rurp_communication_read();  // Discard unknown character
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
            log_error_P_int_buf(handle.response_msg, "Unknown cmd: ", handle.cmd);
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
