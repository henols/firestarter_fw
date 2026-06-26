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
 *   - eeprom_28c.cpp   (protocol 0x0D — A9-12V read with mfr_addr = mem_size-64)
 *   - flash_intel.cpp  (protocol 0x10 — command-register 0x90 autoselect read)
 *   - flash_utils.cpp  (protocols 0x05/0x06 — AMD/JEDEC FLASH_ENABLE_ID sequence)
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

void chip_id_report(firestarter_handle_t* handle, uint16_t read_id) {
    if (read_id != handle->chip_id) {
        uint8_t _b[4];
        _b[0] = (uint8_t)((read_id >> 8) & 0xFF);
        _b[1] = (uint8_t)(read_id & 0xFF);
        _b[2] = (uint8_t)((handle->chip_id >> 8) & 0xFF);
        _b[3] = (uint8_t)(handle->chip_id & 0xFF);
        if (is_flag_set(FLAG_FORCE)) {
            LOG_WARN_ID_BYTES(MSG_WARN_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_WARNING;
        } else {
            LOG_ERROR_ID_BYTES(MSG_ERR_CHIP_ID_MISMATCH, _b, 4);
            handle->response_code = RESPONSE_CODE_ERROR;
        }
    }
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
