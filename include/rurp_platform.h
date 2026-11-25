#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(RURP_PLATFORM_PY32F071) || defined(PY32F071xB)

#ifndef RURP_PLATFORM_PY32F071
#define RURP_PLATFORM_PY32F071 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t rurp_millis(void);
uint32_t rurp_micros(void);
void rurp_delay_ms(uint32_t milliseconds);
void rurp_delay_us(uint32_t microseconds);

#ifdef __cplusplus
}
#endif

#define RURP_DELAY_US(value) rurp_delay_us((uint32_t)(value))
#define RURP_DELAY_MS(value) rurp_delay_ms((uint32_t)(value))
#define RURP_MILLIS() rurp_millis()
#define RURP_MICROS() rurp_micros()

#elif defined(__AVR__)

#define RURP_PLATFORM_AVR 1

/* Declare the Arduino AVR core functions that shared code uses here, not
 * through <Arduino.h>, so that shared code cannot use other Arduino names.
 * The declarations are the same as those in the core's Arduino.h. The core's
 * main() calls setup() and loop() with C linkage. */
#ifdef __cplusplus
extern "C" {
#endif

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void setup(void);
void loop(void);

#ifdef __cplusplus
}
#endif

#define RURP_DELAY_US(value) delayMicroseconds(value)
#define RURP_DELAY_MS(value) delay(value)
#define RURP_MILLIS() millis()
#define RURP_MICROS() micros()

#elif defined(ARDUINO)

/* Every Arduino core defines ARDUINO. Only the AVR core is supported. */
#error "Unsupported Firestarter target platform"

#else

/*
 * Host builds: the PlatformIO native tests and the tests/ harness. There,
 * <Arduino.h> is a test fake (ArduinoFake, or the header that the harness
 * writes), so the tests can stub and count the timing calls.
 */
#include <Arduino.h>

#define RURP_DELAY_US(value) delayMicroseconds(value)
#define RURP_DELAY_MS(value) delay(value)
#define RURP_MILLIS() millis()
#define RURP_MICROS() micros()

#endif
