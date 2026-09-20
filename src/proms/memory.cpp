/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "memory.h"

#include <Arduino.h>
#include <stdint.h>

#include "eprom.h"
#include "flash_nor_unlock.h"
#include "flash_5v_page.h"
#include "flash_intel.h"
#include "eeprom_28c.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "not_implemented.h"
#include "operation_utils.h"
#include "proto_constants.h"
#include "rurp_pinmap_guard.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "sram.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// AVR delayMicroseconds() accurate ceiling -- see mem_util_delay_us /
// mem_util_split_delay below.
#define MEM_UTIL_DELAY_US_MAX 16383UL

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

    // While the board's pin map is provisional (RURP_PINMAP_PROVISIONAL, set by a
    // board header), refuse every command that can energise the PROM bus, BEFORE
    // any handler is configured. The three operation pointers are still NULL here,
    // so this early return leaves the handle exactly as configure_not_implemented()
    // would. The payload is the COMMAND ordinal, not the protocol ordinal -- this
    // is a command-admission refusal, not a protocol-dispatch one. On AVR targets
    // RURP_PINMAP_PROVISIONAL is never defined, so this compiles to nothing.
    if (rurp_pinmap_refuses(handle->cmd)) {
        LOG_ERROR_ID_U8(MSG_ERR_NOT_SUPPORTED, (uint8_t)handle->cmd);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }

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

    if (handle->protocol == PROTO_FLASH_INTEL) {
        configure_flash_intel(handle);
        return;
    }

    if (handle->protocol == PROTO_EEPROM_PARALLEL) {
        configure_eeprom28c(handle);
        return;
    }

    if (handle->protocol == PROTO_FLASH_NOR_UNLOCK) {
        configure_flash_nor_unlock(handle);
        return;
    }

    if (handle->protocol == PROTO_FLASH_5V_PAGE || handle->protocol == PROTO_PHANTOM_0x35 || handle->protocol == PROTO_PHANTOM_0x39) {
        configure_flash_5v_page(handle);
        return;
    }

    if (handle->protocol == PROTO_EPROM_28PIN || handle->protocol == PROTO_EPROM_32PIN || handle->protocol == PROTO_EPROM_24PIN) {
        configure_eprom(handle);
        return;
    }

    if (handle->protocol == PROTO_SRAM_32PIN || handle->protocol == PROTO_SRAM_24PIN ||
        handle->protocol == PROTO_SRAM_28PIN || handle->protocol == PROTO_SRAM_32PIN_NVRAM) {
        configure_sram(handle);
        return;
    }

    // Named infeasibility arms: FWH and GAL/PLD — infeasible on RURP,
    // and explicitly recognized as such rather than falling through.
    if (handle->protocol == 0x11 || handle->protocol == 0x2A ||
        handle->protocol == 0x2B || handle->protocol == 0x2C) {
        configure_not_implemented(handle);
        return;
    }

    // Generic fail-closed guard: every remaining protocol value — including
    // protocol == 0 — is unrecognized and reaches not-implemented. Trusts
    // only handle->protocol end to end; no backward-compat fallback axis
    // remains -- dispatch is protocol-only.
    configure_not_implemented(handle);
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
    // CTRL_VPE_ENABLE, CTRL_VPP_P1_ENABLE, CTRL_VPP_A9_ENABLE and CTRL_VPP_REGULATOR_ENABLE are
    // UNCONDITIONALLY preserved below — this is why VPE survives a per-byte verify read. It is
    // NOT because the verify read leaves the control register alone: every address write (the
    // one caller of this function) writes CONTROL_REGISTER unconditionally on every byte, for
    // both the pulse and the verify; the unconditional preserve mask is what carries the route
    // bit across that write.
    rurp_register_t mask = CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE | CTRL_VPP_REGULATOR_ENABLE;
    // CTRL_VPP_VPE_DROP_ENABLE selects a VPP LEVEL -- VPE dropped through the
    // resistor to ~13 V -- and nothing else. Do NOT exclude pins >= 32 from the
    // preserve: 32-pin parts with VPP on pin 1 are handled with a jumper, so
    // excluding the bit silently programs 0x08 on the UN-DROPPED rail.
    //
    // For pins < 32 the drop bit is preserved unconditionally on every revision.
    // For pins >= 32 the preserve is gated on hardware revision ALONE, and the
    // gate is necessary: on Rev 0 / Rev 1,
    // rurp_map_ctrl_reg_for_hardware_revision() maps CTRL_VPP_VPE_DROP_ENABLE and
    // CTRL_ADDRESS_LINE_16 onto the SAME physical bit, so preserving the drop bit
    // there would force physical A16 permanently high. On Rev 2-class they are
    // distinct bits and do not interfere.
    if (handle->pins < 32) {
        mask |= CTRL_VPP_VPE_DROP_ENABLE;
    }
#ifdef HARDWARE_REVISION
    else {
        switch (rurp_get_hardware_revision()) {
        case REVISION_2_0:
        case REVISION_2_1:
        case REVISION_2_2:
        case REVISION_2_3:
            // The arm's nominal reach widens to every 32-pin protocol on Rev 2-class (0x0E, 0x29,
            // 0x10, 32-pin flash), not just the EPROM family -- but none of them ever SETS the
            // drop bit, so there is nothing for this preserve to leak. Proven, not argued:
            // test_vpp_eprom_v131.cpp's 32-pin non-EPROM byte-identity case
            // (test_vpp01_dip32_nonEprom_0x10_route_is_byte_identical_before_and_after) and its
            // "preserve, never introduce" leg
            // (test_vpp01_truthtable_pins32_rev2_2_preserve_never_introduces).
            mask |= CTRL_VPP_VPE_DROP_ENABLE;
            break;
        default:
            // Fail-safe direction: REVISION_0, REVISION_1, REVISION_UNKNOWN (0xFE) and any
            // unrecognised byte keep TODAY'S stripping -- adds nothing. Matches
            // rurp_hw_rev_utils.h:33-37's own `default` leaving ctrl_reg = 0. Deliberately an
            // explicit four-case set above, never a `>= REVISION_2_0` range test:
            // rurp_shield.h:25-31 numbers revisions 0..5, and a range test would silently swallow
            // a future REVISION_2_4.
            break;
        }
    }
#endif
    top_address |= rurp_read_from_register(CONTROL_REGISTER) & mask;

    if (handle->pins == 28) {
        top_address |= CTRL_ADDRESS_LINE_17;
    }
    return top_address;
}

/* Shared VPP-mismatch report:
 *   [measured_V u16 BE][measured_tenths u16 BE][expected_V u16 BE][expected_tenths u16 BE]
 *
 * Severity rides entirely in msg_id -- every LOG_{WARN,ERROR}_ID_BYTES macro is
 * the same alias of LOG_ID_BYTES.
 *
 * Both millivolt parameters are uint16_t DELIBERATELY: at every call site the
 * operands are uint16_t, so `(x + 50)` promotes to 16-bit and `/1000` compiles
 * to __udivmodhi4. Widening either to uint32_t swaps in the 32-bit
 * __udivmodsi4 and moves the wrap point above 65485 mV. Do not widen them. */
void mem_util_report_voltage(firestarter_handle_t* handle, uint16_t measured_mv,
                              uint16_t expected_mv, uint8_t msg_id, uint8_t response_code) {
    uint16_t _v0 = (uint16_t)((measured_mv + 50) / 1000);
    uint16_t _v1 = (uint16_t)((((measured_mv + 50) / 100) % 10));
    uint16_t _v2 = (uint16_t)((expected_mv + 50) / 1000);
    uint16_t _v3 = (uint16_t)((((expected_mv + 50) / 100) % 10));
    uint8_t _b[8];
    _b[0] = (uint8_t)((_v0 >> 8) & 0xFF);
    _b[1] = (uint8_t)(_v0 & 0xFF);
    _b[2] = (uint8_t)((_v1 >> 8) & 0xFF);
    _b[3] = (uint8_t)(_v1 & 0xFF);
    _b[4] = (uint8_t)((_v2 >> 8) & 0xFF);
    _b[5] = (uint8_t)(_v2 & 0xFF);
    _b[6] = (uint8_t)((_v3 >> 8) & 0xFF);
    _b[7] = (uint8_t)(_v3 & 0xFF);
    LOG_ID_BYTES(msg_id, _b, 8);
    handle->response_code = response_code;
}

/* Shared chip-ID-mismatch report:
 *   [actual_id_hi u8][actual_id_lo u8][expected_id_hi u8][expected_id_lo u8]
 *
 * Matching ids emit nothing -- the early return is the mismatch guard hoisted
 * out of the four former call sites.
 *
 * Severity is the CALLER's decision, passed as warn_only, and must stay that
 * way: eprom.cpp's standalone CMD_CHECK_CHIP_ID path refuses unconditionally,
 * independent of FLAG_FORCE, while eprom_generic_init derives warn_only from
 * it. Folding is_flag_set(FLAG_FORCE) in here would make the standalone check
 * start honouring --force, which it must not.
 *
 * The id and the response_code both derive from that one boolean, so they
 * cannot be transposed -- unlike mem_util_report_voltage, which takes them
 * separately because its under-voltage arm needs a pairing the over-voltage
 * arm does not. */
void mem_util_report_chip_id(firestarter_handle_t* handle, uint16_t actual, bool warn_only) {
    if (actual == handle->chip_id) {
        return;
    }
    uint8_t _b[4];
    _b[0] = (uint8_t)((actual >> 8) & 0xFF);
    _b[1] = (uint8_t)(actual & 0xFF);
    _b[2] = (uint8_t)((handle->chip_id >> 8) & 0xFF);
    _b[3] = (uint8_t)(handle->chip_id & 0xFF);
    LOG_ID_BYTES(warn_only ? MSG_WARN_CHIP_ID_MISMATCH : MSG_ERR_CHIP_ID_MISMATCH, _b, 4);
    handle->response_code = warn_only ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR;
}

void mem_util_split_delay(uint32_t us, uint32_t* out_ms, uint16_t* out_us) {
    if (us <= MEM_UTIL_DELAY_US_MAX) {
        *out_ms = 0;
        *out_us = (uint16_t)us;  // <= 16383, fits and is accurate
        return;
    }
    *out_ms = us / 1000UL;
    *out_us = (uint16_t)(us % 1000UL);  // <= 999, always under the ceiling
}

void mem_util_delay_us(uint32_t us) {
    uint32_t ms;
    uint16_t rem;
    mem_util_split_delay(us, &ms, &rem);
    if (ms) {
        delay(ms);  // unsigned long -- 32-bit safe
    }
    if (rem) {
        delayMicroseconds(rem);
    }
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
     * Zero-ambiguity: 0 = no settling delay (explicit zero — a valid test
     * point). Knob: "read-settling-delay" JSON field -> handle->read_settling_us.
     * Read-timing cap: value already clamped at parse time (READ_TIMING_MAX_US=1000µs);
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
     * Read-timing cap: clamped at parse time; secondary guard here for safety.
     */
    uint32_t strobe = handle->read_strobe_us ? handle->read_strobe_us : 3UL;
    if (strobe > 1000UL) strobe = 1000UL;   /* read-timing secondary guard */
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
    rurp_chip_enable();
    mem_util_delay_us(handle->pulse_delay);
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

/* Saved address for the multi-call blank check, held across the two dispatch
 * calls that make up one mem_util_blank_check invocation.
 *
 * A file-scope static is safe here: the firmware runs strictly one command at
 * a time -- single-threaded, no reentrancy, no nesting -- and the value is
 * written only in the first-call branch and read only in a later call's
 * completion branch, so write-before-read holds by construction.
 *
 * Do not replace it with a heap allocation. The previous 4-byte malloc was the
 * only malloc/free in the firmware and pulled in the whole 586 B avr-libc
 * allocator, for a result that was dereferenced without a NULL test on a part
 * with a few hundred bytes of shared heap-and-stack headroom. */
static uint32_t blank_check_saved_address;

/* One progress frame and one host ack round-trip are spent per chunk, so this
 * also sets how chatty a blank check is: a 512 KB part is 64 chunks here, where
 * 2048 made it 256 for no extra visible resolution. */
#define BLANK_CHECK_CHUNK_SIZE 8192
void uint32_to_bytes(char* buffer, int pos, uint32_t value) {
    buffer[pos] = (value >> 24) & 0xFF;
    buffer[pos++] = (value >> 16) & 0xFF;
    buffer[pos++] = (value >> 8) & 0xFF;
    buffer[pos++] = value & 0xFF;
}

/* The single resolution point for D-04's "0 = absent = whole device"
 * fallback and for the fail-closed clamp. A host or a corrupted frame
 * cannot widen a scan past the device: region_end above mem_size clamps
 * down to mem_size exactly like region_end == 0 does. See
 * include/memory_utils.h for why this is a function rather than an
 * inline ternary. */
uint32_t mem_util_operation_end(const firestarter_handle_t* handle) {
    if (handle->region_end == 0 || handle->region_end > handle->mem_size) {
        return handle->mem_size;
    }
    return handle->region_end;
}

/* The region-scoped scan body. mem_util_blank_check (below) is a one-line
 * wrapper over this passing (0, handle->mem_size) -- the whole-device
 * entry point six function-pointer assignments and two direct calls in
 * other protocol files target (plan 201-05's source-contract gate
 * enumerates all of them). start is read only on the first call, inside
 * the !is_operation_in_progress branch: on later calls handle->address is
 * the scan cursor and start is ignored. */
void mem_util_blank_check_region(firestarter_handle_t* handle, uint32_t start, uint32_t end) {
    if (!is_operation_in_progress(handle)) {
        set_operation_in_progress(handle);
        blank_check_saved_address = handle->address;
        handle->address = start;
    } else {
        if (handle->address >= end) {
            clear_operation_in_progress(handle);
            handle->address = blank_check_saved_address;
            return;
        }
    }

    // for (uint32_t i = handle->address; i < handle->address + BLANK_CHECK_CHUNK_SIZE; i++) {
    uint32_t end_address = handle->address + BLANK_CHECK_CHUNK_SIZE;
    for (uint32_t i = handle->address; i < end_address && i < end; i++) {
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
    uint32_to_bytes(handle->data_buffer, 4, end);
    handle->data_size = 8;
#else
    if (handle->address > end) {
        handle->address = end;
    }
    // Send progress back to the client. For the standalone blank-check command the
    // emit is deferred to _single_step_operation_callback (communication mode): this
    // function runs in programmer mode where the Uno's com_mode-gated rurp_log_id
    // drops frames. Other callers (write-init / erase-end) keep the direct emit.
    // (#transport-protocol-verify)
    if (handle->cmd != CMD_BLANK_CHECK) {
        LOG_DATA_ID_U32_U32(MSG_DATA_PROGRESS, handle->address, end);
    }
#endif
}

/* Whole-device entry point. Six function-pointer assignments and two direct
 * calls in other protocol files (flash_intel.cpp, flash_nor_unlock.cpp)
 * target this wrapper -- plan 201-05's source-contract gate enumerates all
 * of them. It cannot drift from the region form: it is the region form,
 * called with the whole device as its region. */
void mem_util_blank_check(firestarter_handle_t* handle) {
    mem_util_blank_check_region(handle, 0, handle->mem_size);
}
