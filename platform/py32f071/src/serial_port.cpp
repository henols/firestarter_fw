/*
 * The serial-port backend for PY32F071: USB CDC through usb_cdc.c.
 */

#include "boards/py32f071_usb.h"
#include "rurp_serial_port.h"

void rurp_serial_port_begin(unsigned long baud)
{
    (void)baud;
    rurp_communication_begin();
}

void rurp_serial_port_end(void)
{
}

bool rurp_serial_port_ready(void)
{
    return true;
}

int rurp_serial_port_available(void)
{
    return py32_usb_available();
}

int rurp_serial_port_read(void)
{
    return py32_usb_read();
}

int rurp_serial_port_peek(void)
{
    return py32_usb_peek();
}

size_t rurp_serial_port_read_bytes(char *buffer, size_t length)
{
    return py32_usb_read_bytes(buffer, length);
}

size_t rurp_serial_port_write_byte(uint8_t value)
{
    return py32_usb_write(&value, 1U);
}

size_t rurp_serial_port_write(const uint8_t *buffer, size_t length)
{
    return py32_usb_write(buffer, length);
}

void rurp_serial_port_flush(void)
{
    py32_usb_flush();
}
