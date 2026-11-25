/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __READY_CHUNK_H__
#define __READY_CHUNK_H__

#include <stdint.h>

#include "proto_constants.h"

/*
 * Chunk size advertised in the MSG_OK_READY ack (buffer_size field).
 *
 * The host reads this field from every ack and sends data chunks of exactly
 * that size, so the firmware can choose a size per command. Page families
 * get the buffer rounded down to whole pages, so a chunk never splits a page:
 *
 *   0x05, 0x35, 0x39  flash_5v_page_write_execute refuses a block that is not
 *                     whole pages (MSG_ERR_FL4_PAGE_ALIGN). The validity rule
 *                     is the same as flash_5v_page_mask(). An invalid page
 *                     size gives the full buffer: the handler refuses it with
 *                     MSG_ERR_FL4_PAGE_SIZE before it writes.
 *   0x0D              A split page costs one more t_WC write cycle. The page
 *                     size and its 64 B fallback are the same as
 *                     eeprom28c_page_mask().
 *
 * Every other protocol writes byte by byte and gets the full buffer.
 *
 * buffer must be >= READY_CHUNK_PAGE_MAX, so the result is never 0.
 */
#define READY_CHUNK_PAGE_MAX 512
#define READY_CHUNK_EEPROM28C_FALLBACK 64

static inline uint8_t ready_chunk_page_valid(uint16_t page_size) {
    return page_size != 0 && page_size <= READY_CHUNK_PAGE_MAX &&
           (page_size & (page_size - 1)) == 0;
}

static inline uint16_t fs_ready_chunk_size(uint16_t buffer, uint8_t protocol, uint16_t page_size) {
    uint16_t page;
    switch (protocol) {
        case PROTO_FLASH_5V_PAGE:
        case PROTO_PHANTOM_0x35:
        case PROTO_PHANTOM_0x39:
            if (!ready_chunk_page_valid(page_size)) {
                return buffer;
            }
            page = page_size;
            break;
        case PROTO_EEPROM_PARALLEL:
            page = ready_chunk_page_valid(page_size) ? page_size : READY_CHUNK_EEPROM28C_FALLBACK;
            break;
        default:
            return buffer;
    }
    return (uint16_t)(buffer & (uint16_t)~(uint16_t)(page - 1));
}

#endif  // __READY_CHUNK_H__
