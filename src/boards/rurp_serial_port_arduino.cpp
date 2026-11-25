/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/*
 * The serial-port backend for the AVR boards and the host tests. It forwards
 * each call to the Arduino Serial object: HardwareSerial on the Uno, USB CDC
 * on the Leonardo, and the ArduinoFake mock in the native tests. Keep the
 * Serial overloads as they are: the native tests stub these overloads.
 */

#include <Arduino.h>

#include "rurp_serial_port.h"

void rurp_serial_port_begin(unsigned long baud) {
    Serial.begin(baud);
}

void rurp_serial_port_end(void) {
    Serial.end();
}

bool rurp_serial_port_ready(void) {
    return (bool)Serial;
}

int rurp_serial_port_available(void) {
    return Serial.available();
}

int rurp_serial_port_read(void) {
    return Serial.read();
}

int rurp_serial_port_peek(void) {
    return Serial.peek();
}

size_t rurp_serial_port_read_bytes(char* buffer, size_t length) {
    return Serial.readBytes(buffer, length);
}

size_t rurp_serial_port_write_byte(uint8_t value) {
    return Serial.write(value);
}

size_t rurp_serial_port_write(const uint8_t* buffer, size_t length) {
    return Serial.write(buffer, length);
}

void rurp_serial_port_flush(void) {
    Serial.flush();
}
