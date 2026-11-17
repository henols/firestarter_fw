#ifndef __RURP_SERIAL_UTILS_H__
#define __RURP_SERIAL_UTILS_H__

#include "firestarter.h"
#include "rurp_platform_compat.h"

#ifndef SERIAL_PORT
#include <Arduino.h>
#define SERIAL_PORT Serial
#endif

// Board-agnostic ID-encoded frame emitter, defined in rurp_serial_utils.cpp.
// param_count is uint16_t so a MSG_DATA_CHUNK payload (512 / 1024 bytes) fits;
// the frame's u16 len field carries up to 65533 parameter bytes.
void _firestarter_emit_frame_wide(uint8_t id, const uint8_t* params, uint16_t param_count);

// Narrow form for rurp_log_id. The Uno strong override of rurp_log_id calls
// it after applying the com_mode gate. A uint8_t count always fits the frame.
static inline void _firestarter_emit_frame(uint8_t id, const uint8_t* params, uint8_t param_count) {
    _firestarter_emit_frame_wide(id, params, param_count);
}

void rurp_serial_begin(unsigned long baud);

void rurp_serial_end();

int rurp_communication_available();

int rurp_communication_read();

int rurp_communication_peak();

size_t rurp_communication_read_bytes(char* buffer, size_t size);

int rurp_communication_read_data(char* buffer, size_t cap);
size_t rurp_communication_write(const char* buffer, size_t size);

#endif  // __RURP_SERIAL_UTILS_H__
