/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * suite-local copy of the ArduinoFake Serial.read /
 * available / peek queued-byte mock helper for the test_cobs_cmd_frame suite.
 *
 * This is a LOCAL copy (not the shared test_messages/serial_read_mock.h) so
 * that future changes to the messages-suite copy cannot silently affect the
 * cmd-frame suite and vice versa.  The platformio.ini -I path lists
 * test/native/avr/test_messages BEFORE test/native/avr/test_cobs_cmd_frame,
 * but PlatformIO also injects each suite's own directory into the compiler
 * search path — and since the compiler sees the suite-local directory first
 * at the translation-unit level (it is the directory of the including file),
 * this local copy takes precedence over the shared one.  Either way, keeping
 * a local copy is safer and makes the resolution explicit.
 *
 * No setup_serial_read_mock_finite() wrapper is needed — the existing
 * setup_serial_read_mock() already provides finite-stream semantics.
 *
 * Usage:
 *
 *   static std::vector<uint8_t> rx_queue;
 *   static size_t rx_pos;
 *
 *   setUp() {
 *       ArduinoFakeReset();
 *       ...
 *       rx_queue = { ... partial frame bytes (NO trailing 0x00) ... };
 *       rx_pos = 0;
 *       setup_serial_read_mock(rx_queue, rx_pos);
 *   }
 *
 * read()       — pops the front byte (advances rx_pos); returns -1 when empty.
 * available()  — returns remaining byte count; 0 when empty.
 * peek()       — returns front byte without consuming; -1 when empty.
 * readBytes()  — copies up to `length` bytes from the queue into `buffer`,
 *                returns the number of bytes copied.
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

    /* Serial.available() — returns remaining byte count; 0 when exhausted. */
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

    /* Serial.readBytes(char* buf, size_t len) — copies up to `len` bytes
     * from the queue into `buf`, advances pos, returns count copied. */
    When(Method(ArduinoFake(Serial), readBytes))
        .AlwaysDo([&queue, &pos](char* buf, size_t length) -> size_t {
            size_t count = 0;
            while (count < length && pos < queue.size()) {
                buf[count++] = (char)queue[pos++];
            }
            return count;
        });
}
