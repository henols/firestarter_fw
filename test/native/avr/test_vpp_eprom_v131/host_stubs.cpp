/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * PITFALL 1, restated: the register-cache globals (lsb_address, msb_address,
 * control_register, declared in include/rurp_register_utils.h) are
 * non-static and 0xff-initialised. They persist across Unity test cases in
 * this single binary, and the 0xff CONTROL value ORs a VPP-regulator bit
 * (CTRL_VPP_REGULATOR_ENABLE, 0x80) into the FIRST address write of any case
 * that does not reset them. Every case must reset the cache deliberately
 * before driving anything (reset_register_cache, below).
 *
 * PITFALL 2, restated: every opt-in guard above MUST be defined BEFORE the
 * shared stub base is included below -- each is read at include time, and a
 * #define written after the include silently does nothing.
 *
 * Why HOST_STUBS_RECORD_BUS (the shared base's OTHER, older recorder) is
 * unusable for this suite, for two independent reasons (F-141-09): it
 * stores `(uint8_t)data`, which truncates the 0x100 wide-variant drop bit to
 * zero before this suite could ever observe it; and it records CALLS to
 * rurp_write_to_register rather than STROBES below the cache-compare
 * elision, so an elided (unchanged-value) write is invisible to it -- a
 * green assertion built on it could be evidence of nothing.
 *
 * Because HOST_STUBS_REAL_REGISTER_UTILS defines the wider suppression block
 * that removes all four hardware-revision stubs from the shared base, the
 * REAL rurp_get_hardware_revision() is linked in this suite, and it returns
 * rurp_get_config()->hardware_revision -- which is 0 (REVISION_0) by default
 * from the shared base's zero-initialised config struct. Every case that
 * cares about the drop bit's per-revision physical mapping, or about
 * reaching eprom_check_vpp's voltage compare at all (Rev 0 takes an early
 * return there), must override rurp_get_config()->hardware_revision per
 * case. This suite does NOT opt into any narrower revision-override guard --
 * there is nothing for one to do once the wider suppression block above is
 * active.
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
 * s_strobe_count, which only exists in that block (enforced by a #error in
 * the shared stub base if this is requested alone). */
#define HOST_STUBS_RECORD_TIMING
/* Opt OUT of the shared base's default rurp_read_data_buffer (always
 * returns 0), so this file can supply the stateful, 16-bit-keyed,
 * mismatch-window model below instead. MUST precede the include. */
#define HOST_STUBS_CUSTOM_READ_DATA_BUFFER
/* Opt OUT of the shared base's default rurp_read_voltage_mv (always returns
 * 0), so this file can inject an arbitrary VPP millivolt reading per case
 * (below). MUST precede the include. */
#define HOST_STUBS_CUSTOM_VOLTAGE_MV

#include "../_shared/host_stubs_common.inc"

/* Production's real cache-compare + latch-strobe sequencing + timing
 * (delayMicroseconds(1) after every non-elided latch, delayMicroseconds(4)
 * on a VPP P1-enable set->clear transition), instead of a hand-maintained
 * replica that could silently drift from rurp_write_to_register /
 * rurp_internal_write_to_register. MUST come AFTER the shared stub base --
 * that base suppresses the real declarations only inside its
 * HOST_STUBS_REAL_REGISTER_UTILS arm. */
#include "rurp_register_utils.h"

/* Pitfall 1 (restated above): lsb_address, msb_address and control_register
 * are non-static globals (rurp_register_utils.h:12-14) initialised to 0xff.
 * They persist across Unity test cases in this single binary, and the 0xff
 * CONTROL value ORs a VPP-regulator bit (CTRL_VPP_REGULATOR_ENABLE, 0x80)
 * into the FIRST address write of any case that does not reset them. Every
 * case must reset the cache deliberately before driving anything. */
extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl) {
    lsb_address = lsb;
    msb_address = msb;
    control_register = ctrl;
}

/* ─────────────────────────────────────────────────────────────────────────
 * 16-bit-latched-address-keyed read-back model, WITH a mismatch window.
 *
 * Derivation of the key (source-verified against src/proms/memory.cpp, not
 * assumed): for this suite's 32-pin VPP_BUS_CONFIG_0x08, matching_lines is
 * 17 and static_high_mask is 0. mem_util_remap_address_bus starts from
 * `config.address_mask & address` and only perturbs bits at index >=
 * matching_lines (here, bit 17 and up); bits 0-16 are therefore left
 * IDENTITY-mapped for every address this suite drives. Bit 16 (A16) is
 * carried by mem_util_calculate_top_address_register's CTRL_ADDRESS_LINE_16
 * bit in the CONTROL top-address register, NOT by the LSB/MSB latches -- so
 * the LSB/MSB pair alone carries exactly the low 16 bits, and
 *     rurp_read_from_register(LEAST_SIGNIFICANT_BYTE)
 *     | (rurp_read_from_register(MOST_SIGNIFICANT_BYTE) << 8)
 * equals `address & 0xFFFF` for every byte of a block, including a block
 * based anywhere other than address 0.
 *
 * Read-count-to-pulse-count mapping (unchanged from the sibling suite's
 * model): the write loop reads FIRST (the skip check) and only then pulses,
 * so seeding `converge_after = N` means the byte matches on read `N+1`, i.e.
 * after exactly N pulses -- vpp_readback_reads(addr) == 1 + pulses. A byte
 * that is skipped entirely under the 0xFF rule is never read at all, so its
 * read count stays 0.
 * ───────────────────────────────────────────────────────────────────────── */

struct vpp_readback_entry_t {
    uint16_t addr16;
    uint8_t target;
    uint16_t converge_after;
    uint16_t mismatch_from;
    uint16_t read_count;
    uint8_t seeded;
};

#define VPP_READBACK_MAX_ENTRIES 8
static vpp_readback_entry_t s_vpp_readback[VPP_READBACK_MAX_ENTRIES];

extern "C" void vpp_readback_reset(void) {
    for (int i = 0; i < VPP_READBACK_MAX_ENTRIES; i++) {
        s_vpp_readback[i].addr16 = 0;
        s_vpp_readback[i].target = 0xFF;
        s_vpp_readback[i].converge_after = 0;
        s_vpp_readback[i].mismatch_from = 0xFFFF;  /* never-mismatch sentinel */
        s_vpp_readback[i].read_count = 0;
        s_vpp_readback[i].seeded = 0;
    }
}

extern "C" void vpp_readback_seed(uint16_t addr16, uint8_t target, uint16_t converge_after, uint16_t mismatch_from) {
    int free_slot = -1;
    for (int i = 0; i < VPP_READBACK_MAX_ENTRIES; i++) {
        if (s_vpp_readback[i].seeded && s_vpp_readback[i].addr16 == addr16) {
            /* Re-seeding the same address within one case: reset its read
             * counter along with the new target/converge_after/mismatch_from. */
            s_vpp_readback[i].target = target;
            s_vpp_readback[i].converge_after = converge_after;
            s_vpp_readback[i].mismatch_from = mismatch_from;
            s_vpp_readback[i].read_count = 0;
            return;
        }
        if (free_slot < 0 && !s_vpp_readback[i].seeded) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        s_vpp_readback[free_slot].addr16 = addr16;
        s_vpp_readback[free_slot].target = target;
        s_vpp_readback[free_slot].converge_after = converge_after;
        s_vpp_readback[free_slot].mismatch_from = mismatch_from;
        s_vpp_readback[free_slot].read_count = 0;
        s_vpp_readback[free_slot].seeded = 1;
    }
    /* VPP_READBACK_MAX_ENTRIES (8) comfortably exceeds every block this
     * suite drives; a ninth seed past that cap, with the table already full
     * of distinct addresses, is not reachable by any case authored here. */
}

extern "C" int vpp_readback_reads(uint16_t addr16) {
    for (int i = 0; i < VPP_READBACK_MAX_ENTRIES; i++) {
        if (s_vpp_readback[i].seeded && s_vpp_readback[i].addr16 == addr16) {
            return (int)s_vpp_readback[i].read_count;
        }
    }
    return -1;  /* never seeded -- the negative-control return value */
}

extern "C" int vpp_readback_seeded_count(void) {
    int n = 0;
    for (int i = 0; i < VPP_READBACK_MAX_ENTRIES; i++) {
        if (s_vpp_readback[i].seeded) {
            n++;
        }
    }
    return n;
}

extern "C" uint8_t rurp_read_data_buffer(void) {
    uint16_t key = (uint16_t)((uint16_t)rurp_read_from_register(LEAST_SIGNIFICANT_BYTE)
                 | (uint16_t)((uint16_t)rurp_read_from_register(MOST_SIGNIFICANT_BYTE) << 8));
    for (int i = 0; i < VPP_READBACK_MAX_ENTRIES; i++) {
        if (s_vpp_readback[i].seeded && s_vpp_readback[i].addr16 == key) {
            vpp_readback_entry_t* e = &s_vpp_readback[i];
            uint8_t result;
            if (e->read_count < e->converge_after) {
                result = 0xFF;
            } else if (e->read_count >= e->mismatch_from) {
                result = (uint8_t)(~e->target);
            } else {
                result = e->target;
            }
            e->read_count++;
            return result;
        }
    }
    /* Unseeded address: return 0xFF and do NOT silently create an entry --
     * the negative-control property vpp_readback_seeded_count() proves. */
    return 0xFF;
}

/* Suite-local mockable VPP voltage -- TU-private state; the test TU calls
 * set_mock_vpp_mv() to inject a value before driving into eprom_check_vpp
 * (via drive_vpp_init) or into the write path. Exact signature per
 * include/rurp_shield.h:145 -- no parameters. */
static uint16_t s_mock_vpp_mv = 0;
extern "C" void set_mock_vpp_mv(uint16_t mv) { s_mock_vpp_mv = mv; }
extern "C" uint16_t rurp_read_voltage_mv() { return s_mock_vpp_mv; }

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
 * assume it holds only its own frame. This is what makes "assert
 * MSG_ERR_VPP_HIGH by id" possible -- nothing else in the tree does it.
 * ───────────────────────────────────────────────────────────────────────── */

#define VPP_LOGGED_ID_MAX_ENTRIES 32
#define VPP_LOGGED_ID_MAX_PARAMS 8

struct vpp_logged_id_entry_t {
    uint8_t id;
    uint8_t param_count;
    uint8_t params[VPP_LOGGED_ID_MAX_PARAMS];
};

static vpp_logged_id_entry_t s_logged_ids[VPP_LOGGED_ID_MAX_ENTRIES];
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
    if (s_logged_id_count >= VPP_LOGGED_ID_MAX_ENTRIES) {
        s_logged_id_overflow = 1;  /* tail dropped; prefix stays valid */
        return;
    }
    vpp_logged_id_entry_t* e = &s_logged_ids[s_logged_id_count];
    uint8_t n = (param_count > VPP_LOGGED_ID_MAX_PARAMS) ? (uint8_t)VPP_LOGGED_ID_MAX_PARAMS : param_count;
    e->id = id;
    e->param_count = n;
    for (uint8_t j = 0; j < n; j++) {
        e->params[j] = params[j];
    }
    s_logged_id_count++;
}
