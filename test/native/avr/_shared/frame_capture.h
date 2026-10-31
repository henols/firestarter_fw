/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Captures the rurp_log_id() frames that the firmware writes to Serial, and
 * gives the message ids of the captured frames.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoFake.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace fakeit;

/* Every byte the firmware wrote with Serial.write(uint8_t). */
static std::vector<uint8_t> captured_frames;

/* Routes every single-byte Serial.write into captured_frames. Call it after
 * ArduinoFakeReset(). */
static inline void capture_serial_writes() {
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured_frames.push_back(b);
            return (size_t)1;
        });
}

/* Walks captured_frames with rurp_log_id()'s fixed wire layout (4-byte magic,
 * 2-byte big-endian length, 1 id byte, params, 1 crc byte, 1 anchor byte) and
 * appends each frame's id byte, in order, to *out_ids. The length field alone
 * finds the next frame, so no message-id lookup table is needed. */
static inline void captured_frame_ids(std::vector<uint8_t>* out_ids) {
    size_t offset = 0;
    while (offset + 7 <= captured_frames.size()) {
        uint16_t len_value = (uint16_t)(((uint16_t)captured_frames[offset + 4] << 8) | captured_frames[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len_value + 1;
        if (offset + frame_size > captured_frames.size()) {
            break; /* incomplete trailing frame -- not expected in these cases */
        }
        out_ids->push_back(captured_frames[offset + 6]);
        offset += frame_size;
    }
}

/* Membership check over an already-enumerated id list. */
static inline bool ids_contains(const std::vector<uint8_t>& ids, uint8_t id) {
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}
