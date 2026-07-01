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

#ifdef DEV_TOOLS
#define CMD_DEV_ADDRESS 7
#define CMD_DEV_REGISTER 8
#endif

#define CMD_READ_VPP 11
#define CMD_READ_VPE 12
#define CMD_FW_VERSION 13
#define CMD_CONFIG 14
#define CMD_HW_VERSION 15

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

#define is_flag_set(flag) \
    ((handle->ctrl_flags & flag) == flag)

#define ADDRESS_LINES_SIZE 20

/* MAX_27C020_SIZE: the <=256K (262144 byte) size boundary for 0x08
 * (EPROM_QUICK) 32-pin parts where pin 31 (A18 on DIP32_STD) is
 * structurally unused as an address line and is safe to repurpose as
 * DIP32_27C020's PGM/RW strobe (see memory.cpp's CR-1 comment block).
 * Chips above this boundary (512K AM27C040, 1M AM27C080) legitimately
 * use pin 31 = A18 and MUST stay on DIP32_STD (D-04 alias guard).
 * Mirrors the host-side constant of the same name/value in
 * firestarter_app/tools/build_db.py (98-03's resolve_pinout_key size
 * gate) — a divergence between the two is a hardware-damage A18 risk;
 * see IN-02 / T-98-18 and tests/test_revision_constants_parity.py. */
#define MAX_27C020_SIZE 262144

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
    uint8_t mem_type;
    uint32_t protocol;
    uint8_t pins;
    uint32_t mem_size;
    uint32_t address;
    uint16_t vpp_mv;
    uint32_t pulse_delay;
    uint32_t read_settling_us;   /* address-settling delay before /CE assert (µs; 0 = no settling delay) */
    uint32_t read_strobe_us;     /* /CE read-strobe pulse width (µs; 0 = use default 3µs) */
    uint32_t page_size;          /* PGSZ-02/03: per-chip page size from DB (bytes; 0 = use flash4_page_size() heuristic) */
    uint32_t ctrl_flags;
    uint16_t chip_id;
    char data_buffer[DATA_BUFFER_SIZE];
    uint32_t data_size;
    bus_config_t bus_config;
    void* progress_data;

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