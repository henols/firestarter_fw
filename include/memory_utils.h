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
/* The shared final-pass read-and-compare that eprom.cpp's
 * VERIFY_PER_PULSE_PLUS_FINAL arm calls after its per-pulse loop converges,
 * instead of carrying a byte-identical copy. Defined in
 * src/proms/memory.cpp. Its command surface -- CMD_VERIFY, ordinal 6 -- was
 * retired in 3.1.0 (Phase 204); the function itself was deliberately kept,
 * because it is still the eprom program loop's only final-pass verify. */
void memory_verify_execute(firestarter_handle_t* handle);
/* The single resolution point for D-04's "0 = absent = whole device"
 * fallback and for the fail-closed clamp: returns handle->mem_size when
 * handle->region_end is 0 OR exceeds handle->mem_size, else returns
 * handle->region_end. Deliberately a function rather than an inline
 * ternary -- a ternary written inside eprom.cpp would add a row to the
 * branch-inventory golden (tests/golden/protocol_branch_inventory.json),
 * a function call in a plain assignment does not. Both eprom_operations.cpp
 * and eprom.cpp call it. Defined in src/proms/memory.cpp. */
uint32_t mem_util_operation_end(const firestarter_handle_t* handle);
/* The region-scoped blank check. mem_util_blank_check (above) is now a
 * one-line wrapper over this passing (0, handle->mem_size), so the two
 * cannot drift -- there is exactly one scan body. Defined in
 * src/proms/memory.cpp. */
void mem_util_blank_check_region(firestarter_handle_t* handle, uint32_t start, uint32_t end);
void mem_util_set_address(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_lsb_register(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_msb_register(firestarter_handle_t* handle, uint32_t address);
rurp_register_t mem_util_calculate_top_address_register(firestarter_handle_t* handle, uint32_t address);

/*
 * 32-bit-safe microsecond delay.
 *
 * AVR's delayMicroseconds() takes a 16-bit unsigned int and its 16 MHz arm
 * computes `us <<= 2`, which overflows at 16384 -- so the accurate ceiling is
 * 16383, and any request above it produces a MUCH SHORTER delay (20000 ->
 * ~3615 us), never a longer one. [framework-arduino-avr 5.3.0
 * cores/arduino/wiring.c]
 *
 * So this splits a request into whole milliseconds via delay(), which is
 * 32-bit safe, plus a sub-millisecond remainder via delayMicroseconds().
 */
void mem_util_split_delay(uint32_t us, uint32_t* out_ms, uint16_t* out_us);
void mem_util_delay_us(uint32_t us);

/* Shared VPP-mismatch report. Defined in src/proms/memory.cpp.
 *
 * The parameter widths are part of the contract: both are uint16_t so `(x+50)`
 * promotes to 16-bit on AVR and the division stays on __udivmodhi4. Do not
 * widen either. */
void mem_util_report_voltage(firestarter_handle_t* handle, uint16_t measured_mv,
                              uint16_t expected_mv, uint8_t msg_id, uint8_t response_code);

/*
 * Retires four chip-ID mismatch blocks across four translation units
 * (flash_utils.cpp, flash_intel.cpp, eprom.cpp, eeprom_28c.cpp). Definition
 * lives in src/proms/memory.cpp. warn_only is a parameter, not an internal
 * is_flag_set(FLAG_FORCE) check, because eprom.cpp's standalone
 * CMD_CHECK_CHIP_ID path must keep refusing unconditionally regardless of
 * --force -- do not fold the force test into this helper.
 */
void mem_util_report_chip_id(firestarter_handle_t* handle, uint16_t actual, bool warn_only);

static inline bool using_p1_as_vpp(const firestarter_handle_t* handle) {
    return (handle->pins == 32 && handle->bus_config.vpp_line == VPP_P1_32_DIP) ||
           (handle->pins == 28 && handle->bus_config.vpp_line == VPP_P1_28_DIP) ||
           (handle->pins == 24 && handle->bus_config.vpp_line == VPP_P21_24_DIP);
}
#ifdef __cplusplus
}
#endif

#endif  // MEMORY_UTILS_H