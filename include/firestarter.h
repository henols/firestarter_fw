/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __FIRESTARTER_H__
#define __FIRESTARTER_H__

#include <stdbool.h>
#include <stdint.h>

#include "rurp_shield.h"

#ifndef DATA_BUFFER_SIZE
#define DATA_BUFFER_SIZE 512
#endif

// Value semantics for DEV_TOOLS, shared so the directive means the same thing
// on every target. Under a presence-based #ifdef, DEV_TOOLS=0 would enable
// dev tools on ARM.
//
// Must stay inside the include guard.
//
// This default is not in scope in include/dev_tools.h or src/dev_tools.cpp,
// which test DEV_TOOLS before including anything. They are correct anyway,
// because an undefined identifier evaluates to 0 in a preprocessor #if.
#ifndef DEV_TOOLS
#define DEV_TOOLS 0
#endif

/* CMD_FRAME_MAX: largest legitimate JSON command frame payload (bytes).
 * Worst-case JSON command is ~422 B; 512 B gives headroom.
 * Equals DATA_BUFFER_SIZE — the decoder's internal overflow cap.
 */
#define CMD_FRAME_MAX DATA_BUFFER_SIZE

/* FW identity string: "<version>:<board>" only.
 * Buffer capacity (DATA_BUFFER_SIZE) is advertised as a u16 bytes param on
 * every MSG_OK_READY ack. */
#define FW_VERSION VERSION ":" RURP_BOARD_NAME

#define TIMEOUT_MS 1000

#define CMD_IDLE 0
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_ERASE 3

// Ordinal 4 (the standalone blank-check command) was retired in 3.1.0. Never
// reuse it for a new command, flag or reserved meaning: an already-shipped
// host still composes it, and reassigning the number would make that host
// drive a different operation. A frame with ordinal 4 reaches the default arm of
// firestarter.cpp's dispatch switch, which answers MSG_ERR_UNKNOWN_CMD.

#define CMD_CHECK_CHIP_ID 5

// Ordinal 6 (the verify command) was retired in 3.1.0. Never reuse it for a
// new command, flag or reserved meaning, for the same reason as ordinal 4. A frame
// with ordinal 6 reaches the default arm of firestarter.cpp's dispatch switch,
// which answers MSG_ERR_UNKNOWN_CMD.

#if DEV_TOOLS
#define CMD_DEV_ADDRESS 7
#define CMD_DEV_REGISTER 8

// 18 is the next unused integer: 0-17 are allocated and ordinals 4 and 6 are
// retired and must never be reused. CMD_DEV_ADC exists only under
// -D DEV_TOOLS, which the beta channel injects and the stable build does not.
// It is not a memory command: it configures no protocol handler and is absent
// from is_memory_cmd() below.
#define CMD_DEV_ADC 17
#endif

// Standalone SDP (Software Data Protection) enable/disable commands on
// protocol 0x0D (eeprom_28c.cpp). Unlike CMD_READ/CMD_WRITE/etc they carry
// no payload and no MAIN/END round-trip. Both sit above CMD_DEV_ADDRESS (7),
// so admission to configure_memory() is decided by is_memory_cmd() (below)
// and not by an ordinal range.
// Both defines are unconditional: they are user-facing operations in every
// build.
#define CMD_SDP_UNLOCK 9
#define CMD_SDP_LOCK 10

#define CMD_READ_VPP 11
#define CMD_READ_VPE 12
#define CMD_FW_VERSION 13
#define CMD_CONFIG 14
#define CMD_HW_VERSION 15

// CMD_LOCK_STATUS is a memory command: the protection-status read is issued
// through handle->firestarter_get_data, a protocol-handler function pointer
// only configure_memory() sets, so it needs a protocol handler as
// CMD_READ/CMD_WRITE/etc do.
#define CMD_LOCK_STATUS 16

// Access-control gate: this decides which commands may call configure_memory()
// and so configure the hardware bus. configure_eprom(), reachable only
// through this gate, enables the 12 V VPP boost regulator, a hazard on a 5 V
// part. Admitting a command here is a hardware-safety decision.
//
// It lists the memory commands by name instead of testing an ordinal range.
// firestarter.cpp (the admission test in parse_json), json_parser.c (the
// algorithm requirement) and rurp_pinmap_guard.h use this predicate and do
// not list the set again.
//
// Hard constraints:
//  - No preprocessor conditional in this function's body. All seven named
//    macros are unconditionally defined.
//  - static inline, in this header. [env:native]'s build_src_filter compiles
//    only src/proms/, rurp_serial_utils.cpp and json_parser.c, so a
//    definition elsewhere would not link into the native test binary.
//  - It must not name CMD_DEV_ADDRESS or CMD_DEV_REGISTER: they do not exist
//    in a build without DEV_TOOLS.
static inline bool is_memory_cmd(uint8_t cmd) {
    switch (cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case CMD_ERASE:
        case CMD_CHECK_CHIP_ID:
        case CMD_SDP_UNLOCK:
        case CMD_SDP_LOCK:
        case CMD_LOCK_STATUS:
            return true;
        default:
            return false;
    }
}

#define RESPONSE_CODE_OK 1
#define RESPONSE_CODE_DATA 3
#define RESPONSE_CODE_WARNING 2
#define RESPONSE_CODE_ERROR 0

// Control flags
#define FLAG_FORCE 0x01
#define FLAG_CAN_ERASE 0x02
#define FLAG_SKIP_ERASE 0x04

// The skip-blank-check control flag, 0x08, was retired in 3.1.0. Never reuse
// this value for a new control flag: an already-shipped host still composes
// 0x08 on every `write -b` and on `dev test`'s masked UV slot writes, and
// reassigning the bit would turn on whatever new behaviour took the number.

#define FLAG_VPE_AS_VPP 0x10

#define FLAG_OUTPUT_ENABLE 0x20
#define FLAG_CHIP_ENABLE 0x40

#define FLAG_VERBOSE 0x80

// Declines the SDP (Software Data Protection) auto-unlock command sequence
// on protocol 0x0D (eeprom_28c.cpp), so a write against an SDP-protected
// AT28C part will not land -- an honest tradeoff reported via
// MSG_WARN_SDP_UNLOCK_SKIPPED rather than a silent no-op.
#define FLAG_SKIP_SDP_UNLOCK 0x100

#define is_flag_set(flag) \
    ((handle->ctrl_flags & flag) == flag)

#define ADDRESS_LINES_SIZE 20

typedef struct bus_config {
    uint8_t address_lines[ADDRESS_LINES_SIZE];  // Array mapping address lines
    uint32_t address_mask;                      // Mask for address lines
    uint8_t matching_lines;                     // Number of matching address lines
    uint8_t rw_line;                            // RW line mapping
    uint8_t vpp_line;                           // VPP line mapping
    uint32_t static_high_mask;                  // Bus lines unconditionally driven HIGH (e.g. CE2, tied-high NC pins)
} bus_config_t;

typedef struct firestarter_handle {
    uint8_t cmd;
    uint8_t operation_state;
    uint8_t response_code;
    uint8_t protocol;            /* largest dispatched value is 0x39 (PROTO_PHANTOM_0x39),
                                   * which fits uint8_t */
    uint8_t pins;
    uint32_t mem_size;
    uint32_t address;
    uint16_t vpp_mv;
    uint32_t pulse_delay;
    uint32_t read_settling_us;   /* address-settling delay before /CE assert (µs; 0 = no settling delay) */
    uint32_t read_strobe_us;     /* /CE read-strobe pulse width (µs; 0 = use default 3µs) */
    uint16_t ctrl_flags;         /* largest flag is 0x100 (FLAG_SKIP_SDP_UNLOCK). A flag above
                                   * 0xFFFF needs this field widened. */
    uint16_t chip_id;
    uint16_t page_size;          /* per-chip page-write size delivered by the host over the wire;
                                   * 0 = absent. Reset per command in json_parse. */
    uint32_t region_end;         /* absolute, EXCLUSIVE end address of the operation's region,
                                   * delivered by the host over the wire; 0 = absent. Unlike
                                   * page_size above, absent means the WHOLE DEVICE -- the
                                   * inversion is deliberate: page_size guards a destructive
                                   * write, so refusing on absence is the safe direction, while
                                   * this field guards a relaxation of an existing check, so
                                   * falling back to the stricter whole-device behaviour is the
                                   * safe direction instead. Reset per command in json_parse. */
    char data_buffer[DATA_BUFFER_SIZE];
    uint32_t data_size;
    bus_config_t bus_config;

    void (*firestarter_operation_init)(struct firestarter_handle*);
    void (*firestarter_operation_main)(struct firestarter_handle*);
    void (*firestarter_operation_end)(struct firestarter_handle*);

    void (*firestarter_set_data)(struct firestarter_handle*, uint32_t, uint8_t);
    uint8_t (*firestarter_get_data)(struct firestarter_handle*, uint32_t);

    void (*firestarter_set_address)(struct firestarter_handle*, uint32_t);

    void (*firestarter_set_control_register)(struct firestarter_handle*, rurp_register_t, bool);
    bool (*firestarter_get_control_register)(struct firestarter_handle*, rurp_register_t);

} firestarter_handle_t;

#endif  // __FIRESTARTER_H__