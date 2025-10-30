/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __BINARY_PROTOCOL_H__
#define __BINARY_PROTOCOL_H__

#include <stdint.h>
#include <stdbool.h>
#include "firestarter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINARY_MAGIC_BYTE 0xF5
#define BINARY_MAX_PAYLOAD_SIZE 1024  // 1K buffer

// Message Header (4 bytes)
typedef struct {
    uint8_t  magic;        // 0xF5 (magic byte for sync)
    uint8_t  cmd;          // Command ID (same as current CMD_* defines)
    uint16_t length;       // Payload length (little endian)
} __attribute__((packed)) binary_header_t;

// Command payload for memory operations
typedef struct {
    uint32_t address;      // Memory address
    uint32_t mem_size;     // Memory size  
    uint16_t chip_id;      // Chip ID
    uint16_t vpp_mv;       // VPP voltage in mV
    uint32_t pulse_delay;  // Pulse delay
    uint8_t  pins;         // Pin count
    uint8_t  mem_type;     // Memory type
    uint16_t ctrl_flags;   // Control flags (extended to 16 bits)
    bus_config_t bus_config; // Bus configuration
} __attribute__((packed)) binary_cmd_payload_t;

// Configuration payload
typedef struct {
    uint16_t ctrl_flags;   // Control flags (extended to 16 bits)
    uint8_t  hardware_revision;
    uint8_t  reserved;     // Padding
    uint32_t r1;           // R1 resistor value
    uint32_t r2;           // R2 resistor value
} __attribute__((packed)) binary_config_payload_t;

// Response structure  
typedef struct {
    uint8_t  magic;        // 0xF5
    uint8_t  status;       // Response status (OK/ERROR/WARNING/DATA)
    uint16_t length;       // Response data length
    // Variable length data follows
} __attribute__((packed)) binary_response_t;

// Protocol detection and parsing functions
bool is_binary_protocol(const uint8_t* buffer, size_t len);
bool parse_binary_command(const uint8_t* buffer, size_t len, firestarter_handle_t* handle);
bool parse_binary_config(const uint8_t* buffer, size_t len, rurp_configuration_t* config, firestarter_handle_t* handle);

// Response functions
void send_binary_response_with_data(uint8_t status, const void* data, uint16_t data_len);

// Protocol state
typedef enum {
    BINARY_STATE_WAITING_HEADER,
    BINARY_STATE_WAITING_PAYLOAD,
    BINARY_STATE_PROCESSING
} binary_protocol_state_t;

typedef struct {
    binary_protocol_state_t state;
    binary_header_t header;
    uint8_t* payload_buffer;    // Pointer to external buffer (reuse data_buffer)
    uint16_t bytes_received;
    uint16_t bytes_expected;
    uint16_t buffer_size;       // Size of external buffer
} binary_protocol_context_t;

// Protocol context management
void binary_protocol_init(binary_protocol_context_t* ctx, uint8_t* buffer, uint16_t buffer_size);
bool binary_protocol_process_byte(binary_protocol_context_t* ctx, uint8_t byte);
bool binary_protocol_has_complete_message(const binary_protocol_context_t* ctx);
const uint8_t* binary_protocol_get_message(const binary_protocol_context_t* ctx, size_t* len);
void binary_protocol_reset(binary_protocol_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // __BINARY_PROTOCOL_H__