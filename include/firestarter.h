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
// on every target. Under a presence-based #ifdef, DEV_TOOLS=0 would perversely
// ENABLE dev tools on ARM.
//
// Must stay INSIDE the include guard: above it, the host parity test's guard
// detection misidentifies the real guard.
//
// Not syntactically in scope in include/dev_tools.h or src/dev_tools.cpp, which
// test DEV_TOOLS before including anything. They are still correct, because an
// undefined identifier evaluates to 0 in a preprocessor #if -- but enabling
// -Wundef would require pulling this default into a header they include.
#ifndef DEV_TOOLS
#define DEV_TOOLS 0
#endif

/* CMD_FRAME_MAX: largest legitimate JSON command frame payload (bytes).
 * Worst-case JSON command is ~422 B; 512 B gives headroom.
 * Equals DATA_BUFFER_SIZE — the decoder's internal overflow cap.
 * Mirrors constants.py CMD_FRAME_MAX per CLAUDE.md parity. */
#define CMD_FRAME_MAX DATA_BUFFER_SIZE

/* FW identity string: "<version>:<board>" only.
 * Buffer capacity (DATA_BUFFER_SIZE) is no longer carried in the identity
 * string — it is advertised as a u16 bytes param on every MSG_OK_READY ack. */
#define FW_VERSION VERSION ":" RURP_BOARD_NAME

#define TIMEOUT_MS 1000

#define CMD_IDLE 0
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_ERASE 3

// Ordinal 4 -- the standalone blank-check command -- retired in 3.1.0
// (Phase 204). The host side -- firestarter_app/firestarter/constants.py's
// COMMAND_BLANK_CHECK and its COMMAND_NAMES row -- was retired in the same
// commit pair. This ordinal must NEVER be reused for any new command, flag
// or reserved meaning: an already-shipped host still composes it, and
// reassigning the number would make that stale host silently drive a
// different operation. A frame carrying ordinal 4 now falls through
// firestarter.cpp's dispatch switch default: arm, which answers
// MSG_ERR_UNKNOWN_CMD rather than running any handler.

#define CMD_CHECK_CHIP_ID 5

// Ordinal 6 -- the verify command -- retired in 3.1.0 (Phase 204). The host
// side -- firestarter_app/firestarter/constants.py's COMMAND_VERIFY and its
// COMMAND_NAMES row -- was retired in the same commit pair. This ordinal
// must NEVER be reused for any new command, flag or reserved meaning: an
// already-shipped host still composes it, and reassigning the number would
// make that stale host silently drive a different operation. A frame
// carrying ordinal 6 now falls through firestarter.cpp's dispatch switch
// default: arm, which answers MSG_ERR_UNKNOWN_CMD rather than running any
// handler.

#if DEV_TOOLS
#define CMD_DEV_ADDRESS 7
#define CMD_DEV_REGISTER 8
#endif

// Standalone SDP (Software Data Protection) enable/disable commands on
// protocol 0x0D (eeprom_28c.cpp). Unlike CMD_READ/CMD_WRITE/etc these carry
// no payload and no MAIN/END round-trip. Slots 9 and 10
// were the only two free command values, and BOTH sit above
// CMD_DEV_ADDRESS (7): that is exactly why the #ifdef DEV_TOOLS-conditional
// ordinal admission guard had to be replaced by is_memory_cmd() (below)
// BEFORE these could be defined -- with the old guard in place, a
// DEV_TOOLS build would have
// routed 9/10 into the dev-tools branch and never called configure_memory,
// leaving the new commands with no bus configuration at all.
// Both defines are UNCONDITIONAL (never DEV_TOOLS-gated): they are real
// user-facing operations in every build.
#define CMD_SDP_UNLOCK 9
#define CMD_SDP_LOCK 10

#define CMD_READ_VPP 11
#define CMD_READ_VPE 12
#define CMD_FW_VERSION 13
#define CMD_CONFIG 14
#define CMD_HW_VERSION 15

// 16 is the next unused integer -- no slot below 11 was free
// (CMD_READ_VPP..CMD_HW_VERSION occupy 11-15, and slots 9 and 10 were the
// only two free command values below that, per the
// CMD_SDP_UNLOCK/CMD_SDP_LOCK comment above). This IS a memory command: the
// protection-status read is issued through handle->firestarter_get_data,
// a protocol-handler function pointer only configure_memory() sets, so it
// needs a protocol handler exactly as CMD_READ/CMD_WRITE/etc do.
#define CMD_LOCK_STATUS 16

// ACCESS-CONTROL GATE, not hygiene: this decides which commands may call
// configure_memory() and so configure the hardware bus. configure_eprom(),
// reachable only through this gate, enables the 12V VPP boost regulator --
// a hazard on a 5V part. Admitting a command here is a hardware-safety
// decision.
//
// It enumerates the memory commands BY NAME rather than testing an ordinal
// range, which is what lets it be unconditional: the old guard had to be
// #ifdef'd because it named CMD_DEV_ADDRESS/CMD_DEV_REGISTER, which exist only
// under -D DEV_TOOLS. rurp_pinmap_guard.h delegates to this predicate rather
// than re-listing the set.
//
// Hard constraints:
//  - NO preprocessor conditional of any kind in this function's body. All seven
//    named macros are unconditionally defined. There is no source-scan gate
//    that checks this -- a tree-wide search finds none -- and the only
//    consumer of this predicate outside this header is
//    test/native/avr/test_cmd_admission/test_cmd_admission.cpp's truth table.
//  - static inline, IN THIS HEADER. [env:native]'s build_src_filter compiles
//    only src/proms/, rurp_serial_utils.cpp and json_parser.c, so a definition
//    elsewhere would not link into the native test binary.
//  - MUST NOT name CMD_DEV_ADDRESS or CMD_DEV_REGISTER -- they do not exist in
//    a no-DEV_TOOLS build, and naming them recreates the divergence this
//    predicate exists to remove.
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

// The skip-blank-check control flag, 0x08 -- retired in 3.1.0 (Phase 205).
// The host side -- firestarter_app/firestarter/constants.py's flag block --
// was retired in the same commit pair. This value must NEVER be reused for
// any new control flag: an already-shipped host still composes 0x08 on
// every `write -b` and on `dev test`'s masked UV slot writes, and
// reassigning the bit would make that stale host silently turn on whatever
// new behaviour took the number. While it existed, the flag selected
// whether write-init's blank check ran; that check itself left the
// firmware in the same phase (FWBLANK-01..03).

#define FLAG_VPE_AS_VPP 0x10

#define FLAG_OUTPUT_ENABLE 0x20
#define FLAG_CHIP_ENABLE 0x40

#define FLAG_VERBOSE 0x80

// Declines the SDP (Software Data Protection) auto-unlock command sequence
// on protocol 0x0D (eeprom_28c.cpp), so a write against an SDP-protected
// AT28C part will not land -- an honest tradeoff reported via
// MSG_WARN_SDP_UNLOCK_SKIPPED rather than a silent no-op. Firmware-only in
// this milestone phase: the host CLI surface (--skip-sdp-unlock /
// constants.py) is delivered separately, firmware-before-host.
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
    uint8_t protocol;            /* largest dispatched value is 0x39 (PROTO_PHANTOM_0x39,
                                   * include/proto_constants.h) -- fits uint8_t */
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
                                   * 0 = absent. Reset per command in json_parse,
                                   * exactly like chip_id above. */
    uint32_t region_end;         /* absolute, EXCLUSIVE end address of the operation's region,
                                   * delivered by the host over the wire; 0 = absent. Unlike
                                   * page_size above, absent means the WHOLE DEVICE -- the
                                   * inversion is deliberate: page_size guards a destructive
                                   * write, so refusing on absence is the safe direction, while
                                   * this field guards a relaxation of an existing check, so
                                   * falling back to the stricter whole-device behaviour is the
                                   * safe direction instead. Reset per command in json_parse,
                                   * exactly like page_size above. */
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