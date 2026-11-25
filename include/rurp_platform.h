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
#include <Arduino.h>

#define RURP_DELAY_US(value) delayMicroseconds(value)
#define RURP_DELAY_MS(value) delay(value)
#define RURP_MILLIS() millis()
#define RURP_MICROS() micros()

#else

/*
 * Host builds: the PlatformIO native tests and the tests/ harness. There,
 * <Arduino.h> is a test fake (ArduinoFake, or the header that the harness
 * writes), so the tests can stub and count the timing calls. An embedded
 * target that is not named above has no <Arduino.h> and fails here.
 */
#define RURP_PLATFORM_HOST 1
#include <Arduino.h>

#define RURP_DELAY_US(value) delayMicroseconds(value)
#define RURP_DELAY_MS(value) delay(value)
#define RURP_MILLIS() millis()
#define RURP_MICROS() micros()

#endif
