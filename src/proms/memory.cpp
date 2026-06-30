/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Top-level dispatch entry: configure_memory() — all protocols and legacy mem_type fallback.
 *
 * Dispatch order (protocol-prefix first, mem_type fallback only when protocol==0):
 *   1. protocol==0x10 → configure_flash_intel()   (FLASH-INTEL)
 *   2. protocol==0x0D → configure_eeprom28c()      (EEPROM-POLL)
 *   3. protocol==0x06 → configure_flash3()         (FLASH-AMD-ALT)
 *   4. protocol∈{0x05,0x35,0x39} → configure_flash4()  (FLASH-AMD-STD; 0x35/0x39 = phantoms)
 *   5. protocol∈{0x07,0x08,0x0B} → configure_eprom()   (EPROM-STD/QUICK/LEGACY)
 *   6. protocol∈{0x0E,0x27,0x28,0x29} → configure_sram()  (SRAM/NVRAM — BLOCKER-2 guard)
 *  6a. protocol∈{0x11,0x2A,0x2B,0x2C} → configure_not_implemented()  (named infeasibles)
 *  6b. protocol!=0 → configure_not_implemented()  (generic fail-closed: unknown non-zero proto)
 *   7–11. protocol==0 only: mem_type fallback for backward-compat (DISP-02)
 *
 * The protocol-prefix chain (steps 1–6b) ensures that every non-zero protocol either
 * reaches its correct handler or fail-closes. Steps 7–11 (mem_type fallback) are
 * UNREACHABLE for any protocol_id emitted by chip_database.json.
 *
 * Safety invariant (BLOCKER-2): SRAM-family chips (0x0E/0x27/0x28/0x29) must NEVER
 * reach configure_eprom() — that would enable the 12V VPP boost regulator on a 5V part.
 * The protocol-prefix dispatch enforces this structurally: 0x28 (SRAM_STD/FM1608 FRAM)
 * dispatches to configure_sram() at step 6, before the generic fail-closed at 6b.
 *
 * This dispatch structure is FROZEN for Phase 87 (naming/documentation pass).
 * Source of truth: firestarter/CLAUDE.md §Protocol Dispatch (dispatch order table).
 * Full protocol prose: firestarter/doc/PROTOCOLS.md §1 (real buckets), §2 (non-protocols).
 */

#include "memory.h"

#include <Arduino.h>
#include <stdint.h>

#include "eprom.h"
#include "flash_type_3.h"
#include "flash_type_4.h"
#include "flash_intel.h"
#include "eeprom_28c.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "not_implemented.h"
#include "operation_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "sram.h"

#define TYPE_EPROM 1
#define TYPE_FLASH_TYPE_3 3
#define TYPE_SRAM 4
#define TYPE_FLASH_TYPE_4 5

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

void memory_read_execute(firestarter_handle_t* handle);
void memory_write_execute(firestarter_handle_t* handle);
void memory_verify_execute(firestarter_handle_t* handle);

void memory_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool state);
bool memory_get_control_register(firestarter_handle_t* handle, rurp_register_t bit);
uint8_t memory_get_data(firestarter_handle_t* handle, uint32_t address);
void memory_set_data(firestarter_handle_t* handle, uint32_t address, uint8_t data);

uint8_t programming = 0;

void configure_memory(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_MEMORY);
    handle->firestarter_operation_init = NULL;
    handle->firestarter_operation_main = NULL;
    handle->firestarter_operation_end = NULL;

    switch (handle->cmd) {
        case CMD_READ:
            handle->firestarter_operation_main = memory_read_execute;
            break;
        case CMD_WRITE:
            handle->firestarter_operation_main = memory_write_execute;
            break;
        case CMD_VERIFY:
            handle->firestarter_operation_main = memory_verify_execute;
            break;
    }

    handle->firestarter_get_data = memory_get_data;
    handle->firestarter_set_data = memory_set_data;

    handle->firestarter_set_address = mem_util_set_address;

    handle->firestarter_set_control_register = memory_set_control_register;
    handle->firestarter_get_control_register = memory_get_control_register;

    mem_util_set_address(handle, 0);

    if (handle->protocol == 0x10) {
        configure_flash_intel(handle);
        return;
    }

    if (handle->protocol == 0x0D) {
        configure_eeprom28c(handle);
        return;
    }

    if (handle->protocol == 0x06) {
        configure_flash3(handle);
        return;
    }

    if (handle->protocol == 0x05 || handle->protocol == 0x35 || handle->protocol == 0x39) {
        configure_flash4(handle);
        return;
    }

    if (handle->protocol == 0x07 || handle->protocol == 0x08 || handle->protocol == 0x0B) {
        configure_eprom(handle);
        return;
    }

    if (handle->protocol == 0x0E || handle->protocol == 0x27 ||
        handle->protocol == 0x28 || handle->protocol == 0x29) {
        configure_sram(handle);
        return;
    }

    // Named infeasibility arms (D-02): FWH and GAL/PLD — infeasible on RURP.
    // Explicitly recognized per SC#4 / roadmap Phase 64 requirement (DISP-04).
    if (handle->protocol == 0x11 || handle->protocol == 0x2A ||
        handle->protocol == 0x2B || handle->protocol == 0x2C) {
        configure_not_implemented(handle);
        return;
    }

    // Generic fail-closed guard: any non-zero unrecognized protocol → not-implemented.
    // Must sit AFTER all implemented protocol cases and BEFORE the protocol==0 mem_type fallback.
    // Eliminates the 12V VPP-hazard mem_type fallback for unimplemented protocols (T-64-01).
    if (handle->protocol != 0) {
        configure_not_implemented(handle);
        return;
    }

    // Legacy mem_type fallback: reachable ONLY when protocol == 0 (DISP-02).
    if (handle->mem_type == TYPE_EPROM) {
        configure_eprom(handle);
        return;
    } else if (handle->mem_type == TYPE_SRAM) {
        configure_sram(handle);
        return;
    } else if (handle->mem_type == TYPE_FLASH_TYPE_3) {
        configure_flash3(handle);
        return;
    } else if (handle->mem_type == TYPE_FLASH_TYPE_4) {
        configure_flash4(handle);
        return;
    }
    LOG_ERROR_ID_U8(MSG_ERR_MEM_TYPE_UNSUPPORTED, handle->mem_type);
    handle->response_code = RESPONSE_CODE_ERROR;
}

void memory_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool state) {
    rurp_register_t control_register = rurp_read_from_register(CONTROL_REGISTER);
    rurp_register_t data = state ? control_register | (bit) : control_register & ~(bit);
    rurp_write_to_register(CONTROL_REGISTER, data);
}

bool memory_get_control_register(firestarter_handle_t* handle, rurp_register_t bit) {
    rurp_register_t control_register = rurp_read_from_register(CONTROL_REGISTER);
    return control_register & bit;
}
rurp_register_t mem_util_calculate_lsb_register(firestarter_handle_t* handle, uint32_t address) {
    return address & 0xFF;
}

rurp_register_t mem_util_calculate_msb_register(firestarter_handle_t* handle, uint32_t address) {
    return ((address >> 8) & 0xFF);
}

rurp_register_t mem_util_calculate_top_address_register(firestarter_handle_t* handle, uint32_t address) {
    rurp_register_t top_address = ((uint32_t)address >> 16) & (CTRL_ADDRESS_LINE_16 | CTRL_ADDRESS_LINE_17 | CTRL_ADDRESS_LINE_18 | CTRL_READ_WRITE);
    rurp_register_t mask = CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE | CTRL_VPP_REGULATOR_ENABLE;
    if (handle->pins < 32) {
        // CTRL_VPP_VPE_DROP_ENABLE and CTRL_ADDRESS_LINE_16 share the same CONTROL bit — preserving CTRL_VPP_VPE_DROP_ENABLE
        // would corrupt A16 for 32-pin (512KB) chips. DIP32 chips use CTRL_VPP_P1_ENABLE instead.
        mask |= CTRL_VPP_VPE_DROP_ENABLE;
    }
    top_address |= rurp_read_from_register(CONTROL_REGISTER) & mask;

    if (handle->pins == 28) {
        top_address |= CTRL_ADDRESS_LINE_17;
    }
    return top_address;
}

void mem_util_set_address(firestarter_handle_t* handle, uint32_t address) {
#ifdef DEBUG_ADDRESS
    LOG_DEBUG_ID_SUB_U24(DBG_ADDRESS, address);
#endif
    uint8_t lsb = mem_util_calculate_lsb_register(handle, address);
    rurp_write_to_register(LEAST_SIGNIFICANT_BYTE, lsb);

    uint8_t msb = mem_util_calculate_msb_register(handle, address);
    rurp_write_to_register(MOST_SIGNIFICANT_BYTE, msb);

    rurp_register_t top_address = mem_util_calculate_top_address_register(handle, address);
    rurp_write_to_register(CONTROL_REGISTER, top_address);

#ifdef DEBUG_ADDRESS
    LOG_DEBUG_ID_SUB_U8_U8_U8(DBG_TOP_MSB_LSB, (uint8_t)top_address, msb, lsb);
#endif
}

void memory_read_execute(firestarter_handle_t* handle) {
    int buf_size = min(handle->mem_size - handle->address, DATA_BUFFER_SIZE);
    LOG_DEBUG_ID_SUB_U24(DBG_READING_FROM_ADDRESS, handle->address);
    for (int i = 0; i < buf_size; i++) {
        uint8_t data = handle->firestarter_get_data(handle, handle->address + i);
        handle->data_buffer[i] = data;
    }
    handle->data_size = buf_size;
}

uint8_t memory_get_data(firestarter_handle_t* handle, uint32_t address) {
    rurp_chip_output();
    address = mem_util_remap_address_bus(handle, address, READ_FLAG);

    handle->firestarter_set_address(handle, address);
    rurp_set_data_input();

    /*
     * Address-settling delay: time from address-set to /CE assertion.
     * Zero-ambiguity: 0 = no settling delay (explicit zero — a valid D-04 test
     * point). Knob: "read-settling-delay" JSON field -> handle->read_settling_us.
     * T-44-01 cap: value already clamped at parse time (READ_TIMING_MAX_US=1000µs);
     * guard here catches any future path that bypasses the parser.
     * Note: values < 3µs are below delayMicroseconds() accuracy on 16 MHz AVR
     * (Pitfall 5) — the sweep should start at 3µs for clean step response.
     */
    if (handle->read_settling_us) {
        uint32_t settling = handle->read_settling_us > 1000UL ? 1000UL : handle->read_settling_us;
        delayMicroseconds(settling);
    }

    rurp_chip_enable();

    /*
     * Read-strobe pulse width: time /CE is asserted before data latch.
     * Zero-ambiguity: 0 = use firmware default 3µs (preserves current behaviour
     * when the host does not set the param). Knob: "read-strobe-us" JSON field
     * -> handle->read_strobe_us.
     * T-44-01 cap: clamped at parse time; secondary guard here for safety.
     */
    uint32_t strobe = handle->read_strobe_us ? handle->read_strobe_us : 3UL;
    if (strobe > 1000UL) strobe = 1000UL;   /* T-44-01 secondary guard */
    delayMicroseconds(strobe);

    uint8_t data = rurp_read_data_buffer();
    rurp_chip_disable();

    return data;
}

void memory_write_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        handle->firestarter_set_data(handle, handle->address + i, handle->data_buffer[i]);
    }
}

void memory_set_data(firestarter_handle_t* handle, uint32_t address, uint8_t data) {
    rurp_chip_input();
    address = mem_util_remap_address_bus(handle, address, WRITE_FLAG);

    handle->firestarter_set_address(handle, address);
    rurp_write_data_buffer(data);
    delayMicroseconds(3);  // Needed for slower address changes like slow ROMs and "Power through address lines"

    /* RC-1 / Phase 98 Plan 02 deliberate PGM-assert (D-01 belt):
     * For 0x08 EPROM_QUICK, 32-pin, ≤256K (A18 = bit 18 = mask 0x40000 unused):
     *   AM27C020.pdf: program cycle requires CE=VIL AND PGM=VIL.  Plan 01's
     *   DIP32_27C020 pinout took pin 31 OFF the address bus so line 22 is no
     *   longer driven by an address bit.  This gated write is the "suspenders"
     *   half of D-01: it EXPLICITLY holds pin 31's bus line (CTRL_ADDRESS_LINE_18)
     *   at the program-active LOW level (VIL) across the CE-pulse window.
     *
     *   Q1 RESOLVED (2026-06-30): static-high-pins would drive HIGH (VIH) —
     *   the RURP latch path has no inversion.  PGM program-active is VIL (LOW),
     *   so the assert MUST be a firmware clear/hold-low, not a static_high_mask.
     *
     *   Gate: protocol==0x08 && pins==32 && mem_size<=262144 ensures the 0x08
     *   bit (CTRL_VPP_P1_ENABLE_REV2 == CTRL_ADDRESS_LINE_18_REV2 == 0x08 on
     *   Rev 2.0) NEVER fires for a 512K/1M A18 user (D-04 firmware belt).
     *
     *   HIGH-1 / RC-1 caveat (no over-claim): under RC-1 the addr-0 register
     *   state is byte-unchanged by this fix — pin 31 is already at VIL at addr 0
     *   because line 22 is neither an address bit (DIP32_27C020 has 18 address
     *   pins, A0-A17) nor in static_high_mask.  If Phase 99 still shows 0 bits
     *   at addr 0 that is CONSISTENT WITH the analysis, not a new bug.
     *   Phase 99 is the sole empirical gate.
     *
     *   MED-5 (verified no-op): program_mismatched_bytes already holds
     *   CTRL_VPP_P1_ENABLE across the FULL program window (set before the byte
     *   loop, cleared after).  That per-buffer hold strictly encompasses every
     *   per-byte CE pulse in this function.  No redundant per-byte P1 re-assertion
     *   is added here — only the explicit PGM line hold-LOW (CTRL_ADDRESS_LINE_18
     *   clear), which is a DISTINCT control from the P1 VPP routing. */
    if (handle->protocol == 0x08 && handle->pins == 32 && handle->mem_size <= 262144) {
        /* Deliberate PGM=VIL hold-LOW: read current CONTROL register state,
         * explicitly clear CTRL_ADDRESS_LINE_18 (pin 31 / line 22), and write
         * it back.  This emits a recorded CONTROL_REGISTER write (the
         * implementation artifact that Test A / RC-98A asserts on) and holds
         * pin 31 at the program-active level through the CE pulse below. */
        rurp_register_t ctrl = rurp_read_from_register(CONTROL_REGISTER);
        rurp_write_to_register(CONTROL_REGISTER, ctrl & ~CTRL_ADDRESS_LINE_18);
    }

    rurp_chip_enable();
    delayMicroseconds(handle->pulse_delay);
    rurp_chip_disable();
}

void memory_verify_execute(firestarter_handle_t* handle) {
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint8_t byte = handle->firestarter_get_data(handle, handle->address + i);
        uint8_t expected = handle->data_buffer[i];
        if (byte != expected) {
            {
                uint32_t addr = (uint32_t)(handle->address + i);
                uint8_t _b[5] = {
                    (uint8_t)expected,
                    (uint8_t)byte,
                    (uint8_t)((addr >> 16) & 0xFF),
                    (uint8_t)((addr >> 8) & 0xFF),
                    (uint8_t)(addr & 0xFF),
                };
                LOG_ERROR_ID_BYTES(MSG_ERR_VERIFY, _b, 5);
            }
            handle->response_code = RESPONSE_CODE_ERROR;
            return;
        }
    }
}

// Utility functions
uint32_t mem_util_remap_address_bus(const firestarter_handle_t* handle, uint32_t address, uint8_t read_write) {
    bus_config_t config = handle->bus_config;
    uint32_t reorg_address = config.address_mask & address;
    if (config.address_lines[0] != 0xFF) {
        for (int i = config.matching_lines; i < ADDRESS_LINES_SIZE && config.address_lines[i] != 0xFF; i++) {
            uint32_t line_bit = (uint32_t)1 << config.address_lines[i];
            reorg_address &= ~line_bit;
            if (address & 1UL << i) {
                reorg_address |= line_bit;
            }
        }
    }
    if (config.rw_line != 0xFF) {
        reorg_address |= (uint32_t)read_write << config.rw_line;
    }

    // Set VPP line to high if VPP is not on P1
    if (config.vpp_line != 0xFF && !using_p1_as_vpp(handle)) {
        reorg_address |= 1UL << config.vpp_line;
    }

    reorg_address |= config.static_high_mask;
    return reorg_address;
}

typedef struct {
    uint32_t address;
} blank_check_progress_data_t;

#define BLANK_CHECK_CHUNK_SIZE 2048
void uint32_to_bytes(char* buffer, int pos, uint32_t value) {
    buffer[pos] = (value >> 24) & 0xFF;
    buffer[pos++] = (value >> 16) & 0xFF;
    buffer[pos++] = (value >> 8) & 0xFF;
    buffer[pos++] = value & 0xFF;
}

void mem_util_blank_check(firestarter_handle_t* handle) {
    blank_check_progress_data_t* progress_data;
    if (!is_operation_in_progress(handle)) {
        set_operation_in_progress(handle);
        handle->progress_data = malloc(sizeof(blank_check_progress_data_t));
        progress_data = (blank_check_progress_data_t*)handle->progress_data;
        progress_data->address = handle->address;
        handle->address = 0;
    } else {
        progress_data = (blank_check_progress_data_t*)handle->progress_data;
        if (handle->address >= handle->mem_size) {
            clear_operation_in_progress(handle);
            handle->address = progress_data->address;
            free(handle->progress_data);
            handle->progress_data = NULL;
            return;
        }
    }

    // for (uint32_t i = handle->address; i < handle->address + BLANK_CHECK_CHUNK_SIZE; i++) {
    uint32_t end_address = handle->address + BLANK_CHECK_CHUNK_SIZE;
    for (uint32_t i = handle->address; i < end_address && i < handle->mem_size; i++) {
        uint8_t val = handle->firestarter_get_data(handle, i);
        if (val != 0xFF) {
            uint8_t _b[4] = {
                (uint8_t)((i >> 16) & 0xFF),
                (uint8_t)((i >> 8) & 0xFF),
                (uint8_t)(i & 0xFF),
                (uint8_t)val,
            };
            // On the Uno, rurp_log_id is com_mode-gated and this function runs in
            // programmer mode, so a direct emit here is silently dropped. For the
            // standalone blank-check command, stash the offset+value and let
            // _single_step_operation_callback emit MSG_ERR_NOT_BLANK in communication
            // mode. Other callers (write-init / erase-end) keep the direct emit.
            // (#transport-protocol-verify)
            if (handle->cmd == CMD_BLANK_CHECK) {
                handle->data_buffer[0] = (char)_b[0];
                handle->data_buffer[1] = (char)_b[1];
                handle->data_buffer[2] = (char)_b[2];
                handle->data_buffer[3] = (char)_b[3];
                handle->data_size = 4;
            } else {
                LOG_ERROR_ID_BYTES(MSG_ERR_NOT_BLANK, _b, 4);
            }
            handle->response_code = RESPONSE_CODE_ERROR;
            return;
        }
    }
    handle->address += BLANK_CHECK_CHUNK_SIZE;
// #define RAW_DATA_PROGRESS
#ifdef RAW_DATA_PROGRESS
    handle->response_code = RESPONSE_CODE_DATA;
    uint32_to_bytes(handle->data_buffer, 0, handle->address);
    uint32_to_bytes(handle->data_buffer, 4, handle->mem_size);
    handle->data_size = 8;
#else
    if (handle->address > handle->mem_size) {
        handle->address = handle->mem_size;
    }
    // Send progress back to the client. For the standalone blank-check command the
    // emit is deferred to _single_step_operation_callback (communication mode): this
    // function runs in programmer mode where the Uno's com_mode-gated rurp_log_id
    // drops frames. Other callers (write-init / erase-end) keep the direct emit.
    // (#transport-protocol-verify)
    if (handle->cmd != CMD_BLANK_CHECK) {
        LOG_DATA_ID_U32_U32(MSG_DATA_PROGRESS, handle->address, handle->mem_size);
    }
#endif
}
