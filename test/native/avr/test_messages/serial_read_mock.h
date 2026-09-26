/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * ArduinoFake Serial.read / available / peek
 * queued-byte mock helper (RED scaffold).
 *
 * Provides a single helper function that wires ArduinoFake's Serial mock to a
 * shared std::vector<uint8_t> front-cursor so a full COBS frame stream can be
 * driven into rurp_communication_read_data() without a real UART.
 *
 * Usage:
 *
 *   static std::vector<uint8_t> rx_queue;
 *   static size_t rx_pos;
 *
 *   setUp() {
 *       ArduinoFakeReset();
 *       ...
 *       rx_queue = { ... frame bytes ... };
 *       rx_pos = 0;
 *       setup_serial_read_mock(rx_queue, rx_pos);
 *   }
 *
 * Mirrors the write-mock cadence in test_rurp_log_id.cpp (setUp lines 52-67).
 * Uses OverloadedMethod to resolve the single-byte read/peek overloads.
 *
 * read()       — pops the front byte (advances rx_pos); returns -1 when empty.
 * available()  — returns (int)(rx_queue.size() - rx_pos).
 * peek()       — returns front byte without consuming; -1 when empty.
 * readBytes()  — copies up to `length` bytes from the queue into `buffer`,
 *                returns the number of bytes copied (for the legacy
 *                len_u16+XOR decoder used by rurp_communication_read_bytes).
 */

#pragma once

#include <Arduino.h>
#include <ArduinoFake.h>

#include <cstdint>
#include <cstddef>
#include <vector>

using namespace fakeit;

/**
 * Wire ArduinoFake's Serial.read / available / peek to a shared queued-byte
 * vector driven by a front-cursor index.
 *
 * @param queue  The byte vector to drain on reads.
 * @param pos    Reference to the current read position (modified by lambdas
 *               via capture; caller must keep `pos` in scope for the mock
 *               lifetime).
 *
 * Must be called AFTER ArduinoFakeReset() so any prior mock state is cleared.
 */
static void setup_serial_read_mock(const std::vector<uint8_t>& queue, size_t& pos) {
    /* Serial.read() — single-byte overload, returns int.
     * Pops front byte; returns -1 when the queue is exhausted. */
    When(OverloadedMethod(ArduinoFake(Serial), read, int(void)))
        .AlwaysDo([&queue, &pos]() -> int {
            if (pos >= queue.size()) {
                return -1;
            }
            return (int)(uint8_t)queue[pos++];
        });

    /* Serial.available() — returns remaining byte count. */
    When(Method(ArduinoFake(Serial), available))
        .AlwaysDo([&queue, &pos]() -> int {
            int remaining = (int)(queue.size() - pos);
            return (remaining > 0) ? remaining : 0;
        });

    /* Serial.peek() — single-byte overload, returns int.
     * Returns front byte without advancing pos; -1 when empty. */
    When(OverloadedMethod(ArduinoFake(Serial), peek, int(void)))
        .AlwaysDo([&queue, &pos]() -> int {
            if (pos >= queue.size()) {
                return -1;
            }
            return (int)(uint8_t)queue[pos];
        });

    When(Method(ArduinoFake(Serial), readBytes))
        .AlwaysDo([&queue, &pos](char* buf, size_t length) -> size_t {
            size_t count = 0;
            while (count < length && pos < queue.size()) {
                buf[count++] = (char)queue[pos++];
            }
            return count;
        });
}
