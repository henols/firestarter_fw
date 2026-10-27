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
#include "rurp_voltage_math.h"
#include "rurp_pinout.h"
#include "sram.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// AVR delayMicroseconds() accurate ceiling. See mem_util_delay_us and
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
    // would. The payload is the COMMAND ordinal, not the protocol ordinal. On AVR
    // targets RURP_PINMAP_PROVISIONAL is never defined, so this compiles to nothing.
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
    }

    handle->firestarter_get_data = memory_get_data;
    handle->firestarter_set_data = memory_set_data;

    handle->firestarter_set_address = mem_util_set_address;

    handle->firestarter_set_control_register = memory_set_control_register;
    handle->firestarter_get_control_register = memory_get_control_register;

    // Pick the handler first. A protocol that reaches no handler is refused
    // before the bus is touched. FWH and GAL/PLD (0x11, 0x2A-0x2C) are
    // infeasible on RURP; every other unrecognised value, including 0, takes
    // the same refusal. Dispatch trusts only handle->protocol.
    void (*configure)(firestarter_handle_t*) = NULL;
    if (handle->protocol == PROTO_FLASH_INTEL) {
        configure = configure_flash_intel;
    } else if (handle->protocol == PROTO_EEPROM_PARALLEL) {
        configure = configure_eeprom28c;
    } else if (handle->protocol == PROTO_FLASH_NOR_UNLOCK) {
        configure = configure_flash_nor_unlock;
    } else if (handle->protocol == PROTO_FLASH_5V_PAGE || handle->protocol == PROTO_PHANTOM_0x35 || handle->protocol == PROTO_PHANTOM_0x39) {
        configure = configure_flash_5v_page;
    } else if (handle->protocol == PROTO_EPROM_28PIN || handle->protocol == PROTO_EPROM_32PIN || handle->protocol == PROTO_EPROM_24PIN) {
        configure = configure_eprom;
    } else if (handle->protocol == PROTO_SRAM_32PIN || handle->protocol == PROTO_SRAM_24PIN ||
               handle->protocol == PROTO_SRAM_28PIN || handle->protocol == PROTO_SRAM_32PIN_NVRAM) {
        configure = configure_sram;
    }
    if (configure == NULL) {
        configure_not_implemented(handle);
        return;
    }

    mem_util_set_address(handle, 0);
    configure(handle);
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
    // always preserved below. Every address write (the one caller of this function) writes
    // CONTROL_REGISTER on every byte, for both the pulse and the verify; the preserve mask is
    // what carries the route bit across that write.
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
            // This arm reaches every 32-pin protocol on Rev 2-class (0x0E, 0x29, 0x10, 32-pin
            // flash), not just the EPROM family. None of them sets the drop bit, so the
            // preserve introduces nothing.
            mask |= CTRL_VPP_VPE_DROP_ENABLE;
            break;
        default:
            // Unknown and older hardware revisions retain conservative bit
            // stripping. Match explicit supported revisions rather than a
            // range that might include untested future hardware.
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

// Applies the VPP acceptance window to a measured reading. HIGH is a refusal
// above target + max(500 mV, 3 %), downgraded to a warning by FLAG_FORCE. LOW is
// a warning below 95 % of target.
void mem_util_check_vpp_window(firestarter_handle_t* handle, uint16_t vpp_mv) {
    uint32_t target = (uint32_t)handle->vpp_mv;
    uint32_t headroom = target * RURP_VPP_HIGH_HEADROOM_PCT / 100;
    if (headroom < RURP_VPP_HIGH_HEADROOM_FLOOR_MV) {
        headroom = RURP_VPP_HIGH_HEADROOM_FLOOR_MV;
    }
    if ((uint32_t)vpp_mv > target + headroom) {
        // HIGH stays a hard error unless FLAG_FORCE: an over-set rail damages
        // the part, where an under-set one only fails to program.
        bool force = is_flag_set(FLAG_FORCE);
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv,
                                force ? MSG_WARN_VPP_HIGH : MSG_ERR_VPP_HIGH,
                                force ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
    } else if ((uint32_t)vpp_mv < target * (100 - RURP_VPP_LOW_TOLERANCE_PCT) / 100) {
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv, MSG_WARN_VPP_LOW,
                                RESPONSE_CODE_WARNING);
    }
}

void mem_util_warn_if_uncalibrated(firestarter_handle_t* handle) {
#if RURP_HAS_BANDGAP_CAL
    rurp_configuration_t* cfg = rurp_get_config();
    if (cfg->bandgap_mv != (uint16_t)RURP_BANDGAP_NOMINAL_MV) {
        return;
    }
    LOG_WARN_ID(MSG_WARN_NOT_CALIBRATED);
    handle->response_code = RESPONSE_CODE_WARNING;
#else
    // No calibration exists to do, so there is nothing to warn about.
    (void)handle;
#endif
}

bool mem_util_refuse_bad_calibration(firestarter_handle_t* handle) {
    rurp_configuration_t* cfg = rurp_get_config();
    if (rurp_calibration_is_plausible((uint32_t)cfg->r1, (uint32_t)cfg->r2, cfg->bandgap_mv)) {
        return false;
    }
    uint8_t _cal[8];
    uint32_t r1 = (uint32_t)cfg->r1;
    uint32_t r2 = (uint32_t)cfg->r2;
    _cal[0] = (uint8_t)((r1 >> 24) & 0xFF);
    _cal[1] = (uint8_t)((r1 >> 16) & 0xFF);
    _cal[2] = (uint8_t)((r1 >>  8) & 0xFF);
    _cal[3] = (uint8_t)((r1      ) & 0xFF);
    _cal[4] = (uint8_t)((r2 >> 24) & 0xFF);
    _cal[5] = (uint8_t)((r2 >> 16) & 0xFF);
    _cal[6] = (uint8_t)((r2 >>  8) & 0xFF);
    _cal[7] = (uint8_t)((r2      ) & 0xFF);
    LOG_ERROR_ID_BYTES(MSG_ERR_CALIBRATION, _cal, 8);
    handle->response_code = RESPONSE_CODE_ERROR;
    return true;
}

/* Shared VPP-mismatch report:
 *   [measured_V u16 BE][measured_tenths u16 BE][expected_V u16 BE][expected_tenths u16 BE]
 *
 * Both millivolt parameters are uint16_t deliberately: `(x + 50)` then
 * promotes to 16-bit and `/1000` compiles to __udivmodhi4 on AVR. Do not
 * widen them. */
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
    if (handle->address > handle->mem_size) {
        handle->data_size = 0;
        LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
    const uint32_t remaining = handle->mem_size - handle->address;
    const int buf_size = (int)min(remaining, (uint32_t)DATA_BUFFER_SIZE);
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
    if (handle->data_size > DATA_BUFFER_SIZE ||
        !mem_util_range_fits(handle->address, handle->data_size,
                             mem_util_operation_end(handle))) {
        LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
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
    if (handle->data_size > DATA_BUFFER_SIZE ||
        !mem_util_range_fits(handle->address, handle->data_size, handle->mem_size)) {
        LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
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

    // Set VPP line to high if VPP is not on P1.
    // A chip that takes VPP on P1 (2764/27128-class) still needs P1 at VCC
    // level while it is read: with the line low the outputs read marginally
    // (stray 0x7F/0xFE bytes on a blank 2764). Drive it high for reads, but
    // only while the P1 programming route is off, so the line never sits
    // high against VPP.
    if (config.vpp_line != 0xFF) {
        if (!using_p1_as_vpp(handle)) {
            reorg_address |= 1UL << config.vpp_line;
        } else if (read_write == READ_FLAG &&
                   !handle->firestarter_get_control_register((firestarter_handle_t*)handle, CTRL_VPP_P1_ENABLE)) {
            reorg_address |= 1UL << config.vpp_line;
        }
    }

    reorg_address |= config.static_high_mask;
    return reorg_address;
}

/* Resolve the operation end in one place and clamp invalid region ends to
 * mem_size; zero denotes a whole-device operation. */
uint32_t mem_util_operation_end(const firestarter_handle_t* handle) {
    if (handle->region_end == 0 || handle->region_end > handle->mem_size) {
        return handle->mem_size;
    }
    return handle->region_end;
}
