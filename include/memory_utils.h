/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __MEMORY_UTILS_H__
#define __MEMORY_UTILS_H__
#include "firestarter.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WRITE_FLAG 0
#define READ_FLAG 1

uint32_t mem_util_remap_address_bus(const firestarter_handle_t* handle, uint32_t address, uint8_t read_write);
void mem_util_blank_check(firestarter_handle_t* handle);
/*
 * Debug session w27c512-write-slow-3x: exposed so eprom.cpp's
 * VERIFY_PER_PULSE_PLUS_FINAL arm can CALL the canonical full-block verify
 * instead of carrying a byte-identical copy of it. eprom.cpp's own comment
 * already said its copy "mirrors memory_verify_execute exactly: same
 * MSG_ERR_VERIFY id, same 5-byte payload, same early return" -- this
 * declaration turns that comment into a linkage. Definition stays in
 * src/proms/memory.cpp; it is the CMD_VERIFY operation_main there, so it was
 * already non-static and externally linkable.
 */
void memory_verify_execute(firestarter_handle_t* handle);
void mem_util_set_address(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_lsb_register(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_msb_register(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_top_address_register(firestarter_handle_t* handle, uint32_t address);

/*
 * 32-bit-safe microsecond delay. AVR's delayMicroseconds() takes a 16-bit
 * `unsigned int` and its 16 MHz arm computes `us <<= 2`, which overflows at
 * 16384 -- so the exact accurate ceiling is 16383, and any request above it
 * produces a MUCH SHORTER delay (e.g. 20000 -> ~3615 us), never a longer
 * one. Source: framework-arduino-avr 5.3.0 cores/arduino/wiring.c:120,
 * :167-183. delay() takes an `unsigned long` and is 32-bit safe, so
 * mem_util_delay_us splits a request into whole milliseconds via delay()
 * plus a sub-millisecond remainder via delayMicroseconds().
 *
 * mem_util_split_delay is exposed here (not made static) so a native test
 * case can assert the split arithmetic directly: the native stubs record
 * no elapsed time, only the arguments passed to the mocked delay /
 * delayMicroseconds calls, so the arithmetic is the only part a test off
 * real hardware can verify.
 */
void mem_util_report_voltage(firestarter_handle_t* handle, uint16_t measured_mv,
                             uint16_t expected_mv, uint8_t msg_id, uint8_t response_code);
void mem_util_report_chip_id(firestarter_handle_t* handle, uint16_t actual, bool warn_only);
void mem_util_split_delay(uint32_t us, uint32_t* out_ms, uint16_t* out_us);
void mem_util_delay_us(uint32_t us);

static inline bool using_p1_as_vpp(const firestarter_handle_t* handle) {
    return (handle->pins == 32 && handle->bus_config.vpp_line == VPP_P1_32_DIP) ||
           (handle->pins == 28 && handle->bus_config.vpp_line == VPP_P1_28_DIP) ||
           (handle->pins == 24 && handle->bus_config.vpp_line == VPP_P21_24_DIP);
}
#ifdef __cplusplus
}
#endif

#endif  // MEMORY_UTILS_H