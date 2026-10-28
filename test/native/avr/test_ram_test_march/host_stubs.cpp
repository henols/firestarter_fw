/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Suite-specific extensions: a bus-level SRAM model for the RAM test.
 *   - HOST_STUBS_RECORD_BUS: every rurp_write_to_register call is recorded
 *     (no cache elision), so the latest LSB/MSB/CONTROL entry is what the
 *     latches hold. The buffer is sized for a full 2 KB March C- run.
 *   - HOST_STUBS_CUSTOM_DATA_BUFFER / _CONTROL_PIN / _READ_DATA_BUFFER: the
 *     model drives the data bus. A CE falling edge with WE low stores the
 *     data bus at the latched address; a read returns the stored byte.
 *
 * Address decode matches sram_model_bus_config() in the test: no remap,
 * A0-A10 on LSB/MSB, WE on bus line 14 (MSB bit 6, high = read).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

#define HOST_STUBS_RECORD_BUS
#define HOST_STUBS_MAX_RECORDING 131072
#define HOST_STUBS_CUSTOM_DATA_BUFFER
#define HOST_STUBS_CUSTOM_CONTROL_PIN
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

#define SRAM_MODEL_SIZE 2048
#define SRAM_MODEL_WE_BIT 0x40  // MSB bit 6 = bus line 14

static uint8_t s_sram[SRAM_MODEL_SIZE];
static uint8_t s_data_bus;
static uint32_t s_stuck_at_one_address = 0xFFFFFFFF;
static uint8_t s_stuck_at_one_mask;

static rurp_register_t sram_model_latch(uint8_t reg) {
    for (int i = s_bus_recording_count - 1; i >= 0; i--) {
        if (s_bus_recording[i].reg == reg) {
            return s_bus_recording[i].data;
        }
    }
    return 0;
}

static uint32_t sram_model_address() {
    uint32_t address = ((uint32_t)(sram_model_latch(MOST_SIGNIFICANT_BYTE) & 0xFF) << 8) |
                       (sram_model_latch(LEAST_SIGNIFICANT_BYTE) & 0xFF);
    return address & (SRAM_MODEL_SIZE - 1);
}

static bool sram_model_we_low() {
    return (sram_model_latch(MOST_SIGNIFICANT_BYTE) & SRAM_MODEL_WE_BIT) == 0;
}

static uint8_t sram_model_cell(uint32_t address) {
    uint8_t value = s_sram[address];
    if (address == s_stuck_at_one_address) {
        value |= s_stuck_at_one_mask;
    }
    return value;
}

extern "C" void sram_model_reset(uint8_t fill) {
    memset(s_sram, fill, sizeof(s_sram));
    s_data_bus = 0;
    s_stuck_at_one_address = 0xFFFFFFFF;
    s_stuck_at_one_mask = 0;
}

extern "C" void sram_model_stuck_at_one(uint32_t address, uint8_t mask) {
    s_stuck_at_one_address = address;
    s_stuck_at_one_mask = mask;
}

extern "C" uint8_t sram_model_peek(uint32_t address) {
    return sram_model_cell(address);
}

extern "C" void rurp_write_data_buffer(uint8_t data) {
    s_data_bus = data;
}

extern "C" void rurp_set_control_pin(uint8_t pin, uint8_t state) {
    if (pin == CHIP_ENABLE && state == 0 && sram_model_we_low()) {
        s_sram[sram_model_address()] = s_data_bus;
    }
}

extern "C" uint8_t rurp_read_data_buffer() {
    return sram_model_cell(sram_model_address());
}
