#ifndef __RURP_SERIAL_UTILS_H__
#define __RURP_SERIAL_UTILS_H__

#include "firestarter.h"
#include "rurp_platform_compat.h"

#ifndef SERIAL_PORT
#include <Arduino.h>
#define SERIAL_PORT Serial
#endif

// Board-agnostic ID-encoded frame emitter.
// Defined in rurp_serial_utils.cpp; the Uno strong override of rurp_log_id
// calls into this after applying the com_mode gate.
void _firestarter_emit_frame(uint8_t id, const uint8_t* params, uint8_t param_count);

// Wide variant for large payloads (e.g. MSG_DATA_CHUNK up to
// 512 / 1024 bytes). param_count is uint16_t so 512-byte chunks don't overflow
// the loop counter; the frame's u16 len field already supports this.
void _firestarter_emit_frame_wide(uint8_t id, const uint8_t* params, uint16_t param_count);

void rurp_serial_begin(unsigned long baud);

void rurp_serial_end();

int rurp_communication_available();

int rurp_communication_read();

int rurp_communication_peak();

size_t rurp_communication_read_bytes(char* buffer, size_t size);

int rurp_communication_read_data(char* buffer, size_t cap);
size_t rurp_communication_write(const char* buffer, size_t size);

#endif  // __RURP_SERIAL_UTILS_H__
