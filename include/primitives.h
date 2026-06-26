/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __PRIMITIVES_H__
#define __PRIMITIVES_H__

#include "firestarter.h"
#ifdef __cplusplus
extern "C" {
#endif

    /* chip_id_report — shared chip-ID compare + MSG frame + FORCE downgrade (P4/PRIM-03).
     *
     * Callers: eprom.cpp (protocol 0x07/0x08/0x0B), eeprom_28c.cpp (0x0D),
     *          flash_intel.cpp (0x10), flash_utils.cpp (0x05/0x06 via flash4/flash3).
     *
     * Each caller performs its own protocol-specific chip-ID read mechanism
     * (A9-12V for EPROM/EEPROM28C; command-register 0x90 autoselect for flash_intel;
     * FLASH_ENABLE_ID/FLASH_DISABLE_ID AMD/JEDEC sequence for flash4/flash3), then
     * passes the result here. No regulator control lives in this primitive (D-06 —
     * chip keying is on handle->protocol in the caller; D-06 boundary is preserved).
     *
     * Behavior:
     *   - On match (read_id == handle->chip_id): no log frame, response_code unchanged.
     *   - On mismatch with FLAG_FORCE set: LOG_WARN_ID_BYTES(MSG_WARN_CHIP_ID_MISMATCH)
     *     + RESPONSE_CODE_WARNING.
     *   - On mismatch without FLAG_FORCE: LOG_ERROR_ID_BYTES(MSG_ERR_CHIP_ID_MISMATCH)
     *     + RESPONSE_CODE_ERROR.
     *   - _b[4] byte order: read_id hi, read_id lo, expected hi, expected lo (MSB-first).
     */
    void chip_id_report(firestarter_handle_t* handle, uint16_t read_id);

    /* vpp_check_window — shared VPP read + HIGH/LOW window-compare + _b[8] packing
     *                    + FORCE downgrade (P3/PRIM-04).
     *
     * Callers: eprom.cpp (protocols 0x07/0x08/0x0B via eprom_check_vpp),
     *          flash_intel.cpp (protocol 0x10 via flash_intel_check_vpp).
     *
     * Each caller keeps its own:
     *   - Regulator enable/disable (protocol-keyed, D-06): eprom_check_vpp enables
     *     CTRL_VPP_REGULATOR_ENABLE (+ CTRL_VPP_VPE_DROP_ENABLE for 0x07/0x08) before
     *     calling here, then clears after; flash_intel's caller (flash_intel_write_init)
     *     already holds CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE throughout.
     *   - REV0 guard: early return on REVISION_0 hardware (stays handler-local, Open Q2).
     *   - Trailing regulator clear: only eprom_check_vpp issues the clear; flash_intel
     *     holds VPP for the entire write cycle.
     *   - Settle delay: eprom_check_vpp issues delay(100) before calling here;
     *     flash_intel's caller already delayed 500ms (delay diverges — handler-local).
     *
     * This primitive owns ONLY:
     *   - rurp_read_voltage_mv() read
     *   - HIGH check: vpp_mv > (uint32_t)handle->vpp_mv + 500 (D-08 threshold UNCHANGED)
     *     → FORCE: LOG_WARN_ID_BYTES(MSG_WARN_VPP_HIGH) + RESPONSE_CODE_WARNING
     *     → else:  LOG_ERROR_ID_BYTES(MSG_ERR_VPP_HIGH) + RESPONSE_CODE_ERROR
     *   - LOW check: vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100
     *     → always: LOG_WARN_ID_BYTES(MSG_WARN_VPP_LOW) + RESPONSE_CODE_WARNING
     *   - _b[8] MSB-first packing (vpp_mv + 50 / 1000 hi, lo, /100%10 hi, lo; ×2 for set)
     *
     * No regulator control lives here (D-06 / SAFE-04 / T-89-01).
     */
    void vpp_check_window(firestarter_handle_t* handle);

#ifdef __cplusplus
}
#endif
#endif // __PRIMITIVES_H__
