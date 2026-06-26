/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Cross-family firmware primitives — shared code called from multiple protocol handlers.
 *
 * Callers by primitive:
 *
 * chip_id_report (P4/PRIM-03):
 *   - eprom.cpp        (protocols 0x07/0x08/0x0B — A9-12V read, EPROM/EEPROM family)
 *     CHECK_CHIP_ID path: force_warning=false (ERROR unconditional, CR-01)
 *     generic-init path: force_warning=is_flag_set(FLAG_FORCE)
 *   - eeprom_28c.cpp   (protocol 0x0D — A9-12V read with mfr_addr = mem_size-64)
 *     force_warning=is_flag_set(FLAG_FORCE)
 *   - flash_intel.cpp  (protocol 0x10 — command-register 0x90 autoselect read)
 *     force_warning=is_flag_set(FLAG_FORCE)
 *   - flash_utils.cpp  (protocols 0x05/0x06 — AMD/JEDEC FLASH_ENABLE_ID sequence)
 *     force_warning=is_flag_set(FLAG_FORCE)
 *
 * poll_readback (P5/PRIM-05):
 *   - eeprom_28c.cpp   (protocol 0x0D — eeprom28c_wait_for_write, cap=2000)
 *   - flash_type_4.cpp (protocol 0x05 — flash4_wait_for_page_write, cap=1024)
 *   Each caller emits its own error frame (different MSG id + _b[] byte order per site).
 *   eprom.cpp verify_and_update_mask is a different algorithm (whole-buffer bitmask,
 *   returns count, no timeout frame) — NOT routed through poll_readback.
 *
 * vpp_check_window (P3/PRIM-04):
 *   - eprom.cpp        (protocols 0x07/0x08/0x0B — via eprom_check_vpp)
 *   - flash_intel.cpp  (protocol 0x10 — via flash_intel_check_vpp)
 *
 * Design boundary (D-06): No regulator routing control lives here. Each caller keeps
 * its protocol-specific regulator enable/disable (and settle delay) before/after
 * calling the shared primitive. The regulator routing is keyed on handle->protocol
 * in the caller, never on electrical.type (D-06).
 *
 * The eeprom28c mem_size < 64 underflow guard (eeprom_28c.cpp) stays handler-local
 * (SAFE-04/V5 input-validation — guards hardware-unsafe operation, not a report detail).
 */

#include "primitives.h"
#include "firestarter.h"
#include "logging_id.h"
#include "rurp_shield.h"

#include <Arduino.h>

void chip_id_report(firestarter_handle_t* handle, uint16_t read_id, bool force_warning) {
    if (read_id != handle->chip_id) {
        uint8_t _b[4];
        _b[0] = (uint8_t)((read_id >> 8) & 0xFF);
        _b[1] = (uint8_t)(read_id & 0xFF);
        _b[2] = (uint8_t)((handle->chip_id >> 8) & 0xFF);
        _b[3] = (uint8_t)(handle->chip_id & 0xFF);
        /* CR-01: force_warning is supplied by the caller — FLAG_FORCE is NOT
         * read here. eprom_check_chip_id_execute passes false (unconditional ERROR
         * even with --force); all other callers pass is_flag_set(FLAG_FORCE). */
        if (force_warning) {
            LOG_WARN_ID_BYTES(MSG_WARN_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_WARNING;
        } else {
            LOG_ERROR_ID_BYTES(MSG_ERR_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_ERROR;
        }
    }
}

bool poll_readback(firestarter_handle_t* handle, uint32_t address, uint8_t expected,
                   uint16_t max_iters, uint8_t* observed_out) {
    /* Bounded single-address poll kernel (P5/PRIM-05).
     *
     * Per-iteration: delayMicroseconds(10) then read address. Returns true on match.
     * On timeout: writes last observed byte into *observed_out and returns false.
     * The caller emits its site-specific error frame (MSG id + _b[] byte order diverge
     * between eeprom28c and flash4 — MUST NOT be normalised here).
     */
    uint8_t observed = 0;
    for (uint16_t j = 0; j < max_iters; j++) {
        delayMicroseconds(10);
        observed = handle->firestarter_get_data(handle, address);
        if (observed == expected) {
            return true;
        }
    }
    if (observed_out) {
        *observed_out = observed;
    }
    return false;
}

void vpp_check_window(firestarter_handle_t* handle) {
    /* Shared VPP window-compare body (P3/PRIM-04).
     *
     * Caller is responsible for: regulator enable (protocol-keyed, D-06), settle delay
     * (eprom_check_vpp issues delay(100); flash_intel_write_init already delayed 500ms),
     * REV0 guard (handler-local, Open Q2), and trailing regulator clear (eprom_check_vpp
     * only; flash_intel holds VPP for the whole write cycle).
     *
     * This body: read voltage, HIGH check (D-08 threshold +500 mV, UNCHANGED), LOW check
     * (95% floor), _b[8] MSB-first pack, FORCE/ERROR frames. No regulator writes here.
     */
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
}
