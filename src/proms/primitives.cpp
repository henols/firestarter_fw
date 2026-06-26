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
 * Design boundary (D-06): No regulator routing control lives here. Each caller keeps
 * its protocol-specific chip-ID read mechanism (A9-12V vs command-register vs AMD unlock)
 * and any associated regulator enable/disable, then calls chip_id_report() with the
 * result. This file contains ONLY the compare + MSG frame + FORCE downgrade tail.
 *
 * The eeprom28c mem_size < 64 underflow guard (eeprom_28c.cpp) stays handler-local
 * (SAFE-04/V5 input-validation — guards hardware-unsafe operation, not a report detail).
 */

#include "primitives.h"
#include "firestarter.h"
#include "logging_id.h"

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
