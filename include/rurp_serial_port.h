/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __RURP_SERIAL_PORT_H__
#define __RURP_SERIAL_PORT_H__

/*
 * The byte transport to the host. Shared code uses only these functions and
 * never a board serial object. Each target supplies one backend:
 *
 *   AVR boards and host tests: src/boards/rurp_serial_port_arduino.cpp
 *   PY32F071:                  platform/py32f071/src/serial_port.cpp
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rurp_serial_port_begin(unsigned long baud);
void rurp_serial_port_end(void);
// True when the port can carry data. Leonardo USB CDC: the host opened it.
bool rurp_serial_port_ready(void);
int rurp_serial_port_available(void);
int rurp_serial_port_read(void);
int rurp_serial_port_peek(void);
size_t rurp_serial_port_read_bytes(char* buffer, size_t length);
size_t rurp_serial_port_write_byte(uint8_t value);
size_t rurp_serial_port_write(const uint8_t* buffer, size_t length);
void rurp_serial_port_flush(void);

#ifdef __cplusplus
}
#endif

#endif  // __RURP_SERIAL_PORT_H__
