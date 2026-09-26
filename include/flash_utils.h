/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __FLASH_UTILS_H__
#define __FLASH_UTILS_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "firestarter.h"

#define flash_execute_command(command) \
    flash_util_byte_flipping(handle, command, sizeof(command) / sizeof(command[0]));


    typedef struct byte_flip {
        uint32_t address;
        uint8_t byte;
    } byte_flip_t;

    const byte_flip_t FLASH_ENABLE_ID[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x90},
    };
    const byte_flip FLASH_DISABLE_ID[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0xF0},
    };
    const byte_flip_t FLASH_ERASE[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x80},
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x10},
    };
    const byte_flip_t FLASH_ENABLE_WRITE[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0xA0},
    };

    const byte_flip_t FLASH_ENABLE_WRITE_PROTECTION[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0xA0},
    };
    const byte_flip_t FLASH_DISABLE_WRITE_PROTECTION[] = {
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x80},
        {0x5555, 0xAA},
        {0x2AAA, 0x55},
        {0x5555, 0x20},
    };

    /* Protection-status read address & decode constants.
     *
     * Both sequences are datasheet-derived: infoic.xml's `config` field is the
     * literal string "NULL" on every 0x05 and 0x06 entry, so there is no
     * machine-readable upstream to diff against. The pinning legs in
     * test_val_nor_unlock.cpp / test_val_5v_page.cpp are change detectors over
     * these literals, not correctness proofs.
     *
     * Sequence A -- 0x06 AMD Autoselect Sector Group Protection Verify.
     * [AMD Am29F040B Rev. F, "Autoselect Mode", verify at (SA)+0x02, p.11;
     * corroborated by AM29F002B/NB and Macronix MX29F200C v2.1 p.14.] SA = 0x0000
     * -- this reports the lowest sector's state as the device's answer, not a
     * per-sector map. x8 mode. Mode entry/exit is byte-identical to
     * FLASH_ENABLE_ID / FLASH_DISABLE_ID above, so there is no separate table.
     *
     * Addressing: 0x0002 is under 64 KiB and is issued through
     * handle->firestarter_get_data, NEVER fu_flash_fast_address -- that path has no
     * A16+ bank register. */
    #define FLASH_NOR_UNLOCK_PROTECT_VERIFY_ADDR   0x0002UL
    #define FLASH_NOR_UNLOCK_PROTECT_UNPROTECTED   0x00
    #define FLASH_NOR_UNLOCK_PROTECT_PROTECTED     0x01

    /* Sequence B -- 0x05 Winbond Product-ID boot-block protection status.
     * [Winbond W29C020C, "Product Identification Entry/Exit" and the adjoining
     * boot-block protection-status description, p.9 -- print revision not
     * independently confirmed.]
     *
     * There is no Product-ID entry distinct from FLASH_ENABLE_ID:
     * flash_util_get_chip_id already drives that exact AA/55/90 sequence on this
     * family, confirmed on silicon. No separate table.
     *
     * The status-read address is sourced by structural analogy to that confirmed
     * manufacturer/device word pair rather than from an independently re-checked
     * page -- the lowest-confidence citation here, stated as such. Addressing path
     * as Sequence A. */
    #define FLASH_5V_PAGE_BOOT_BLOCK_STATUS_ADDR   0x0002UL
    #define FLASH_5V_PAGE_BOOT_BLOCK_UNLOCKED      0xFF
    #define FLASH_5V_PAGE_BOOT_BLOCK_LOCKED        0xFE

    void flash_util_byte_flipping(firestarter_handle_t* handle, const byte_flip_t* byte_flips, size_t size);
    void flash_util_verify_operation(firestarter_handle_t* handle, uint8_t expected_data);

    /* Shared AMD/JEDEC chip-ID read and mismatch check.
     * Used by flash_nor_unlock and flash_5v_page to avoid duplicating the
     * FLASH_ENABLE_ID / FLASH_DISABLE_ID command sequence.
     *
     * flash_util_read_in_id_mode is the same mode
     * entry/exit with a caller-supplied read address rather than the fixed
     * 0x0000/0x0001 chip-ID pair — a protect-verify read is this same
     * AMD/JEDEC ID mode, just reading a different word. It therefore lives
     * in this shared block rather than being duplicated in
     * flash_nor_unlock.cpp or flash_5v_page.cpp. */
    uint16_t flash_util_get_chip_id(firestarter_handle_t* handle);
    void flash_util_check_chip_id_execute(firestarter_handle_t* handle);
    uint8_t flash_util_read_in_id_mode(firestarter_handle_t* handle, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_UTILS_H__