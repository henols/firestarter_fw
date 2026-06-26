/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/* Handler: configure_sram() — protocols 0x0E (SRAM-32PIN), 0x27 (SRAM-24PIN),
 *                              0x28 (SRAM-STD), 0x29 (SRAM-512K-1M)
 *
 * Standard SRAM read/write: assert address, assert CE+WE for write (or CE+OE for read),
 * present/capture data, de-assert. No programming protocol; no VPP regulator involvement.
 * This handler is the BLOCKER-2 mitigation: SRAM chips dispatched here can NEVER reach
 * configure_eprom() which would enable the 12V VPP boost regulator on a 5V-only part.
 *
 * Protocol family membership:
 *   0x0E — 32-pin battery-backed NVRAM (DS1245Y, M48T128Y series); optional 12V
 *           write-protect bypass via CTRL_VPP_P1_ENABLE (not the program regulator).
 *   0x27 — 24-pin async SRAM (6116 / 2K×8); ADDRESS_LINE_13 hardwired high (24-pin socket).
 *   0x28 — 28-pin SRAM / FRAM (62256, 6264, W24256, FM1608 siblings); standard JEDEC pinout.
 *   0x29 — 32-pin large battery-backed NVRAM (DS1245AB(TEST), DS1250AB(TEST) series).
 *   Citation: datasheets/0x28-SRAM-STD/FM1608.pdf p.4 §Write Timing.
 *
 * INV-07 — FM1608 routes to configure_sram() as SRAM_STD/FRAM (algorithm=0x28):
 *   The FM1608 (Ramtron ferroelectric RAM) raw infoic.xml tuple is type=4/proto=0x07/
 *   variant=0x4126. The Phase-86 variant decode maps this to algorithm=0x28 (SRAM_STD)
 *   because variant high byte 0x41 is the Ramtron FRAM class discriminator.
 *   The FM1608 therefore dispatches to configure_sram() — never to configure_eprom().
 *   This is correct: FRAM uses standard SRAM bus cycles; enabling VPP would destroy it.
 *   The historical "FM1608 algorithm 40" framing was a decimal-40 ↔ hex-0x28 conflation
 *   (decimal 40 IS hex 0x28); the ground truth is proto=0x07+variant=0x4126→0x28 (FRAM).
 *   Full prose: firestarter/doc/PROTOCOLS.md §1.10 (SRAM-STD NAME-04 call-out) and §3 INV-07 row.
 */

#include "sram.h"
#include "firestarter.h"
#include "rurp_shield.h"
#include <stdio.h>
#include "logging_id.h"
#include "messages.h"

void configure_sram(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_SRAM);
}
