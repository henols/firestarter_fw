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
void mem_util_set_read_high_mask(firestarter_handle_t* handle);
/* Shared final read-and-compare pass after pulse verification converges.
 * Required by the EPROM program path regardless of standalone commands. */
void memory_verify_execute(firestarter_handle_t* handle);
/* Resolve the exclusive operation end: region_end == 0 or > mem_size
 * means the whole device; otherwise use region_end. The clamp prevents
 * an invalid host-supplied range from extending beyond the device. */
uint32_t mem_util_operation_end(const firestarter_handle_t* handle);

/* Validate a half-open memory range without forming address + size.
 * An empty transfer at the exclusive end is valid. */
static inline bool mem_util_range_fits(uint32_t address, uint32_t size, uint32_t end) {
    return address <= end && size <= end - address;
}
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

/* Shared VPP-mismatch report, in millivolts. Defined in src/proms/memory.cpp. */
void mem_util_report_voltage(firestarter_handle_t* handle, uint16_t measured_mv,
                              uint16_t expected_mv, uint8_t msg_id, uint8_t response_code);

/*
 * Refuse a voltage-gated operation when the stored divider calibration cannot
 * be evaluated. Returns true when it refused, having emitted
 * MSG_ERR_CALIBRATION and set RESPONSE_CODE_ERROR.
 *
 * Call this BEFORE asserting any high-voltage bit. Without it, an unusable
 * calibration makes rurp_read_voltage_mv return 0, which the low-side test
 * reads as "the rail is low" -- a WARNING, so the write proceeds against a
 * rail nothing has measured.
 */
bool mem_util_refuse_bad_calibration(firestarter_handle_t* handle);

/* Apply the common measured-VPP acceptance window and report the matching
 * error code. Shared by the EPROM, Intel flash and AT28C chip-ID paths. */
void mem_util_check_vpp_window(firestarter_handle_t* handle, uint16_t vpp_mv);

/*
 * Warn, once per voltage-gated operation, when this board has never been
 * calibrated. Its readings then carry the full per-die bandgap spread, up to
 * 10 %, which is three times the acceptance window -- so the verdict that
 * follows is not trustworthy in either direction. A warning, never a refusal:
 * it reports a limit on what the firmware knows, not a hardware fault.
 */
void mem_util_warn_if_uncalibrated(firestarter_handle_t* handle);

/*
 * Report a chip-ID mismatch. warn_only is a parameter, not an internal
 * is_flag_set(FLAG_FORCE) check, because eprom.cpp's standalone
 * CMD_CHECK_CHIP_ID path must keep refusing regardless of --force. Definition
 * lives in src/proms/memory.cpp.
 */
void mem_util_report_chip_id(firestarter_handle_t* handle, uint16_t actual, bool warn_only);

static inline bool using_p1_as_vpp(const firestarter_handle_t* handle) {
    return (handle->pins == 32 && handle->bus_config.vpp_line == VPP_P1_32_DIP) ||
           (handle->pins == 28 && handle->bus_config.vpp_line == VPP_P1_28_DIP) ||
           (handle->pins == 24 && handle->bus_config.vpp_line == VPP_P21_24_DIP);
}

/*
 * True when driving the VPP bus line high also switches a VPP route onto the
 * socket. Lines 0-15 reach the socket from the address latches through a
 * Schottky diode, so they are logic levels on every revision. A line above
 * them is a control-register bit: on Rev 0/1 line 21 is the A18 latch bit,
 * and on Rev 2.x it is the same bit as the pin-1 VPP switch.
 */
static inline bool vpp_line_switches_vpp(const firestarter_handle_t* handle) {
    uint8_t line = handle->bus_config.vpp_line;
    if (line < RURP_CTRL_ADDRESS_SHIFT) {
        return false;
    }
    rurp_register_t bit = (rurp_register_t)1 << (line - RURP_CTRL_ADDRESS_SHIFT);
#ifdef HARDWARE_REVISION
    return (rurp_map_ctrl_reg_for_hardware_revision(bit) & RURP_SOCKET_VPP_ROUTE_BITS) != 0;
#else
    return (bit & RURP_SOCKET_VPP_ROUTE_BITS) != 0;
#endif
}
#ifdef __cplusplus
}
#endif

#endif  // MEMORY_UTILS_H