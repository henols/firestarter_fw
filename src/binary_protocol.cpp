/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "binary_protocol.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>
#include "logging.h"
#include "firestarter.h"
#include "rurp_shield.h"
#include "rurp_serial_utils.h"

// Protocol detection
bool is_binary_protocol(const uint8_t* buffer, size_t len) {
    return (len > 0 && buffer[0] == BINARY_MAGIC_BYTE);
}

// Parse binary command
bool parse_binary_command(const uint8_t* buffer, size_t len, firestarter_handle_t* handle) {
    if (len < sizeof(binary_header_t)) {
        debug("Binary: Header too short");
        return false;
    }
    
    const binary_header_t* header = (const binary_header_t*)buffer;
    
    if (header->magic != BINARY_MAGIC_BYTE) {
        debug("Binary: Invalid magic byte");
        return false;
    }
    
    if (len < sizeof(binary_header_t) + header->length) {
        debug("Binary: Incomplete message");
        return false;
    }
    
    // Initialize handle defaults
    handle->cmd = header->cmd;
    handle->address = 0;
    handle->ctrl_flags = 0;
    handle->bus_config.rw_line = 0xFF;
    handle->bus_config.vpp_line = 0xFF;
    handle->bus_config.address_lines[0] = 0xFF;
    handle->bus_config.address_mask = 0;
    handle->chip_id = 0;
    handle->mem_size = 0;
    handle->vpp_mv = 12000; // Default 12V
    handle->pulse_delay = 1000; // Default 1ms
    handle->pins = 28; // Default 28 pins
    handle->mem_type = 0;
    
    // Parse payload if present
    if (header->length > 0) {
        const uint8_t* payload = buffer + sizeof(binary_header_t);
        
        if (header->length >= sizeof(binary_cmd_payload_t)) {
            const binary_cmd_payload_t* cmd_payload = (const binary_cmd_payload_t*)payload;
            
            handle->address = cmd_payload->address;
            handle->mem_size = cmd_payload->mem_size;
            handle->chip_id = cmd_payload->chip_id;
            handle->vpp_mv = cmd_payload->vpp_mv;
            handle->pulse_delay = cmd_payload->pulse_delay;
            handle->pins = cmd_payload->pins;
            handle->mem_type = cmd_payload->mem_type;
            handle->ctrl_flags = cmd_payload->ctrl_flags;
            
            // Copy bus configuration
            memcpy(&handle->bus_config, &cmd_payload->bus_config, sizeof(bus_config_t));
            
            debug_format("Binary: Parsed cmd=%d, addr=0x%lx, size=%lu", 
                        handle->cmd, handle->address, handle->mem_size);
        }
    }
    
    // Set default address mask if not configured
    if (handle->bus_config.address_lines[0] == 0xFF) {
        handle->bus_config.address_mask = 0xFFFF;
    }
    
    return true;
}

// Parse binary configuration
bool parse_binary_config(const uint8_t* buffer, size_t len, rurp_configuration_t* config, firestarter_handle_t* handle) {
    if (len < sizeof(binary_header_t)) {
        return false;
    }
    
    const binary_header_t* header = (const binary_header_t*)buffer;
    
    if (header->magic != BINARY_MAGIC_BYTE) {
        return false;
    }
    
    if (len < sizeof(binary_header_t) + header->length) {
        return false;
    }
    
    handle->cmd = header->cmd;
    handle->ctrl_flags = 0;
    
    if (header->length >= sizeof(binary_config_payload_t)) {
        const binary_config_payload_t* cfg_payload = 
            (const binary_config_payload_t*)(buffer + sizeof(binary_header_t));
        
        handle->ctrl_flags = cfg_payload->ctrl_flags;
        config->hardware_revision = cfg_payload->hardware_revision;
        config->r1 = cfg_payload->r1;
        config->r2 = cfg_payload->r2;
        
        debug_format("Binary config: flags=0x%x, r1=%lu, r2=%lu", 
                    handle->ctrl_flags, config->r1, config->r2);
        
        return true;
    }
    
    return false;
}

// Send simple binary response
void send_binary_response(uint8_t status, const char* message) {
    uint16_t msg_len = message ? strlen(message) : 0;
    
    binary_response_t response = {
        .magic = BINARY_MAGIC_BYTE,
        .status = status,
        .length = msg_len
    };
    
    SERIAL_PORT.write((uint8_t*)&response, sizeof(response));
    
    if (msg_len > 0) {
        SERIAL_PORT.write((const uint8_t*)message, msg_len);
    }
    SERIAL_PORT.flush();
}

// Send binary response with data
void send_binary_response_with_data(uint8_t status, const void* data, uint16_t data_len) {
    binary_response_t response = {
        .magic = BINARY_MAGIC_BYTE,
        .status = status,
        .length = data_len
    };
    
    SERIAL_PORT.write((uint8_t*)&response, sizeof(response));
    
    if (data_len > 0) {
        SERIAL_PORT.write((const uint8_t*)data, data_len);
    }
    SERIAL_PORT.flush();
}

// Send formatted binary response
void send_binary_response_format(uint8_t status, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < (int)sizeof(buffer)) {
        send_binary_response(status, buffer);
    } else {
        send_binary_response(status, "Format error");
    }
}

// Protocol context management
void binary_protocol_init(binary_protocol_context_t* ctx, uint8_t* buffer, uint16_t buffer_size) {
    ctx->state = BINARY_STATE_WAITING_HEADER;
    ctx->bytes_received = 0;
    ctx->bytes_expected = sizeof(binary_header_t);
    ctx->payload_buffer = buffer;
    ctx->buffer_size = buffer_size;
    memset(&ctx->header, 0, sizeof(ctx->header));
}

bool binary_protocol_process_byte(binary_protocol_context_t* ctx, uint8_t byte) {
    switch (ctx->state) {
        case BINARY_STATE_WAITING_HEADER:
            if (ctx->bytes_received == 0 && byte != BINARY_MAGIC_BYTE) {
                // Invalid magic byte, reset
                return false;
            }
            
            ((uint8_t*)&ctx->header)[ctx->bytes_received] = byte;
            ctx->bytes_received++;
            
            if (ctx->bytes_received >= sizeof(binary_header_t)) {
                // Header complete, check if we need payload
                if (ctx->header.length > 0) {
                    if (ctx->header.length > ctx->buffer_size) {
                        // Payload too large for buffer, reset
                        binary_protocol_reset(ctx);
                        return false;
                    }
                    ctx->state = BINARY_STATE_WAITING_PAYLOAD;
                    ctx->bytes_expected = ctx->header.length;
                    ctx->bytes_received = 0;
                } else {
                    // No payload, message complete
                    ctx->state = BINARY_STATE_PROCESSING;
                    return true;
                }
            }
            break;
            
        case BINARY_STATE_WAITING_PAYLOAD:
            if (ctx->bytes_received >= ctx->buffer_size) {
                // Buffer overflow protection
                binary_protocol_reset(ctx);
                return false;
            }
            ctx->payload_buffer[ctx->bytes_received] = byte;
            ctx->bytes_received++;
            
            if (ctx->bytes_received >= ctx->bytes_expected) {
                // Payload complete
                ctx->state = BINARY_STATE_PROCESSING;
                return true;
            }
            break;
            
        case BINARY_STATE_PROCESSING:
            // Should not receive bytes in this state
            return false;
    }
    
    return false; // Message not complete yet
}

bool binary_protocol_has_complete_message(const binary_protocol_context_t* ctx) {
    return ctx->state == BINARY_STATE_PROCESSING;
}

const uint8_t* binary_protocol_get_message(const binary_protocol_context_t* ctx, size_t* len) {
    if (ctx->state != BINARY_STATE_PROCESSING) {
        *len = 0;
        return NULL;
    }
    
    // For binary protocol, we can return the payload buffer directly since
    // the header has already been processed and the payload contains the command data
    *len = ctx->header.length;
    return ctx->payload_buffer;
}

void binary_protocol_reset(binary_protocol_context_t* ctx) {
    ctx->state = BINARY_STATE_WAITING_HEADER;
    ctx->bytes_received = 0;
    ctx->bytes_expected = sizeof(binary_header_t);
    memset(&ctx->header, 0, sizeof(ctx->header));
    // Keep payload_buffer and buffer_size as they were set during init
}