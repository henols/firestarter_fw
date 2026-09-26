#ifndef FIRESTARTER_PY32F071_ARDUINO_COMPAT_H
#define FIRESTARTER_PY32F071_ARDUINO_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rurp_platform.h"
#include "rurp_platform_compat.h"

using byte = uint8_t;

#ifndef HIGH
#define HIGH 1U
#endif
#ifndef LOW
#define LOW 0U
#endif

static inline void delayMicroseconds(uint32_t value)
{
    RURP_DELAY_US(value);
}

static inline void delay(uint32_t value)
{
    RURP_DELAY_MS(value);
}

static inline unsigned long millis(void)
{
    return (unsigned long)RURP_MILLIS();
}

static inline unsigned long micros(void)
{
    return (unsigned long)RURP_MICROS();
}

#ifdef __cplusplus
extern "C" {
#endif

int py32_usb_available(void);
int py32_usb_read(void);
int py32_usb_peek(void);
size_t py32_usb_read_bytes(char *buffer, size_t length);
size_t py32_usb_write(const uint8_t *buffer, size_t length);
void py32_usb_flush(void);

#ifdef __cplusplus
}

class Py32SerialPort
{
public:
    void begin(unsigned long baud);
    void end();
    int available() const;
    int read();
    int peek() const;
    size_t readBytes(char *buffer, size_t length);
    size_t write(uint8_t value);
    size_t write(const uint8_t *buffer, size_t length);
    size_t write(const char *buffer, size_t length);
    size_t print(const char *text);
    size_t println(const char *text);
    void flush();
    explicit operator bool() const;
};

extern Py32SerialPort Serial;
#endif

#endif
