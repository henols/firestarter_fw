#include <Arduino.h>

#include "boards/py32f071_rurp_shield.h"

Py32SerialPort Serial;

extern "C" void _init(void)
{
}

void Py32SerialPort::begin(unsigned long baud)
{
    (void)baud;
    rurp_communication_begin();
}

void Py32SerialPort::end()
{
}

int Py32SerialPort::available() const
{
    return py32_usb_available();
}

int Py32SerialPort::read()
{
    return py32_usb_read();
}

int Py32SerialPort::peek() const
{
    return py32_usb_peek();
}

size_t Py32SerialPort::readBytes(char *buffer, size_t length)
{
    return py32_usb_read_bytes(buffer, length);
}

size_t Py32SerialPort::write(uint8_t value)
{
    return py32_usb_write(&value, 1U);
}

size_t Py32SerialPort::write(const uint8_t *buffer, size_t length)
{
    return py32_usb_write(buffer, length);
}

size_t Py32SerialPort::write(const char *buffer, size_t length)
{
    return py32_usb_write(reinterpret_cast<const uint8_t *>(buffer), length);
}

size_t Py32SerialPort::print(const char *text)
{
    return write(text, strlen(text));
}

size_t Py32SerialPort::println(const char *text)
{
    static const uint8_t newline[] = {'\r', '\n'};
    size_t written = print(text);
    written += py32_usb_write(newline, sizeof(newline));
    return written;
}

void Py32SerialPort::flush()
{
    py32_usb_flush();
}

Py32SerialPort::operator bool() const
{
    return true;
}
