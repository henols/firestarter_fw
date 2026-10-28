#ifndef __RURP_SERIAL_UTILS_H__
#define __RURP_SERIAL_UTILS_H__

#include "firestarter.h"
#include "messages.h"
#include "rurp_platform_compat.h"

#ifndef SERIAL_PORT
#include <Arduino.h>
#define SERIAL_PORT Serial
#endif

// Board-agnostic ID-encoded frame emitter, defined in rurp_serial_utils.cpp.
// The frame parameters are head[0..head_len) then params[0..param_count).
// param_count is uint16_t so a MSG_DATA_CHUNK payload (512 / 1024 bytes) fits;
// the frame's u16 len field carries up to 65533 parameter bytes.
void _firestarter_emit_frame_parts(uint8_t id, const uint8_t* head, uint8_t head_len,
                                   const uint8_t* params, uint16_t param_count);

static inline void _firestarter_emit_frame_wide(uint8_t id, const uint8_t* params, uint16_t param_count) {
    _firestarter_emit_frame_parts(id, NULL, 0, params, param_count);
}

// MSG_DATA_CHUNK: [address u32 BE][data]. The address is the chip address of data[0].
static inline void _firestarter_emit_data_chunk(uint32_t address, const uint8_t* data, uint16_t size) {
    const uint8_t head[4] = {
        (uint8_t)(address >> 24),
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address,
    };
    _firestarter_emit_frame_parts(MSG_DATA_CHUNK, head, 4, data, size);
}

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
