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

#ifdef __cplusplus
}
#endif
#endif // __PRIMITIVES_H__
