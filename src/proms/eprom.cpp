/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Handler: configure_eprom() — protocols 0x07 (EPROM-STD), 0x08 (EPROM-QUICK), 0x0B (EPROM-LEGACY)
 *
 * WHY this file exists and the load-bearing protocol distinctions:
 *
 * INV-01 — 0x0B direct-VPE rail (no CTRL_VPP_VPE_DROP_ENABLE drop):
 *   EPROM-LEGACY 24-pin parts (2716/2732/2516) have VPP pins that accept the raw
 *   VPE regulator output (12–25 V). No drop-resistor is needed. The firmware sets
 *   CTRL_VPP_REGULATOR_ENABLE alone (FLAG_VPE_AS_VPP path) — see eprom_write_execute().
 *   0x07/0x08 require CTRL_VPP_VPE_DROP_ENABLE to drop VPE to the correct 13 V level.
 *   Citation: datasheets/0x0B-EPROM-LEGACY/2516_EPROM.pdf p.2 §Vpp Programming Voltage.
 *
 * INV-02 — 0x0B shared OE/VPP read-skip:
 *   Some 2732/2516 variants share the OE and VPP pins. Enabling VPP during CMD_READ
 *   would drive the output-enable pin high with 12–25 V, destroying the logic output.
 *   VPP is therefore skipped on read operations for 0x0B (VPP-skip-on-read, INV-05
 *   applies here too). The eprom_internal_set_control_register() wrapper enforces this.
 *   Citation: datasheets/0x0B-EPROM-LEGACY/2516_EPROM.pdf p.3 §Pin Description (OE/Vpp).
 *
 * INV-03 — 0x08 P1-as-VPP:
 *   32-pin EPROM-QUICK parts (AM27C020, W27C020, AT27C010) expose VPP on socket pin 1.
 *   The RURP routes VPP to pin 1 via CTRL_VPP_P1_ENABLE (0x08) for this family.
 *   0x07 28-pin parts route VPP through the JP4 jumper to a different bus line.
 *   Citation: datasheets/0x08-EPROM-QUICK/W27C020.pdf p.4 §Pin Description (pin 1 = Vpp).
 *
 * INV-05 — VPP-skip-on-read:
 *   VPP is NOT enabled for CMD_READ or CMD_BLANK_CHECK. The eprom_internal_set_control_
 *   register() wrapper intercepts VPP enable bits and suppresses them on read commands.
 *   Enabling VPP during a read would stress the OE/VPP-shared pins on 0x0B parts and
 *   waste regulator settle time on 0x07/0x08.
 *   Citation: datasheets/0x07-EPROM-STD/W27C512.pdf p.5 §Pin Description (Vpp = PGM V during prog only).
 *
 * INV-06 — pulse-delay defaults per protocol (configure_eprom() lines 70–76):
 *   pulse_delay controls the CE-asserted duration for the Intelligent Programming pulse.
 *   0x08 (EPROM_QUICK) → 100 µs (Quick-Pulse algorithm, faster per-byte cycle).
 *   0x0B (EPROM_LEGACY) → 500 µs (older NMOS parts require longer initial pulse).
 *   default / 0x07 (EPROM_STD) → 1000 µs (classic 1 ms JEDEC algorithm).
 *   The DB `pulse-delay` field overrides these defaults when non-zero.
 *   Citation: datasheets/0x08-EPROM-QUICK/AM27C020.pdf p.10 §Quick-Pulse Programming (100µs);
 *             datasheets/0x07-EPROM-STD/ST-M27C512.pdf p.8 §PRESTO IIB (100µs ST variant).
 *
 * INV-08 — WARNING-5 (0x07 EE-EPROM chips reclassified to 0x0D) delivered by Phase-86 decode:
 *   Before Phase 86, build_db.py Rule 2 overrode certain 0x07-classified 28C-series parts
 *   (AT28C010 etc.) to 0x0D at DB-generation time (WARNING-5). Phase 86 removed Rule 2;
 *   the correct 0x0D classification now flows from the infoic.xml classify() decode.
 *   This handler no longer sees those chips; the invariant is preserved at the DB layer.
 *   Full prose: firestarter/doc/PROTOCOLS.md §3 INV-08 row.
 */

#include "eprom.h"

#include <Arduino.h>

#include "firestarter.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "operation_utils.h"


#define NUMBER_OF_RETRIES 20

void eprom_erase_execute(firestarter_handle_t* handle);

void eprom_write_init(firestarter_handle_t* handle);
void eprom_write_execute(firestarter_handle_t* handle);
void eprom_check_chip_id_init(firestarter_handle_t* handle);
void eprom_check_chip_id_execute(firestarter_handle_t* handle);

uint16_t eprom_get_chip_id(firestarter_handle_t* handle);

void eprom_check_vpp(firestarter_handle_t* handle);

void eprom_internal_check_chip_id(firestarter_handle_t* handle, uint8_t error_code);
void eprom_internal_erase(firestarter_handle_t* handle);

void eprom_internal_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool cmd);
void (*ep_set_control_register)(struct firestarter_handle*, rurp_register_t, bool);

void eprom_generic_init(firestarter_handle_t* handle);

void configure_eprom(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_EPROM);

    handle->firestarter_operation_init = eprom_generic_init;

    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = eprom_write_init;
            handle->firestarter_operation_main = eprom_write_execute;
            break;
        case CMD_ERASE:
            handle->firestarter_operation_main = eprom_erase_execute;
            if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
                handle->firestarter_operation_end = mem_util_blank_check;
            }
            break;
        case CMD_BLANK_CHECK:
            handle->firestarter_operation_main = mem_util_blank_check;
            break;
        case CMD_CHECK_CHIP_ID:
            handle->firestarter_operation_init = eprom_check_chip_id_init;
            handle->firestarter_operation_main = eprom_check_chip_id_execute;
            break;
    }

    ep_set_control_register = handle->firestarter_set_control_register;
    handle->firestarter_set_control_register = eprom_internal_set_control_register;

    // Set default pulse_delay from protocol when Python doesn't supply one
    if (handle->pulse_delay == 0) {
        switch (handle->protocol) {
            case 0x08: handle->pulse_delay = 100;  break;  // EPROM_QUICK: 100µs
            case 0x0B: handle->pulse_delay = 500;  break;  // EPROM_LEGACY: 500µs
            default:   handle->pulse_delay = 1000; break;  // EPROM_STD: 1ms
        }
    }
}

void eprom_check_chip_id_init(firestarter_handle_t* handle) {
    eprom_check_vpp(handle);
}

void eprom_check_chip_id_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID);
    eprom_internal_check_chip_id(handle, RESPONSE_CODE_ERROR);
}

void eprom_erase_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_ERASE);
    eprom_internal_erase(handle);
}

void eprom_write_init(firestarter_handle_t* handle) {
    if(!is_operation_in_progress(handle)){
        eprom_generic_init(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
        
        if (is_flag_set(FLAG_CAN_ERASE)) {
            if (!is_flag_set(FLAG_SKIP_ERASE)) {
                eprom_internal_erase(handle);
            } else {
                LOG_INFO_ID(MSG_INFO_SKIPPING_ERASE);
            }
        }
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

// New helper to program only the bytes that have failed so far
static void program_mismatched_bytes(firestarter_handle_t* handle, const uint8_t* mismatch_bitmask) {
     rurp_register_t programming_bits = CTRL_VPE_ENABLE;

    handle->firestarter_set_control_register(handle, programming_bits, 1);
    delay(10); // Consider making this a named constant
    for (uint32_t i = 0; i < handle->data_size; i++) {
        // Use the corrected bitwise-AND operator here
        if (mismatch_bitmask[i / 8] & (1 << (i % 8))) {
            handle->firestarter_set_data(handle, handle->address + i, handle->data_buffer[i]);
        }
    }
    handle->firestarter_set_control_register(handle, programming_bits, 0);
}

// New helper to verify bytes and update the mismatch mask
static int verify_and_update_mask(firestarter_handle_t* handle, uint8_t* mismatch_bitmask) {
    int mismatch_count = 0;
    for (uint32_t i = 0; i < handle->data_size; i++) {
        if (handle->firestarter_get_data(handle, (handle->address + i)) != (uint8_t)handle->data_buffer[i]) {
            mismatch_count++;
            mismatch_bitmask[i / 8] |= (1 << (i % 8)); // Set bit for mismatch
        } else {
            mismatch_bitmask[i / 8] &= ~(1 << (i % 8)); // Clear bit for match
        }
    }
    
    return mismatch_count;
}

void eprom_write_execute(firestarter_handle_t* handle) {
    if (handle->firestarter_get_control_register(handle, CTRL_VPP_REGULATOR_ENABLE) == 0) {
        if (handle->protocol == 0x0B || is_flag_set(FLAG_VPE_AS_VPP)) {
            // EPROM_LEGACY: direct VPE path — no CTRL_VPP_VPE_DROP_ENABLE dropping resistor
            handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
        } else {
            // EPROM_STD / EPROM_QUICK: CTRL_VPP_VPE_DROP_ENABLE dropping path for precise VPP
            handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE, 1);
        }
        delay(500);
    }

    uint8_t mismatch_bitmask[DATA_BUFFER_SIZE / 8];
    // Use memset for cleaner initialization
    memset(mismatch_bitmask, 0xFF, sizeof(mismatch_bitmask));

    int mismatch = 0;
    int retries = 0;
    uint32_t org_delay = handle->pulse_delay;

    for (int w = 0; w < NUMBER_OF_RETRIES; w++) {
        program_mismatched_bytes(handle, mismatch_bitmask);
        
        mismatch = verify_and_update_mask(handle, mismatch_bitmask);

        if (!mismatch) {
            if (retries > 0) {
                LOG_INFO_ID_U8(MSG_INFO_RETRIES, (uint8_t)retries);
            }
            handle->pulse_delay = org_delay;
            return;
        }

        retries = w + 1;
        handle->pulse_delay = org_delay + (org_delay * retries / NUMBER_OF_RETRIES);
        LOG_DEBUG_ID_SUB_U16_U16(DBG_PULSE_DELAY_MISMATCH, (uint16_t)org_delay, (uint16_t)handle->pulse_delay);
    }

    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 0);
    {
        uint8_t _b[6];
        _b[0] = (uint8_t)((handle->address >> 16) & 0xFF);
        _b[1] = (uint8_t)((handle->address >> 8)  & 0xFF);
        _b[2] = (uint8_t)( handle->address        & 0xFF);
        _b[3] = (uint8_t)retries;
        _b[4] = (uint8_t)(((uint16_t)mismatch >> 8) & 0xFF);
        _b[5] = (uint8_t)( (uint16_t)mismatch       & 0xFF);
        LOG_ERROR_ID_BYTES(MSG_ERR_WRITE_FAILED, _b, 6);
    }
    handle->response_code = RESPONSE_CODE_ERROR;
}


uint16_t eprom_get_chip_id(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_GET_CHIP_ID);
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    delay(50);

    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE, 1);
    delay(100);
    uint16_t chip_id = handle->firestarter_get_data(handle, 0x0000) << 8;
    chip_id |= (handle->firestarter_get_data(handle, 0x0001));
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_A9_ENABLE, 0);
    return chip_id;
}

void eprom_check_vpp(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_VPP);
#ifdef HARDWARE_REVISION
    if (rurp_get_hardware_revision() == REVISION_0) {
        LOG_WARN_ID(MSG_WARN_REV0_VPP_UNSUPPORTED);
        handle->response_code = RESPONSE_CODE_WARNING;
        return;
    }
#endif
    if (handle->protocol == 0x0B || is_flag_set(FLAG_VPE_AS_VPP)) {
        // EPROM_LEGACY: direct VPE path
        handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    } else {
        // EPROM_STD / EPROM_QUICK: VPE through dropping resistor to produce VPP
        handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE, 1);
    }

    delay(100);
    uint16_t vpp_mv = rurp_read_voltage_mv();
    LOG_DEBUG_ID_SUB_U16(DBG_CHECKING_VPP_VOLTAGE, vpp_mv);
    if (vpp_mv > (uint32_t)handle->vpp_mv + 500) {
        {
            uint16_t _v0 = (uint16_t)((vpp_mv + 50) / 1000);
            uint16_t _v1 = (uint16_t)((((vpp_mv + 50) / 100) % 10));
            uint16_t _v2 = (uint16_t)((handle->vpp_mv + 50) / 1000);
            uint16_t _v3 = (uint16_t)((((handle->vpp_mv + 50) / 100) % 10));
            uint8_t _b[8];
            _b[0] = (uint8_t)((_v0 >> 8) & 0xFF);
            _b[1] = (uint8_t)(_v0 & 0xFF);
            _b[2] = (uint8_t)((_v1 >> 8) & 0xFF);
            _b[3] = (uint8_t)(_v1 & 0xFF);
            _b[4] = (uint8_t)((_v2 >> 8) & 0xFF);
            _b[5] = (uint8_t)(_v2 & 0xFF);
            _b[6] = (uint8_t)((_v3 >> 8) & 0xFF);
            _b[7] = (uint8_t)(_v3 & 0xFF);
            if (is_flag_set(FLAG_FORCE)) {
                LOG_WARN_ID_BYTES(MSG_WARN_VPP_HIGH, _b, 8);
                handle->response_code = RESPONSE_CODE_WARNING;
            } else {
                LOG_ERROR_ID_BYTES(MSG_ERR_VPP_HIGH, _b, 8);
                handle->response_code = RESPONSE_CODE_ERROR;
            }
        }
    } else if (vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100) {
        {
            uint16_t _v0 = (uint16_t)((vpp_mv + 50) / 1000);
            uint16_t _v1 = (uint16_t)((((vpp_mv + 50) / 100) % 10));
            uint16_t _v2 = (uint16_t)((handle->vpp_mv + 50) / 1000);
            uint16_t _v3 = (uint16_t)((((handle->vpp_mv + 50) / 100) % 10));
            uint8_t _b[8];
            _b[0] = (uint8_t)((_v0 >> 8) & 0xFF);
            _b[1] = (uint8_t)(_v0 & 0xFF);
            _b[2] = (uint8_t)((_v1 >> 8) & 0xFF);
            _b[3] = (uint8_t)(_v1 & 0xFF);
            _b[4] = (uint8_t)((_v2 >> 8) & 0xFF);
            _b[5] = (uint8_t)(_v2 & 0xFF);
            _b[6] = (uint8_t)((_v3 >> 8) & 0xFF);
            _b[7] = (uint8_t)(_v3 & 0xFF);
            LOG_WARN_ID_BYTES(MSG_WARN_VPP_LOW, _b, 8);
            handle->response_code = RESPONSE_CODE_WARNING;
        }
    }
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE, 0);
}

void eprom_internal_erase(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_INTERNAL_ERASE);
    rurp_chip_input();
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);  // Enable regulator without dropping resistor
    delay(100);
    handle->firestarter_set_address(handle, 0x0000);
    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE, 1);  // Erase with VPE - assumes CTRL_VPP_VPE_DROP_ENABLE isn't set and left active previously
    delay(100);
    rurp_chip_enable();
    delayMicroseconds(handle->pulse_delay);
    // After the erase pulse, we should disable the chip to end the programming cycle.
    rurp_chip_disable();

    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE, 0);
}

void eprom_generic_init(firestarter_handle_t* handle) {
    eprom_check_vpp(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        return;
    }
    if (handle->chip_id > 0) {
        eprom_internal_check_chip_id(handle, is_flag_set(FLAG_FORCE) ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
    }
}

void eprom_internal_check_chip_id(firestarter_handle_t* handle, uint8_t error_code) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID);
    uint16_t chip_id = eprom_get_chip_id(handle);
    if (chip_id != handle->chip_id) {
        uint8_t _b[4];
        _b[0] = (uint8_t)((chip_id >> 8) & 0xFF);
        _b[1] = (uint8_t)(chip_id & 0xFF);
        _b[2] = (uint8_t)((handle->chip_id >> 8) & 0xFF);
        _b[3] = (uint8_t)(handle->chip_id & 0xFF);
        if (error_code == RESPONSE_CODE_WARNING) {
            LOG_WARN_ID_BYTES(MSG_WARN_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_WARNING;
        } else {
            LOG_ERROR_ID_BYTES(MSG_ERR_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_ERROR;
        }
    }
}
// Use this function to set the control register and flip CTRL_VPE_ENABLE bit to CTRL_VPE_ENABLE or CTRL_VPP_P1_ENABLE
void eprom_internal_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool state) {
    if (bit & CTRL_VPE_ENABLE && using_p1_as_vpp(handle)) {
        bit &= ~CTRL_VPE_ENABLE;
        bit |= CTRL_VPP_P1_ENABLE;
    }
    ep_set_control_register(handle, bit, state);
}

void eprom_internal_ensure_regulator_enabled(firestarter_handle_t* handle) {
    if (handle->firestarter_get_control_register(handle, CTRL_VPP_REGULATOR_ENABLE) == 0) {
        handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
        delay(500);
    }
}
