#ifndef FIRESTARTER_TIMEOUT_UTILS_H
#define FIRESTARTER_TIMEOUT_UTILS_H

#include <stdbool.h>
#include <stdint.h>

// Valid for timeouts shorter than 2^31 ms. Subtraction stays well-defined
// when the 32-bit Arduino millis() counter wraps back to zero.
static inline bool fs_timeout_expired(uint32_t start_ms, uint32_t duration_ms, uint32_t now_ms) {
    return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

#endif  // FIRESTARTER_TIMEOUT_UTILS_H
