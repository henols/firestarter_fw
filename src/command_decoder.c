/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "command_decoder.h"

#include "rurp_voltage_math.h"

#include <string.h>

_Static_assert(CF_ADDRESS_LINES == ADDRESS_LINES_SIZE,
               "command_frame.toml limits.address_lines must equal ADDRESS_LINES_SIZE");
_Static_assert(CF_BUS_LINES <= 32, "a bus line must be a valid shift count for a uint32_t mask");

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool bus_line_valid(uint8_t line) {
    return line == CF_LINE_ABSENT || line < CF_BUS_LINES;
}

static uint8_t validate_memory(uint8_t cmd, const uint8_t* p) {
    const uint8_t* lines = p + CF_MEMORY_OFF_ADDRESS_LINES;
    uint32_t seen = 0;
    bool ended = false;
    for (uint8_t i = 0; i < CF_ADDRESS_LINES; i++) {
        if (lines[i] == CF_LINE_ABSENT) {
            ended = true;
            continue;
        }
        if (ended) {
            return CF_REFUSE_ADDRESS_GAP;
        }
        if (lines[i] >= CF_BUS_LINES) {
            return CF_REFUSE_ADDRESS_LINE;
        }
        uint32_t bit = (uint32_t)1 << lines[i];
        if (seen & bit) {
            return CF_REFUSE_ADDRESS_DUPLICATE;
        }
        seen |= bit;
    }
    if (!bus_line_valid(p[CF_MEMORY_OFF_RW_LINE]) || !bus_line_valid(p[CF_MEMORY_OFF_VPP_LINE])) {
        return CF_REFUSE_CONTROL_LINE;
    }
    if (be32(p + CF_MEMORY_OFF_STATIC_HIGH_MASK) >> CF_BUS_LINES) {
        return CF_REFUSE_STATIC_HIGH;
    }
    if (be32(p + CF_MEMORY_OFF_READ_SETTLING_US) > CF_READ_TIMING_MAX_US ||
        be32(p + CF_MEMORY_OFF_READ_STROBE_US) > CF_READ_TIMING_MAX_US) {
        return CF_REFUSE_READ_TIMING;
    }
    /* Every host memory command carries these four, even for 5V-only
     * families. Zero means omitted; refuse it before configuring hardware.
     * Dev commands 7 and 8 share the payload but not this rule. */
    if (is_memory_cmd(cmd) &&
        (be32(p + CF_MEMORY_OFF_MEM_SIZE) == 0 || p[CF_MEMORY_OFF_PROTOCOL] == 0 ||
         p[CF_MEMORY_OFF_PINS] == 0 || be16(p + CF_MEMORY_OFF_VPP_MV) == 0)) {
        return CF_REFUSE_MISSING_FIELD;
    }
    /* No chip in the database needs more; the JSON parser refused it too. */
    if (be16(p + CF_MEMORY_OFF_VPP_MV) > CF_VPP_MAX_MV) {
        return CF_REFUSE_VPP_RANGE;
    }
    /* Refuse a bad cursor before configure_memory() touches bus registers
     * (#120). A read uses the whole device; a write may end at region_end. */
    if (cmd == CMD_READ || cmd == CMD_WRITE) {
        uint32_t mem_size = be32(p + CF_MEMORY_OFF_MEM_SIZE);
        uint32_t end = mem_size;
        if (cmd == CMD_WRITE) {
            uint32_t region_end = be32(p + CF_MEMORY_OFF_REGION_END);
            if (region_end != 0 && region_end < mem_size) {
                end = region_end;
            }
        }
        if (be32(p + CF_MEMORY_OFF_ADDRESS) > end) {
            return CF_REFUSE_ADDRESS_RANGE;
        }
    }
    return CMD_DECODE_OK;
}

/* Every command parameter comes from the payload, so no value can survive
 * from a previous command. */
static void apply_memory(firestarter_handle_t* handle, const uint8_t* p) {
    handle->mem_size = be32(p + CF_MEMORY_OFF_MEM_SIZE);
    handle->address = be32(p + CF_MEMORY_OFF_ADDRESS);
    handle->region_end = be32(p + CF_MEMORY_OFF_REGION_END);
    handle->protocol = p[CF_MEMORY_OFF_PROTOCOL];
    handle->pins = p[CF_MEMORY_OFF_PINS];
    handle->vpp_mv = be16(p + CF_MEMORY_OFF_VPP_MV);
    handle->pulse_delay = be32(p + CF_MEMORY_OFF_PULSE_DELAY);
    handle->chip_id = be16(p + CF_MEMORY_OFF_CHIP_ID);
    handle->page_size = be16(p + CF_MEMORY_OFF_PAGE_SIZE);
    handle->read_settling_us = be32(p + CF_MEMORY_OFF_READ_SETTLING_US);
    handle->read_strobe_us = be32(p + CF_MEMORY_OFF_READ_STROBE_US);

    bus_config_t* bus = &handle->bus_config;
    memcpy(bus->address_lines, p + CF_MEMORY_OFF_ADDRESS_LINES, CF_ADDRESS_LINES);
    bus->rw_line = p[CF_MEMORY_OFF_RW_LINE];
    bus->vpp_line = p[CF_MEMORY_OFF_VPP_LINE];
    bus->static_high_mask = be32(p + CF_MEMORY_OFF_STATIC_HIGH_MASK);

    /* Derived exactly as the JSON parser derived them: matching_lines is the
     * count of leading lines that map An to bus line n, and a frame with no
     * address lines passes the low 16 address bits straight through. */
    bus->address_mask = 0;
    bus->matching_lines = 0;
    uint8_t count = 0;
    while (count < CF_ADDRESS_LINES && bus->address_lines[count] != CF_LINE_ABSENT) {
        bus->address_mask |= (uint32_t)1 << bus->address_lines[count];
        if (bus->matching_lines == count && bus->address_lines[count] == count) {
            bus->matching_lines = count + 1;
        }
        count++;
    }
    if (count == 0) {
        bus->address_mask = 0xFFFF;
    }
}

static uint8_t validate_config(const uint8_t* p) {
    uint8_t present = p[CF_CONFIG_OFF_PRESENT];
    if (present & (uint8_t)~CF_CONFIG_PRESENT_MASK) {
        return CF_REFUSE_CONFIG_FIELD;
    }
#ifndef HARDWARE_REVISION
    if (present & CF_CONFIG_PRESENT_REV) {
        return CF_REFUSE_CONFIG_FIELD;
    }
#endif
#if !RURP_HAS_BANDGAP_CAL
    // This port never reads the stored bandgap, so it takes no calibration.
    if (present & CF_CONFIG_PRESENT_BG) {
        return CF_REFUSE_CONFIG_FIELD;
    }
#endif
    /* 0xFF clears the override; any other value must name a revision. */
    uint8_t rev = p[CF_CONFIG_OFF_REV];
    if ((present & CF_CONFIG_PRESENT_REV) && rev != 0xFF && rev > CF_REVISION_MAX) {
        return CF_REFUSE_REVISION;
    }
    return CMD_DECODE_OK;
}

uint8_t cmd_decode(firestarter_handle_t* handle) {
    const uint8_t* frame = (const uint8_t*)handle->data_buffer;
    uint32_t size = handle->data_size;
    if (size < CF_HEADER_SIZE || frame[CF_OFF_MAGIC] != CF_MAGIC) {
        return CMD_DECODE_BAD_FRAME;
    }
    if (frame[CF_OFF_VERSION] != CF_VERSION) {
        return CMD_DECODE_BAD_VERSION;
    }

    // Locals until the whole frame passes: a refused frame writes no handle field.
    uint8_t cmd = frame[CF_OFF_CMD];
    uint16_t flags = be16(frame + CF_OFF_FLAGS);
    if (flags & (uint16_t)~CF_FLAGS_MASK) {
        return CF_REFUSE_FLAGS;
    }

    uint8_t kind = cf_payload_kind(cmd);
    uint16_t expected = kind == CF_KIND_MEMORY ? CF_MEMORY_SIZE : kind == CF_KIND_CONFIG ? CF_CONFIG_SIZE : 0;
    uint16_t length = be16(frame + CF_OFF_LENGTH);
    if (length != expected || size != (uint32_t)CF_HEADER_SIZE + length) {
        return CF_REFUSE_LENGTH;
    }

    const uint8_t* payload = frame + CF_HEADER_SIZE;
    if (kind == CF_KIND_MEMORY) {
        uint8_t res = validate_memory(cmd, payload);
        if (res != CMD_DECODE_OK) {
            return res;
        }
        handle->cmd = cmd;
        handle->ctrl_flags = flags;
        apply_memory(handle, payload);
        return CMD_DECODE_OK;
    }
    if (kind == CF_KIND_CONFIG) {
        uint8_t res = validate_config(payload);
        if (res != CMD_DECODE_OK) {
            return res;
        }
    }
    handle->cmd = cmd;
    handle->ctrl_flags = flags;
    // No field of an earlier chip may stay in the handle.
    handle->protocol = 0;
    handle->pulse_delay = 0;
    return CMD_DECODE_OK;
}

uint8_t cmd_apply_config(const firestarter_handle_t* handle, rurp_configuration_t* config) {
    const uint8_t* p = (const uint8_t*)handle->data_buffer + CF_HEADER_SIZE;
    uint8_t present = p[CF_CONFIG_OFF_PRESENT];
    uint8_t res = 0;
    if (present & CF_CONFIG_PRESENT_R1) {
        config->r1 = (long)be32(p + CF_CONFIG_OFF_R1);
        res |= CMD_CFG_FIELD_CHANGED;
    }
    if (present & CF_CONFIG_PRESENT_R2) {
        config->r2 = (long)be32(p + CF_CONFIG_OFF_R2);
        res |= CMD_CFG_FIELD_CHANGED;
    }
#ifdef HARDWARE_REVISION
    if (present & CF_CONFIG_PRESENT_REV) {
        config->hardware_revision = p[CF_CONFIG_OFF_REV];
        res |= CMD_CFG_FIELD_CHANGED;
    }
#endif
    if (present & CF_CONFIG_PRESENT_BG) {
        config->bandgap_mv = be16(p + CF_CONFIG_OFF_BG);
        res |= CMD_CFG_FIELD_CHANGED;
    }
    return res;
}
