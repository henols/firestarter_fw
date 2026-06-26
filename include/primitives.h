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

    /* chip_id_report — shared chip-ID compare + MSG frame + caller-keyed severity
     *                   (P4/PRIM-03).
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
     * Parameters:
     *   handle        — firestarter handle (provides chip_id, response_code).
     *   read_id       — chip ID read from hardware.
     *   force_warning — when true, a mismatch is downgraded to WARNING; when false,
     *                   a mismatch is always ERROR. The caller owns this decision
     *                   (CR-01: FLAG_FORCE must NOT be read inside this primitive).
     *
     * Behavior:
     *   - On match (read_id == handle->chip_id): no log frame, response_code unchanged.
     *   - On mismatch with force_warning=true:  LOG_WARN_ID_BYTES(MSG_WARN_CHIP_ID_MISMATCH)
     *     + RESPONSE_CODE_WARNING.
     *   - On mismatch with force_warning=false: LOG_ERROR_ID_BYTES(MSG_ERR_CHIP_ID_MISMATCH)
     *     + RESPONSE_CODE_ERROR (unconditional, regardless of FLAG_FORCE in caller).
     *   - _b[4] byte order: read_id hi, read_id lo, expected hi, expected lo (MSB-first).
     *
     * CR-01 rationale: eprom_check_chip_id_execute (the CHECK_CHIP_ID command, reached
     * via `firestarter id --force`) must always report ERROR on mismatch — even with
     * FLAG_FORCE set. Callers that want FORCE→WARNING (flash, generic-init path) pass
     * is_flag_set(FLAG_FORCE); the eprom CHECK_CHIP_ID caller passes false explicitly.
     */
    void chip_id_report(firestarter_handle_t* handle, uint16_t read_id, bool force_warning);

    /* poll_readback — bounded single-address poll kernel (P5/PRIM-05).
     *
     * Shared by: eeprom_28c.cpp (eeprom28c_wait_for_write, cap=2000),
     *            flash_type_4.cpp (flash4_wait_for_page_write, cap=1024).
     *
     * NOT used by eprom.cpp verify_and_update_mask — that is a whole-buffer
     * bitmask loop returning a count with no timeout frame (different algorithm).
     *
     * Each caller keeps its own error-frame emission on false return, because
     * the two sites use different MSG ids AND different _b[] byte order:
     *   eeprom28c: MSG_ERR_EEPROM_TIMEOUT, _b[5] = {addr>>16, addr>>8, addr, expected, observed}
     *   flash4:    MSG_ERR_FL4_VERIFY_TIMEOUT, _b[5] = {expected, addr>>16, addr>>8, addr, observed}
     * The byte-order divergence is intentional (per site) and MUST NOT be normalised.
     *
     * Parameters:
     *   handle       — firestarter handle (provides firestarter_get_data).
     *   address      — single address to poll.
     *   expected     — byte value that ends the poll.
     *   max_iters    — iteration cap (2000 for eeprom28c, 1024 for flash4).
     *   observed_out — written with the last observed byte on timeout (caller uses
     *                  this in its error frame); ignored on true return.
     *
     * Returns: true if observed == expected within max_iters; false on timeout.
     *
     * Per-iteration behaviour: delayMicroseconds(10) then firestarter_get_data(handle, address).
     */
    bool poll_readback(firestarter_handle_t* handle, uint32_t address, uint8_t expected,
                       uint16_t max_iters, uint8_t* observed_out);

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
