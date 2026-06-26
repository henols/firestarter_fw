/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Fail-closed handler: configure_not_implemented() — covers phantom, infeasible,
 * and PCB-blocked protocol buckets.
 *
 * Returns MSG_ERR_PROTOCOL_NOT_IMPLEMENTED (0xBB) with RESPONSE_CODE_ERROR and
 * zero hardware side effects (all handler function pointers set to NULL).
 * See firestarter/doc/PROTOCOLS.md §2 "Honest non-protocols" for the rationale.
 *
 * Phantom buckets — dispatched-but-dead (zero DB chips):
 *   0x35 (FLASH_EEPROM)  — IC2_ALG_ITE is an ITE EC microcontroller label, NOT a memory
 *                          algorithm; zero chips in chip_database.json. Dispatch preserved
 *                          in memory.cpp for forward-compat; host excludes from KNOWN_PROTOCOLS.
 *   0x39 (FLASH_EEPROM2) — No IC2_ALG constant exists for this value in minipro source;
 *                          zero chips in chip_database.json. Same forward-compat policy.
 *
 * Infeasible buckets — structurally impossible on the RURP parallel bus (DISP-04, Phase 64):
 *   0x11 — FWH/LPC serial flash: requires dedicated FWH serial signaling; incompatible.
 *   0x2A — GAL16V8 PLD: requires high-voltage serial JEDEC programming.
 *   0x2B — GAL20V8 PLD (likely): same rationale as 0x2A.
 *   0x2C — GAL22V10 PLD / PIC MCU: requires ICSP serial protocol; incompatible.
 *
 * PCB-blocked bucket — feasible in principle, blocked by routing constraint (FUT-01):
 *   0x34 — X88C64 XICOR 8051-bus EEPROM (1 DB chip, X88C64P): the ALE/WR/RD bus requires a
 *          free 74HC573 strobe on the RURP control register; none is available on current
 *          Rev 2.x hardware (Phase 78 investigation verdict: PCB-BLOCKED HIGH). No handler
 *          will be committed until the PCB constraint is resolved. electrical.type is correctly
 *          EEPROM (Phase-86 NAME-04 decode correction; see doc/PROTOCOLS.md §1.12).
 *          Citation: datasheets/0x34-EEPROM-X88C64/X88C64.pdf §X88C64 Programming.
 *
 * Generic fail-closed (memory.cpp step 6b): any non-zero unrecognized protocol also
 * reaches this handler, eliminating the 12V VPP-hazard mem_type fallback (T-64-01, DISP-01).
 */

#include "not_implemented.h"
#include "firestarter.h"
#include "logging_id.h"
#include "messages.h"

void configure_not_implemented(firestarter_handle_t* handle) {
    handle->firestarter_operation_init = NULL;
    handle->firestarter_operation_main = NULL;
    handle->firestarter_operation_end = NULL;
    LOG_ERROR_ID_U8(MSG_ERR_PROTOCOL_NOT_IMPLEMENTED, (uint8_t)handle->protocol);
    handle->response_code = RESPONSE_CODE_ERROR;
}
