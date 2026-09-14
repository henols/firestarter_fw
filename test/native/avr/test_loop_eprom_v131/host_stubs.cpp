/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Do NOT also define the narrower hardware-revision override guard that
 * test_val_eprom/host_stubs.cpp uses (see that file's own header comment):
 * HOST_STUBS_REAL_REGISTER_UTILS already defines the wider
 * HOST_STUBS_CUSTOM_HW_REVISION_BLOCK, so the four hardware-revision stubs
 * come from the REAL rurp_hw_rev_utils.h (pulled in transitively by
 * rurp_register_utils.h below). This suite never asserts on
 * hardware-revision behaviour, so it needs no override of that kind at all.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "rurp_shield.h"
#include "rurp_types.h"
}

/* Activate the ordered strobe recorder (opt-IN). MUST precede the include. */
#define HOST_STUBS_REAL_REGISTER_UTILS
/* Activate the timing recorder (opt-IN). MUST precede the include, and
 * requires HOST_STUBS_REAL_REGISTER_UTILS above -- its sequence key is
 * s_strobe_count, which only exists in that block (enforced by an #error in
 * the shared .inc if this is requested alone). */
#define HOST_STUBS_RECORD_TIMING
/* Opt OUT of the shared .inc's default rurp_read_data_buffer (always
 * returns 0), so this file can supply the stateful, 16-bit-keyed model
 * below instead. MUST precede the include. */
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER

#include "../_shared/host_stubs_common.inc"

#include "rurp_register_utils.h"

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl) {
    lsb_address = lsb;
    msb_address = msb;
    control_register = ctrl;
}

/* ─────────────────────────────────────────────────────────────────────────
 * 16-bit-latched-address-keyed read-back model.
 * ───────────────────────────────────────────────────────────────────────── */

struct loop_readback_entry_t {
    uint16_t addr16;
    uint8_t target;
    uint16_t converge_after;
    uint16_t read_count;
    uint8_t seeded;
};

#define LOOP_READBACK_MAX_ENTRIES 8
static loop_readback_entry_t s_loop_readback[LOOP_READBACK_MAX_ENTRIES];

extern "C" void loop_readback_reset(void) {
    for (int i = 0; i < LOOP_READBACK_MAX_ENTRIES; i++) {
        s_loop_readback[i].addr16 = 0;
        s_loop_readback[i].target = 0xFF;
        s_loop_readback[i].converge_after = 0;
        s_loop_readback[i].read_count = 0;
        s_loop_readback[i].seeded = 0;
    }
}

extern "C" void loop_readback_seed(uint16_t addr16, uint8_t target, uint16_t converge_after) {
    int free_slot = -1;
    for (int i = 0; i < LOOP_READBACK_MAX_ENTRIES; i++) {
        if (s_loop_readback[i].seeded && s_loop_readback[i].addr16 == addr16) {
            /* Re-seeding the same address within one case: reset its read
             * counter along with the new target/converge_after. */
            s_loop_readback[i].target = target;
            s_loop_readback[i].converge_after = converge_after;
            s_loop_readback[i].read_count = 0;
            return;
        }
        if (free_slot < 0 && !s_loop_readback[i].seeded) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        s_loop_readback[free_slot].addr16 = addr16;
        s_loop_readback[free_slot].target = target;
        s_loop_readback[free_slot].converge_after = converge_after;
        s_loop_readback[free_slot].read_count = 0;
        s_loop_readback[free_slot].seeded = 1;
    }
    /* LOOP_READBACK_MAX_ENTRIES (8) comfortably exceeds every block this
     * suite or its plan-141-07/141-08 successors drive; a ninth seed past
     * that cap, with the table already full of distinct addresses, is not
     * reachable by any case authored in this tree. */
}

extern "C" int loop_readback_reads(uint16_t addr16) {
    for (int i = 0; i < LOOP_READBACK_MAX_ENTRIES; i++) {
        if (s_loop_readback[i].seeded && s_loop_readback[i].addr16 == addr16) {
            return (int)s_loop_readback[i].read_count;
        }
    }
    return -1;  /* never seeded -- the negative-control return value */
}

extern "C" int loop_readback_seeded_count(void) {
    int n = 0;
    for (int i = 0; i < LOOP_READBACK_MAX_ENTRIES; i++) {
        if (s_loop_readback[i].seeded) {
            n++;
        }
    }
    return n;
}

extern "C" uint8_t rurp_read_data_buffer(void) {
    uint16_t key = (uint16_t)((uint16_t)rurp_read_from_register(LEAST_SIGNIFICANT_BYTE)
                 | (uint16_t)((uint16_t)rurp_read_from_register(MOST_SIGNIFICANT_BYTE) << 8));
    for (int i = 0; i < LOOP_READBACK_MAX_ENTRIES; i++) {
        if (s_loop_readback[i].seeded && s_loop_readback[i].addr16 == key) {
            loop_readback_entry_t* e = &s_loop_readback[i];
            uint8_t result = (e->read_count < e->converge_after) ? 0xFF : e->target;
            e->read_count++;
            return result;
        }
    }
    /* Unseeded address: return 0xFF and do NOT silently create an entry --
     * the negative-control property loop_readback_seeded_count() proves. */
    return 0xFF;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Logged-id capture.
 *
 * rurp_log_id (declared extern "C" inside rurp_shield.h's own extern "C"
 * block, weak-defined in src/boards/rurp_serial_utils.cpp:486) is the single
 * routing point every rurp_log_id_u8/_u16/_u24/_u32 packer (same file,
 * :495-525) calls through. A strong definition here overrides that weak
 * default and therefore captures every logged frame from any production
 * code this suite's build_src_filter compiles -- INCLUDING
 * LOG_DEBUG_ID_SUB's MSG_DEBUG entries (include/logging_id.h), so a case
 * that only cares about its own id must filter this array by id rather than
 * assume it holds only its own frame.
 * ───────────────────────────────────────────────────────────────────────── */

#define LOOP_LOGGED_ID_MAX_ENTRIES 32
#define LOOP_LOGGED_ID_MAX_PARAMS 8

struct loop_logged_id_entry_t {
    uint8_t id;
    uint8_t param_count;
    uint8_t params[LOOP_LOGGED_ID_MAX_PARAMS];
};

static loop_logged_id_entry_t s_logged_ids[LOOP_LOGGED_ID_MAX_ENTRIES];
static int s_logged_id_count = 0;
static int s_logged_id_overflow = 0;

extern "C" void clear_logged_ids(void) {
    s_logged_id_count = 0;
    s_logged_id_overflow = 0;
}

extern "C" int logged_id_count(void) {
    return s_logged_id_count;
}

extern "C" uint8_t logged_id_at(int i) {
    return s_logged_ids[i].id;
}

extern "C" uint8_t logged_id_param_count(int i) {
    return s_logged_ids[i].param_count;
}

extern "C" uint8_t logged_id_param(int i, int j) {
    return s_logged_ids[i].params[j];
}

extern "C" int logged_ids_overflowed(void) {
    return s_logged_id_overflow;
}

extern "C" void rurp_log_id(uint8_t id, const uint8_t* params, uint8_t param_count) {
    if (s_logged_id_count >= LOOP_LOGGED_ID_MAX_ENTRIES) {
        s_logged_id_overflow = 1;  /* tail dropped; prefix stays valid */
        return;
    }
    loop_logged_id_entry_t* e = &s_logged_ids[s_logged_id_count];
    uint8_t n = (param_count > LOOP_LOGGED_ID_MAX_PARAMS) ? (uint8_t)LOOP_LOGGED_ID_MAX_PARAMS : param_count;
    e->id = id;
    e->param_count = n;
    for (uint8_t j = 0; j < n; j++) {
        e->params[j] = params[j];
    }
    s_logged_id_count++;
}
