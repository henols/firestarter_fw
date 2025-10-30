#ifndef __RURP_SERIAL_UTILS_H__
#define __RURP_SERIAL_UTILS_H__

#include "firestarter.h"
#include <avr/pgmspace.h>

#include "firestarter.h"
#include "logging.h"
#ifndef SERIAL_PORT
#include <Arduino.h>
#define SERIAL_PORT Serial
#endif
#ifdef SERIAL_DEBUG
extern char* debug_msg_buffer;
#endif

// --- Core Logging Functions ---
// Core logging function for RAM messages. Takes type from PROGMEM.
void _firestarter_log_ram(PGM_P type, const char* msg);

// Core logging function for PROGMEM messages.
void _firestarter_log_progmem(PGM_P type, PGM_P p_msg);

void rurp_serial_begin(unsigned long baud);

void rurp_serial_end();

int rurp_communication_available();

int rurp_communication_read();

int rurp_communication_peak();

size_t rurp_communication_read_bytes(char* buffer, size_t size);

int rurp_communication_read_data(char* buffer);
size_t rurp_communication_write(const char* buffer, size_t size);

#endif  // __RURP_SERIAL_UTILS_H__