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

// D-02: the single shared value-semantics default for the DEV_TOOLS switch,
// so the same directive means the same thing on every target (AVR, native,
// native_nodevtools and ARM/py32f071) instead of one presence-semantics
// mechanism on AVR/native and a different by-omission mechanism on ARM,
// where DEV_TOOLS=0 would perversely ENABLE dev tools under the old
// #ifdef-based test. Placed INSIDE the __FIRESTARTER_H__ guard, beside
// DATA_BUFFER_SIZE above (the in-tree precedent for exactly this idiom) --
// placing it above the guard instead causes the host-repo parity test's
// _find_header_guard_line_indices to misidentify the real guard, and the
// test then passes only by an arithmetic cancellation between a spurious
// #endif decrement and an un-skipped #ifndef increment (correction C-18),
// never for the right reason. Honest scope caveat (correction C-7): two of
// the six conversion sites -- include/dev_tools.h and src/dev_tools.cpp --
// test DEV_TOOLS before including anything, so this default is not
// syntactically in scope there. Behaviour is still correct at those two
// sites without it: ISO C/C++ evaluates an undefined identifier inside a
// preprocessor #if expression as 0, which is exactly this default's value,
// so the block below is documentary (not load-bearing) at those two sites.
// If -Wundef is ever enabled, those two sites will need this default pulled
// into a dependency-free header included unconditionally at their top.
#ifndef DEV_TOOLS
#define DEV_TOOLS 0
#endif

/* CMD_FRAME_MAX: largest legitimate JSON command frame payload (bytes).
 * Worst-case JSON command is ~422 B; 512 B gives headroom.
 * Equals DATA_BUFFER_SIZE — the decoder's internal overflow cap.
 * Mirrors constants.py CMD_FRAME_MAX per CLAUDE.md parity (FRAME-05 / D-06). */
#define CMD_FRAME_MAX DATA_BUFFER_SIZE

/* FW identity string: "<version>:<board>" only.
 * Buffer capacity (DATA_BUFFER_SIZE) is no longer carried in the identity
 * string — it is advertised as a u16 bytes param on every MSG_OK_READY ack
 * (Phase 55 / CAP-01). */
#define FW_VERSION VERSION ":" RURP_BOARD_NAME

#define TIMEOUT_MS 1000

#define CMD_IDLE 0
#define CMD_READ 1
#define CMD_WRITE 2
#define CMD_ERASE 3
#define CMD_BLANK_CHECK 4
#define CMD_CHECK_CHIP_ID 5
#define CMD_VERIFY 6

#if DEV_TOOLS
#define CMD_DEV_ADDRESS 7
#define CMD_DEV_REGISTER 8
#endif

// Standalone SDP (Software Data Protection) enable/disable commands on
// protocol 0x0D (eeprom_28c.cpp). Unlike CMD_READ/CMD_WRITE/etc these carry
// no payload and no MAIN/END round-trip -- see Plan 119-04. Slots 9 and 10
// were the only two free command values, and BOTH sit above
// CMD_DEV_ADDRESS (7): that is exactly why the #ifdef DEV_TOOLS-conditional
// ordinal admission guard had to be replaced by is_memory_cmd() (below)
// BEFORE these could be defined (D-03, LOCK-03 is a hard prerequisite for
// LOCK-02) -- with the old guard in place, a DEV_TOOLS build would have
// routed 9/10 into the dev-tools branch and never called configure_memory,
// leaving the new commands with no bus configuration at all.
// Both defines are UNCONDITIONAL (never DEV_TOOLS-gated): they are real
// user-facing operations in every build. The host CLI surface
// (`firestarter dev sdp <chip> enable|disable`, constants.py CMD_SDP_* /
// COMMAND_NAMES) arrives in Phase 120 HOST-01/HOST-03 -- firmware-before-host.
#define CMD_SDP_UNLOCK 9
#define CMD_SDP_LOCK 10

#define CMD_READ_VPP 11
#define CMD_READ_VPE 12
#define CMD_FW_VERSION 13
#define CMD_CONFIG 14
#define CMD_HW_VERSION 15

// Phase 151, LOCK-02 (D-01/D-02): 16 is the next unused integer -- no slot
// below 11 was free (CMD_READ_VPP..CMD_HW_VERSION occupy 11-15, and Slots 9
// and 10 were the only two free command values below that, per the
// CMD_SDP_UNLOCK/CMD_SDP_LOCK comment above). This IS a memory command: the
// protection-status read is issued through handle->firestarter_get_data,
// a protocol-handler function pointer only configure_memory() sets, so it
// needs a protocol handler exactly as CMD_READ/CMD_WRITE/etc do.
#define CMD_LOCK_STATUS 16

// Replaces the #ifdef DEV_TOOLS-conditional ordinal admission guard that
// used to live at firestarter.cpp's parse_json (the old
// `if (handle->cmd < CMD_DEV_ADDRESS)` test, itself wrapped in
// `#ifdef DEV_TOOLS`). That conditional was STRUCTURALLY FORCED, not lazy:
// CMD_DEV_ADDRESS/CMD_DEV_REGISTER only exist under -D DEV_TOOLS, so any
// guard naming them had no choice but to be preprocessor-conditional too.
// is_memory_cmd() removes the need for a conditional entirely by not
// naming those two symbols at all -- it enumerates the commands that
// legitimately configure a memory bus, by name, unconditionally. This
// access-control gate admitted eight commands from Phase 119 until Phase
// 151, and now admits nine: Phase 151 (LOCK-02, OD-3) added CMD_LOCK_STATUS
// as the ninth. rurp_pinmap_guard.h's rurp_pinmap_refuses() DELEGATES to
// this predicate rather than re-listing its set, so the provisional-pinmap
// refusal for CMD_LOCK_STATUS follows automatically from this one edit --
// but that guard's test suite (test_pinmap_provisional) runs in NO CI leg,
// so it is verified with a local `pio test` run, not CI green.
//
// This is a DELIBERATE SAFETY TIGHTENING (D-01), not a preserved behaviour:
// today, a RELEASE build (no -D DEV_TOOLS) still runs json_parse AND
// configure_memory for CMD_DEV_ADDRESS (7) / CMD_DEV_REGISTER (8) before
// loop()'s `default:` refuses them with MSG_ERR_UNKNOWN_CMD -- i.e. it
// configures a memory handler for a command it is about to refuse. An
// honest enumeration excludes 7 and 8, so after this change a release
// build no longer does that; cmd 7 and 8 keep their MSG_ERR_UNKNOWN_CMD
// refusal in a release build, unchanged.
//
// This predicate is an ACCESS-CONTROL GATE, not hygiene: it decides which
// commands may call configure_memory() and therefore configure the
// hardware bus. configure_eprom() (reached only through this gate) enables
// the 12V VPP boost regulator -- a hazard on a 5V part -- so admitting an
// extra command here is a hardware-safety decision, not a style one.
//
// Hard constraints (Plan 119-03's source-scan gate makes the first
// machine-checked):
//  - NO preprocessor conditional of any kind inside this function's body.
//    All nine named macros below are unconditionally defined, so none is
//    needed (D-02).
//  - static inline, IN THIS HEADER (not a .cpp / new translation unit):
//    [env:native]'s build_src_filter compiles only src/proms/,
//    src/boards/rurp_serial_utils.cpp and src/json_parser.c, so a
//    definition anywhere else would not link into the native test binary
//    and Plan 119-02's two-env truth-table suite could not exist.
//  - MUST NOT name CMD_DEV_ADDRESS or CMD_DEV_REGISTER -- those macros do
//    not exist in a no-DEV_TOOLS build (see #ifdef above), and naming them
//    here would recreate exactly the divergence this predicate exists to
//    remove.
static inline bool is_memory_cmd(uint8_t cmd) {
    switch (cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case CMD_ERASE:
        case CMD_BLANK_CHECK:
        case CMD_CHECK_CHIP_ID:
        case CMD_VERIFY:
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
#define FLAG_SKIP_BLANK_CHECK 0x08
#define FLAG_VPE_AS_VPP 0x10

#define FLAG_OUTPUT_ENABLE 0x20
#define FLAG_CHIP_ENABLE 0x40

#define FLAG_VERBOSE 0x80

// Declines the SDP (Software Data Protection) auto-unlock command sequence
// on protocol 0x0D (eeprom_28c.cpp), so a write against an SDP-protected
// AT28C part will not land -- an honest tradeoff reported via
// MSG_WARN_SDP_UNLOCK_SKIPPED rather than a silent no-op. Firmware-only in
// this milestone phase (v1.22 Phase 118 OBS-02): the host CLI surface
// (--skip-sdp-unlock / constants.py) arrives in Phase 120 HOST-03.
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
    uint8_t protocol;
    uint8_t pins;
    uint32_t mem_size;
    uint32_t address;
    uint16_t vpp_mv;
    uint32_t pulse_delay;
    uint32_t read_settling_us;   /* address-settling delay before /CE assert (µs; 0 = no settling delay) */
    uint32_t read_strobe_us;     /* /CE read-strobe pulse width (µs; 0 = use default 3µs) */
    uint16_t ctrl_flags;
    uint16_t chip_id;
    uint16_t page_size;          /* per-chip page-write size delivered by the host over the wire
                                   * (Phase 149, PGSZ-01/PGSZ-02); 0 = absent, so the 0x0D handler
                                   * applies its own named fallback floor. Reset per command in
                                   * json_parse, exactly like chip_id above (D-05). */
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