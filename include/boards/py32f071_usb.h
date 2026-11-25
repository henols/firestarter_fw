#pragma once

/*
 * The PY32F071 USB CDC transport, implemented in
 * platform/py32f071/src/usb_cdc.c. The serial-port backend
 * (platform/py32f071/src/serial_port.cpp) and the frame code in
 * src/boards/rurp_serial_utils.cpp use it.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void rurp_communication_begin(void);
int py32_usb_available(void);
int py32_usb_read(void);
int py32_usb_peek(void);
size_t py32_usb_read_bytes(char *buffer, size_t length);
size_t py32_usb_write(const uint8_t *buffer, size_t length);
void py32_usb_flush(void);
uint32_t py32_usb_generation(void);
void py32_usb_frame_begin(void);
void py32_usb_frame_end(void);

#ifdef __cplusplus
}
#endif
