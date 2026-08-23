/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "eprom.h"

#include <Arduino.h>

#include "firestarter.h"
#include "eprom_params.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "rurp_shield.h"
#include "rurp_pinout.h"
#include "operation_utils.h"


void eprom_erase_execute(firestarter_handle_t* handle);

void eprom_write_init(firestarter_handle_t* handle);
void eprom_write_execute(firestarter_handle_t* handle);
void eprom_check_chip_id_init(firestarter_handle_t* handle);
void eprom_check_chip_id_execute(firestarter_handle_t* handle);

uint16_t eprom_get_chip_id(firestarter_handle_t* handle);

void eprom_check_vpp(firestarter_handle_t* handle);

void eprom_internal_check_chip_id(firestarter_handle_t* handle, uint8_t error_code);
void eprom_internal_erase(firestarter_handle_t* handle);

void eprom_internal_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool cmd);
void (*ep_set_control_register)(struct firestarter_handle*, rurp_register_t, bool);

void eprom_generic_init(firestarter_handle_t* handle);

void configure_eprom(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_EPROM);

    handle->firestarter_operation_init = eprom_generic_init;

    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = eprom_write_init;
            handle->firestarter_operation_main = eprom_write_execute;
            break;
        case CMD_ERASE:
            handle->firestarter_operation_main = eprom_erase_execute;
            if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
                handle->firestarter_operation_end = mem_util_blank_check;
            }
            break;
        case CMD_BLANK_CHECK:
            handle->firestarter_operation_main = mem_util_blank_check;
            break;
        case CMD_CHECK_CHIP_ID:
            handle->firestarter_operation_init = eprom_check_chip_id_init;
            handle->firestarter_operation_main = eprom_check_chip_id_execute;
            break;
    }

    ep_set_control_register = handle->firestarter_set_control_register;
    handle->firestarter_set_control_register = eprom_internal_set_control_register;

    // Set default pulse_delay from protocol when Python doesn't supply one
    if (handle->pulse_delay == 0) {
        switch (handle->protocol) {
            case 0x08: handle->pulse_delay = 100;  break;  // EPROM_QUICK: 100µs
            case 0x0B: handle->pulse_delay = 500;  break;  // EPROM_LEGACY: 500µs
            default:   handle->pulse_delay = 1000; break;  // EPROM_STD: 1ms
        }
    }

    // Phase 141 Plan 04 -- both refusals below run AFTER the fallback
    // switch above resolves pulse_delay: a pulse_delay of 0 would otherwise
    // compare as "not greater than the cap" vacuously, letting an
    // unresolved default slip past the D-03 check.

    // Refusal 1 (Phase 140 D-05): an unrecognised protocol fails closed
    // here rather than falling back to &EPROM_PARAMS[0], which would route
    // 13V through the drop resistor for an unknown part.
    const eprom_params_t* row = eprom_params_for(handle->protocol);
    if (row == NULL) {
        LOG_ERROR_ID_U8(MSG_ERR_PROTOCOL_NOT_IMPLEMENTED, (uint8_t)handle->protocol);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }

    // Refusal 2 (D-03): a pulse wider than this row's per-byte
    // program-energy budget is refused pre-flight, before any high
    // voltage is enabled, and is never silently clamped -- clamping would
    // emit a pulse whose width no longer matches what the caller asked for
    // or what the trace claims. Without this, D-01's emit-then-stop rule
    // would apply a single up-to-cap VPE pulse to real silicon and then
    // report a *verify* failure -- a misconfiguration wearing a
    // silicon-failure costume. This is also the firmware-side backstop for
    // Phase 143's --pulse-us bounds, independent of host validation.
    //
    // energy_cap_us > 0 is mandatory: eprom_params.h defines 0 as
    // UNCAPPED, not "cap at zero" -- an unguarded compare would refuse
    // every 0x07/0x08 pulse (both ship energy_cap_us == 0).
    uint32_t energy_cap_us = pgm_read_dword(&row->energy_cap_us);
    if (energy_cap_us > 0 && handle->pulse_delay > energy_cap_us) {
        LOG_ERROR_ID_U32(MSG_ERR_PULSE_TOO_WIDE, handle->pulse_delay);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }
}

void eprom_check_chip_id_init(firestarter_handle_t* handle) {
    eprom_check_vpp(handle);
}

void eprom_check_chip_id_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID);
    eprom_internal_check_chip_id(handle, RESPONSE_CODE_ERROR);
}

void eprom_erase_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_ERASE);
    eprom_internal_erase(handle);
}

/*
 * Phase 142 Plan 04 (D-10 as amended, D-12) -- DEFENSIVE, not corrective.
 * Neither of this body's two exits leaks a route today (142-RESEARCH.md's
 * exit table, E1/E2): the early return below is taken only after
 * eprom_generic_init already disabled via eprom_check_vpp's or
 * eprom_get_chip_id's own clear, and the fall-through touches no HV bit
 * that eprom_internal_erase (called a few lines below, when reached) does
 * not already clear itself. This body is renamed static and wrapped
 * anyway, because eprom_internal_erase's own assert is the ONLY write-path
 * code that asserts CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE (remapped to
 * A9 | CTRL_VPP_P1_ENABLE on a using_p1_as_vpp handle), and that assert's
 * safety rests on there being no `return` between it and its own clear --
 * a property a future edit to this body could otherwise break silently.
 * See eprom_write_init below for the public entry point and the disable
 * guarantee itself.
 */
static void eprom_internal_write_init_body(firestarter_handle_t* handle) {
    if(!is_operation_in_progress(handle)){
        eprom_generic_init(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }

        if (is_flag_set(FLAG_CAN_ERASE)) {
            if (!is_flag_set(FLAG_SKIP_ERASE)) {
                eprom_internal_erase(handle);
            } else {
                LOG_INFO_ID(MSG_INFO_SKIPPING_ERASE);
            }
        }
    }
    if (!is_flag_set(FLAG_SKIP_BLANK_CHECK)) {
        mem_util_blank_check(handle);
    }
}

/*
 * Phase 142 Plan 04 (D-10 as amended, C-1, D-12) -- the single-exit
 * wrapper. Whichever exit eprom_internal_write_init_body takes (or none),
 * control comes back HERE, and this conditional is the ONLY place that
 * decides whether to clear the shared composite -- structural, not
 * remembered, so a `return` added inside the body later cannot silently
 * bypass it. Conditional on RESPONSE_CODE_ERROR, not unconditional: see
 * eprom_write_execute's own wrapper below for the named test that forced
 * that amendment and its cost if ignored -- the same reasoning applies
 * here even though (unlike eprom_write_execute) nothing in this body leaks
 * today; this wrapper's own comment above already says so.
 */
void eprom_write_init(firestarter_handle_t* handle) {
    eprom_internal_write_init_body(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);
    }
}

/*
 * Phase 141 Plan 04 (LOOP-03, D-08) -- see include/eprom.h for the full
 * rationale. factor == 0 (every shipped row) always yields 0, so the
 * per-byte loop's overprogram call is inert on every live protocol; the
 * clamp below yields 0 for cap_us == 0 without a special case, since a
 * positive product always compares greater than a zero cap.
 */
uint32_t __attribute__((noinline)) eprom_overprogram_us(uint8_t pulse_count, uint32_t pulse_us, uint8_t factor, uint32_t cap_us) {
    if (factor == 0) {
        return 0;
    }
    uint32_t product = (uint32_t)factor * pulse_count * pulse_us;
    return product > cap_us ? cap_us : product;
}

/*
 * Phase 141 Plan 04 (LOOP-05, D-04) -- the single place a per-byte program
 * budget failure is reported. Disables the VPP route exactly as the old
 * block-loop's failure path did, packs a 4-byte {addr_hi, addr_mid,
 * addr_lo, pulse_count} big-endian payload -- matching MSG_ERR_MAX_PULSES /
 * MSG_ERR_ENERGY_CAP's catalog shape (u24 address + u8 pulse count) --
 * emits it, and sets response_code.
 *
 * Phase 142 Plan 04 (VPP-02, VPP-03, resolved) -- this function's own
 * disable is now a reference to the shared EPROM_HV_ALL_OFF_MASK composite
 * (VPP-03's mask consolidation), and generalising the disable-on-error
 * guarantee to every OTHER exit in the write path is now
 * eprom_write_execute's own single-exit wrapper, below, which clears the
 * same composite structurally on every error exit -- this function is
 * simply the first of the exits that wrapper now also covers; it keeps
 * emitting the two budget-failure messages themselves, which the wrapper
 * does not.
 */
static void eprom_internal_report_budget_failure(firestarter_handle_t* handle, uint32_t address, uint8_t pulse_count, uint8_t msg_id) {
    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);
    uint8_t _b[4];
    _b[0] = (uint8_t)((address >> 16) & 0xFF);
    _b[1] = (uint8_t)((address >> 8)  & 0xFF);
    _b[2] = (uint8_t)( address        & 0xFF);
    _b[3] = pulse_count;
    LOG_ERROR_ID_BYTES(msg_id, _b, 4);
    handle->response_code = RESPONSE_CODE_ERROR;
}

/*
 * Debug session w27c512-program-fail-byte0 -- ONE program pulse, with the
 * program-voltage route asserted for its duration and released again before
 * the caller's verify read. See include/eprom.h's EPROM_VPP_SETUP_US /
 * EPROM_VPP_HOLD_US comment for the regression this restores, why the assert
 * has to be per-pulse rather than per-block, and where the two settle values
 * come from.
 *
 * CTRL_VPE_ENABLE is named here, never CTRL_VPP_P1_ENABLE, exactly as the
 * deleted program_mismatched_bytes() named it: eprom_internal_set_control_
 * register (bottom of this file) substitutes P1 for VPE whenever
 * using_p1_as_vpp(handle) holds, which is what made the pre-v1.31 traces
 * show +0x04/-0x04 on 0x07 (vpp_line = 0xFF sentinel) but +0x08/-0x08 on
 * 0x08 and 0x0B. Naming P1 directly here would break that substitution on
 * the two protocols that need it.
 *
 * The route bit survives the address latch that memory_set_data performs
 * between the assert and the CE strobe: mem_util_calculate_top_address_
 * register (memory.cpp) preserves CTRL_VPE_ENABLE and CTRL_VPP_P1_ENABLE
 * unconditionally, on every revision.
 *
 * Guarded by EPROM_OVERPROGRAM_SUPPORTED (include/eprom.h) because the
 * LOOP-03 overprogram site below is now its ONLY caller: the pass-batched
 * loop asserts and settles the route once per pass, inline, so nothing else
 * needs a single route-wrapped pulse. Left unguarded it would be an
 * unreferenced static on the leonardo build -- a -Wunused-function warning,
 * against an AVR warning policy of exactly zero.
 */
#if EPROM_OVERPROGRAM_SUPPORTED
static void eprom_internal_program_pulse(firestarter_handle_t* handle, uint32_t addr, uint8_t expected) {
    handle->firestarter_set_control_register(handle, CTRL_VPE_ENABLE, 1);
    delayMicroseconds(EPROM_VPP_SETUP_US);
    handle->firestarter_set_data(handle, addr, expected);
    delayMicroseconds(EPROM_VPP_HOLD_US);
    handle->firestarter_set_control_register(handle, CTRL_VPE_ENABLE, 0);
}
#endif

/*
 * Phase 142 Plan 04 (D-05, D-06, Q4) -- the single function that resolves
 * which EPROM high-voltage route to assert for the current handle. Called
 * from both eprom_internal_write_execute_body (below) and eprom_check_vpp
 * (VPP-03's mask/selection consolidation), replacing the two byte-identical
 * hand-rolled forks this file used to carry at what were :190 and :340.
 * Declared in eprom.h (Q4: exposed, not file-static) -- see that header
 * for the full rationale and the include-edge note.
 *
 * Resolution order, exactly (D-06: FLAG_VPE_AS_VPP is a live user-facing
 * escape hatch set by no database entry -- a pure human override for the
 * 25V NMOS parts and the manual-pot workflow -- so it is checked FIRST and
 * forces the direct-VPE path on top of whatever the table says, with no
 * table read needed on that arm):
 *   1. FLAG_VPE_AS_VPP set -> CTRL_VPP_REGULATOR_ENABLE (the direct-VPE
 *      path).
 *   2. row == NULL -- configure_eprom's own refusal already makes this
 *      unreachable in practice; re-checked here anyway, matching this
 *      file's existing row==NULL precedent inside the write-execute body
 *      below -> EPROM_HV_ROUTE_MASK, failing closed toward the
 *      drop-resistor path (the safer default: a regulated ~13V rather than
 *      an unregulated direct rail).
 *   3. row->vpp_path, read ONLY via pgm_read_byte -- eprom_params.h's
 *      PROGMEM contract requires it; a direct read compiles and silently
 *      returns RAM garbage on AVR: VPP_PATH_DIRECT_VPE ->
 *      CTRL_VPP_REGULATOR_ENABLE; VPP_PATH_DROP_RESISTOR, or any
 *      unrecognised value, -> EPROM_HV_ROUTE_MASK (also fails closed
 *      toward the drop path).
 */
rurp_register_t eprom_hv_route_mask(firestarter_handle_t* handle) {
    if (is_flag_set(FLAG_VPE_AS_VPP)) {
        return CTRL_VPP_REGULATOR_ENABLE;
    }
    const eprom_params_t* row = eprom_params_for(handle->protocol);
    if (row == NULL) {
        return EPROM_HV_ROUTE_MASK;
    }
    if (pgm_read_byte(&row->vpp_path) == VPP_PATH_DIRECT_VPE) {
        return CTRL_VPP_REGULATOR_ENABLE;
    }
    return EPROM_HV_ROUTE_MASK;
}

/*
 * Phase 142 Plan 04 (D-10 as amended, C-1, D-12) -- this is everything that
 * used to be the public eprom_write_execute, renamed static and stripped of
 * its own disable logic: see the public wrapper below for the single-exit
 * disable guarantee this body now relies on. A `return` added inside here
 * in the future cannot escape that guarantee, because this body has no
 * other way out.
 */
static void eprom_internal_write_execute_body(firestarter_handle_t* handle) {
    // --- once per block (LOOP-08) --- KEPT VERBATIM; only the line number
    // moves. D-05 / VPP-01 (resolved): route selection now comes from
    // eprom_hv_route_mask (include/eprom.h), driven by the eprom_params
    // table's vpp_path column -- replacing the hand-rolled
    // protocol==0x0B||FLAG_VPE_AS_VPP fork this guard used to wrap
    // directly. configure_eprom's pulse-fallback switch remains this
    // file's one surviving tier-1 protocol-keyed site.
    if (handle->firestarter_get_control_register(handle, CTRL_VPP_REGULATOR_ENABLE) == 0) {
        handle->firestarter_set_control_register(handle, eprom_hv_route_mask(handle), 1);
        delay(500);  // settle stays amortised once per block -- the whole of LOOP-08
    }

    // D-04 (resolved): the explicit pins>=32 clear that used to live here
    // (Phase 141) is REMOVED, not merely revised. Plan 142-02 (D-01/D-02)
    // revision-gated mem_util_calculate_top_address_register's preserve
    // mask so CTRL_VPP_VPE_DROP_ENABLE now SURVIVES a 32-pin block's
    // set_address() on Rev 2-class hardware -- an explicit clear here would
    // silently defeat that fix by re-stripping the very bit the guard
    // above just asserted, on every revision, before the fix's
    // revision-gated preserve mask ever gets a chance to matter. See
    // test_vpp01_dip32_drop_bit_survives_the_block_on_rev2_class
    // (test/native/avr/test_loop_eprom_v131) for the positive proof, and
    // test_loop08_the_28_pin_row_keeps_its_drop_bit for its unaffected
    // 28-pin partner.

    const eprom_params_t* row = eprom_params_for(handle->protocol);
    if (row == NULL) {
        // Already refused in configure_eprom (task 2) -- unreachable in
        // practice. Re-checked here so this function never dereferences a
        // NULL row on its own; returns without touching hardware further.
        return;
    }
    uint32_t energy_cap_us      = pgm_read_dword(&row->energy_cap_us);
    uint8_t  max_pulses         = pgm_read_byte(&row->max_pulses);
#if EPROM_OVERPROGRAM_SUPPORTED
    // Read only by the LOOP-03 site below, so guarded with it: unused
    // locals are -Wunused-variable and the AVR warning policy is zero.
    uint32_t overprogram_cap_us = pgm_read_dword(&row->overprogram_cap_us);
    uint8_t  overprogram_factor = pgm_read_byte(&row->overprogram_factor);
#endif
    uint8_t  verify_mode        = pgm_read_byte(&row->verify_mode);
    // Phase 142 / VPP-01 (resolved): vpp_path is read by eprom_hv_route_mask
    // above, at the top of this function -- not hoisted here, since nothing
    // below needs the raw column value again once the block's route has
    // already been asserted.
    uint32_t org_delay = handle->pulse_delay;
    // Phase 143 Plan 05 (HOST-02, D-02) -- feeds the time-gated intra-block
    // progress emission a few lines below; see that block's own comment for
    // the full BF-2 rationale behind the #ifndef SERIAL_ON_IO guard. The
    // declaration itself must be guarded too: an unreferenced local on
    // uno/uno328pb would be an unused-variable warning, and the AVR
    // warning policy is exactly zero.
#ifndef SERIAL_ON_IO
    uint32_t last_emit_ms = millis();
#endif

    /*
     * Debug session w27c512-write-slow-3x -- PASS-BATCHED program loop.
     *
     * WHAT CHANGED, AND THE MEASUREMENT THAT FORCED IT. This used to be a
     * single per-BYTE loop whose inner convergence loop called
     * eprom_internal_program_pulse() once per byte. That helper asserts the
     * program-voltage route, waits EPROM_VPP_SETUP_US, strobes, waits
     * EPROM_VPP_HOLD_US and de-asserts -- so it spent 1100 us of pure
     * route-settle PER BYTE. Measured on the bench (leonardo, W27C512,
     * 64 KiB pseudorandom image, 238 bytes of 0xFF): 1.568 s per 1024-byte
     * block = 1531 us/byte, 105.9 s for the device. gh#36's reporter
     * measured the same operation at 29.71 s on the v2.x firmware and
     * 108.74 s on v3.x; gh#42 recorded 139.2 s. include/eprom.h's own
     * EPROM_VPP_SETUP_US comment predicted this cost exactly ("~110 s per
     * 64 KiB write") -- it was simply never compared against v2.x.
     *
     * v2.0.6's program_mismatched_bytes() asserted the route ONCE per pass,
     * paid its settle ONCE per pass, strobed every flagged byte in the
     * block, then verified in a SEPARATE pass with the route down. That
     * amortisation -- one settle per 1024-byte block instead of 1024 of
     * them -- is what is restored here.
     *
     * SHAPE: a scan pass and a pulse pass, alternating.
     *   SCAN  (route down): read every byte still short of its target and
     *         flag it in `pending`. This IS the previous loop's LOOP-06 skip
     *         pair, and it is ALSO the post-pulse verify for the pass before
     *         it -- one read serves both roles, so the read count per byte
     *         is unchanged from the per-byte loop (scan, re-scan, final
     *         verify = 3).
     *   PULSE (route asserted once): strobe every flagged byte at org_delay.
     *
     * WHAT IS PRESERVED, precisely:
     *   - LOOP-06: a 0xFF target is never read and never pulsed; an
     *     already-matching byte is read once and never pulsed.
     *   - LOOP-01: the pulse width is always org_delay, never grown.
     *   - Per-byte pulse accounting: every flagged byte is strobed exactly
     *     once per pulse pass, so a byte still flagged after `pulses` passes
     *     has received exactly `pulses` pulses. `pulses` IS the per-byte
     *     count, not merely a pass counter.
     *   - Verify after every pulse: the scan pass immediately following a
     *     pulse pass reads back every byte that pass strobed, before any
     *     further pulse is emitted.
     *   - max_pulses / energy_cap_us, still tested only AFTER a failed
     *     verify and only after the converged-early break, so a byte that
     *     converges on its last permitted pulse still succeeds.
     *   - LOOP-03 overprogram, at the converging byte's OWN pulse count --
     *     `pending` is deliberately NOT cleared between passes so the scan
     *     can tell "was flagged, now matches" (converged THIS pass) from
     *     "never flagged" (matched all along), which is the only state
     *     needed to keep that per-byte fidelity.
     *   - The final full-block verify pass (VERIFY_PER_PULSE_PLUS_FINAL).
     *
     * MARGIN IS NOT TRADED FOR SPEED. The route is held up for the whole
     * pulse pass, so every strobe after the first sees a rail that has been
     * up for milliseconds -- strictly better settled than the per-pulse
     * 1000 us this replaces. And OE/VPP still comes fully down before every
     * verify read, which is the W27C512 Program-Verify requirement that
     * forced the per-pulse assert in the first place (see include/eprom.h's
     * EPROM_VPP_SETUP_US comment for that constraint in full).
     *
     * `pending` is a stack bitmask of DATA_BUFFER_SIZE/8 bytes (128 on
     * leonardo, 64 on uno-class) -- byte-for-byte the allocation v2.0.6's
     * mismatch_bitmask made on the same boards. It costs no static RAM.
     */
    const uint16_t block_len = (uint16_t)handle->data_size;
    uint8_t pending[DATA_BUFFER_SIZE / 8];
    memset(pending, 0, sizeof(pending));
    uint8_t pulses = 0;
    uint32_t accumulated = 0;

    for (;;) {
        uint16_t remaining = 0;
        uint32_t first_bad = handle->address;
        for (uint16_t i = 0; i < block_len; i++) {
            const uint8_t mask = (uint8_t)(1u << (i & 7));
            const bool was_pending = (pending[i >> 3] & mask) != 0;
            // After the first pass (pulses != 0), re-read ONLY the bytes the previous
            // pulse pass actually strobed. This is what keeps the per-byte
            // read count identical to the per-byte loop this replaces
            // (1 skip-check read + one verify read per pulse + the final
            // pass): a converged or never-flagged byte is never read again.
            if (pulses && !was_pending) {
                continue;
            }
            uint8_t expected = (uint8_t)handle->data_buffer[i];

        /*
         * Phase 143 Plan 05 (HOST-02, D-02/D-03/D-04) -- intra-block write
         * progress, compiled in on leonardo and native only.
         *
         * BF-2 (143-RESEARCH.md), in full: on uno/uno328pb the whole
         * per-byte loop runs inside one programmer-mode window
         * (operation_utils.cpp's _execute_operation calls
         * rurp_set_programmer_mode(); callback(handle); rurp_set_
         * communication_mode();), and rurp_set_programmer_mode()
         * (src/boards/uno_rurp_shield.cpp) tears the UART down for the
         * block's whole duration via rurp_serial_end(). The Uno's strong
         * rurp_log_id() override (same file) DEFERS rather than emits while
         * com_mode == false, into a 4-slot deferred_log buffer
         * (DEFERRED_LOG_MAX); a 5th deferred frame is silently DROPPED. On
         * this path that dropped slot would be one a subsequent
         * MSG_ERR_MAX_PULSES frame needs, turning a program FAILURE into a
         * host transport timeout -- exactly HOST-03's anti-goal, on a path
         * that works today without this emission.
         *
         * The same trap is already documented in-tree, twice, each guarded
         * at RUNTIME: src/boards/uno_rurp_shield.cpp's own DEFERRED_LOG_MAX
         * sizing-rationale comment ("an operation emits at most ~1-2
         * critical frames per programmer-mode window"), and
         * mem_util_blank_check's MSG_DATA_PROGRESS emit comment in
         * src/proms/memory.cpp, which gates on `handle->cmd !=
         * CMD_BLANK_CHECK`. This call site has no handle->cmd it could
         * test that way (eprom_write_execute only ever runs for CMD_WRITE),
         * so it is guarded at COMPILE time instead -- same defect class,
         * different mechanism.
         *
         * Rejected mitigations (each keeps the trap, at a cost):
         *   - a runtime com_mode accessor: costs an accessor call on every
         *     target and still delivers no intra-block progress on Uno,
         *     with no mechanism to reserve the error-frame slots;
         *   - raising DEFERRED_LOG_MAX: RAM cost on the tightest-RAM
         *     target, and still zero intra-block delivery on Uno;
         *   - reserving headroom by emitting at most DEFERRED_LOG_MAX - 2
         *     frames: a fragile invariant split across two files, and
         *     still no delivery on Uno.
         *
         * D-06's non-claim, both dimensions: intra-block write progress is
         * emitted on the EPROM path only, and delivered on leonardo only.
         *
         * The predicate below is TIME-keyed (millis()) and reads no
         * handle->protocol at all, so it adds no tier-1 protocol-keyed
         * site -- configure_eprom's pulse-fallback switch (:70) remains
         * this file's only one (TABLE-05).
         *
         * Payload is (absolute chip address, handle->mem_size) -- identical
         * shape to mem_util_blank_check's own MSG_DATA_PROGRESS emit
         * (memory.cpp), so 0xE0 keeps exactly ONE payload contract; a
         * block-relative pair (D-04's rejected alternative) would have
         * given the id a second meaning depending on which operation
         * emitted it.
         *
         * Placed BEFORE the LOOP-06 skips just below, so the cadence is
         * independent of how many bytes are skipped -- the more honest
         * reading of "progress" than gating on bytes actually pulsed.
         *
         * Unsigned-difference form: a millis() rollover (~49.7 days)
         * cannot stall the cadence -- (uint32_t) subtraction wraps
         * correctly regardless of which side of the rollover last_emit_ms
         * sits on.
         */
#ifndef SERIAL_ON_IO
        // Debug session w27c512-write-slow-3x: gated on `first_pass`. The
        // scan pass restarts at i == 0 on every pass, so emitting from a
        // later pass (pulses != 0) would send a LOWER address than a frame
        // already sent
        // and the host's write bar (which applies the frame's absolute
        // address verbatim -- eprom_operations.py::_apply_write_progress)
        // would visibly rewind. The first pass walks the whole block in
        // ascending order exactly once, so gating here keeps 0xE0's
        // addresses monotonic within a block, which is the property the
        // host relies on. Repeat passes are rare (they mean a byte did not
        // take its first pulse) and short.
        if (pulses == 0 &&
            (uint32_t)(millis() - last_emit_ms) >= EPROM_PROGRESS_EMIT_INTERVAL_MS) {
            last_emit_ms = millis();
            LOG_DATA_ID_U32_U32(MSG_DATA_PROGRESS, handle->address + i, handle->mem_size);
        }
#endif
            // LOOP-06 skips, before any pulse. 0xFF checked first, without a
            // read: an already-erased target never needs a pulse on a UV
            // EPROM (erased state is all-ones; programming only clears bits).
            if (expected == 0xFF) {
                continue;
            }
            const uint32_t addr = handle->address + i;
            if (handle->firestarter_get_data(handle, addr) == expected) {
                if (was_pending) {
                    // Converged on this pass, after exactly `pulses` pulses.
                    pending[i >> 3] &= (uint8_t)~mask;
#if EPROM_OVERPROGRAM_SUPPORTED
                    // LOOP-03: unreachable from any database row --
                    // `overprogram_factor` is ABSENT from all 746 rows of
                    // chip_database.json (the field would sit under
                    // `programming`), so eprom_overprogram_us returns 0 and
                    // no margin pulse is ever emitted. D-07's org_delay
                    // save/restore idiom: exactly one extra
                    // firestarter_set_data call at the computed width,
                    // restored immediately so no failure exit between save
                    // and restore can leak a modified pulse_delay into the
                    // handle. Route-wrapped per byte via
                    // eprom_internal_program_pulse: an overprogram pulse is
                    // a program pulse and needs the program voltage just as
                    // much, and this site runs with the pass's route down.
                    //
                    // COMPILED OUT ON LEONARDO ONLY (debug session
                    // w27c512-write-slow-3x, operator-adjudicated) to fund
                    // that target's flash. See include/eprom.h's
                    // EPROM_OVERPROGRAM_SUPPORTED comment for the Caterina-
                    // cliff reasoning and for the per-target divergence this
                    // creates the moment a row gains an overprogram_factor.
                    uint32_t op_us = eprom_overprogram_us(pulses, org_delay, overprogram_factor, overprogram_cap_us);
                    if (op_us) {
                        handle->pulse_delay = op_us;
                        eprom_internal_program_pulse(handle, addr, expected);
                        handle->pulse_delay = org_delay;
                    }
#endif
                }
                continue;
            }
            if (!was_pending) {
                pending[i >> 3] |= mask;
            }
            if (!remaining) {
                first_bad = addr;
            }
            remaining++;
        }
        if (!remaining) {
            break;  // whole block at target
        }
        // Budgets are checked only AFTER a failed verify (the scan above),
        // so a byte that converges on its last permitted pulse succeeds
        // instead of failing at the boundary.
        if (pulses >= max_pulses) {
            eprom_internal_report_budget_failure(handle, first_bad, pulses, MSG_ERR_MAX_PULSES);
            return;
        }
        // energy_cap_us == 0 means UNCAPPED (eprom_params.h) -- without
        // this guard, 0x07/0x08 (both ship energy_cap_us == 0) would
        // abort after their very first pulse.
        if (energy_cap_us && accumulated >= energy_cap_us) {
            eprom_internal_report_budget_failure(handle, first_bad, pulses, MSG_ERR_ENERGY_CAP);
            return;
        }

        // PULSE pass. Debug session w27c512-program-fail-byte0 is what put a
        // route assert on this path at all: Phase 141 shipped a bare
        // firestarter_set_data, which strobes CE with the 12 V rail
        // generated but never switched onto the part, so no cell can change
        // and every byte exhausts max_pulses. That assert is kept -- only
        // its GRANULARITY moves back to v2.0.6's, one per pass. Nothing is
        // logged inside this window: the progress emit lives in the scan
        // pass, with the route down.
        handle->firestarter_set_control_register(handle, CTRL_VPE_ENABLE, 1);
        delayMicroseconds(EPROM_VPP_SETUP_US);
        for (uint16_t i = 0; i < block_len; i++) {
            if (pending[i >> 3] & (uint8_t)(1u << (i & 7))) {
                handle->firestarter_set_data(handle, handle->address + i,
                                             (uint8_t)handle->data_buffer[i]);
            }
        }
        delayMicroseconds(EPROM_VPP_HOLD_US);
        handle->firestarter_set_control_register(handle, CTRL_VPE_ENABLE, 0);
        pulses++;
        accumulated += org_delay;  // D-02: pulse widths only
    }

    // verify_mode: 0x07/0x08 ship VERIFY_PER_PULSE_PLUS_FINAL -- one more
    // full-block read-and-compare pass, mirroring memory_verify_execute
    // (memory.cpp) exactly: same MSG_ERR_VERIFY id, same 5-byte payload,
    // same early return. 0x0B ships VERIFY_PER_PULSE -- no final pass.
    if (verify_mode == VERIFY_PER_PULSE_PLUS_FINAL) {
        // Debug session w27c512-write-slow-3x: this used to be a
        // byte-identical INLINE COPY of memory_verify_execute -- same
        // MSG_ERR_VERIFY id, same 5-byte {expected, actual, addr[2..0]}
        // payload, same early return on the first mismatch, same
        // RESPONSE_CODE_ERROR -- as this file's own comment stated. It is now
        // the call, so there is exactly one full-block verify in the tree.
        // The behavioural contract is unchanged in every respect, including
        // that it reads EVERY byte of the block (0xFF targets included), which
        // is what test_loop06_the_ff_rule_does_not_suppress_the_final_verify
        // _pass pins. Called at the end of the body so the single-exit
        // wrapper below still sees its RESPONSE_CODE_ERROR and disables the
        // route (test_vpp02_x4).
        memory_verify_execute(handle);
    }
}

/*
 * Phase 142 Plan 04 (D-10 as amended, C-1, D-12) -- the single-exit wrapper
 * VPP-02 requires. Whichever of the body's four error exits is taken
 * (row == NULL; MSG_ERR_MAX_PULSES; MSG_ERR_ENERGY_CAP; MSG_ERR_VERIFY --
 * the pre-existing headline gap, which used to disable nothing at all), or
 * the success fall-through, control returns HERE, and this conditional is
 * the ONLY place that decides whether to clear the shared composite --
 * structural, so a `return` added inside the body later cannot silently
 * bypass it.
 *
 * Conditional on RESPONSE_CODE_ERROR, not unconditional -- D-10's original
 * "unconditionally" is given up here (operator-confirmed correction C-1).
 * The tiebreaker is test_loop_eprom_v131.cpp's
 * test_loop05_a_successful_block_does_not_disable_the_route, which asserts
 * a SUCCESSFUL block leaves CTRL_VPP_REGULATOR_ENABLE SET. An unconditional
 * disable would re-arm the once-per-block guard above and re-pay
 * delay(500) on the very next block too -- roughly 64s added to a 64K Uno
 * write (128 blocks) against a typical 0x07 write's ~32s total at 100us
 * pulses. The OPERATION-level disable (as opposed to this per-block loop's
 * own disable-on-error) is command_done() (firestarter.cpp:162-171), which
 * zeroes CONTROL_REGISTER unconditionally on every command exit, success
 * or abort -- plan 142-06 owes the test proving that.
 */
void eprom_write_execute(firestarter_handle_t* handle) {
    eprom_internal_write_execute_body(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);
    }
}


uint16_t eprom_get_chip_id(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_GET_CHIP_ID);
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    delay(50);

    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE, 1);
    delay(100);
    uint16_t chip_id = handle->firestarter_get_data(handle, 0x0000) << 8;
    chip_id |= (handle->firestarter_get_data(handle, 0x0001));
    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // VPP-03: shared composite (was REGULATOR | A9)
    return chip_id;
}

void eprom_check_vpp(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_VPP);
#ifdef HARDWARE_REVISION
    if (rurp_get_hardware_revision() == REVISION_0) {
        LOG_WARN_ID(MSG_WARN_REV0_VPP_UNSUPPORTED);
        handle->response_code = RESPONSE_CODE_WARNING;
        return;
    }
#endif
    // D-05 / VPP-03 (resolved): route selection via eprom_hv_route_mask --
    // see eprom_internal_write_execute_body's identical call, above, for
    // the full rationale. Replaces the byte-identical
    // protocol==0x0B||FLAG_VPE_AS_VPP fork this file used to carry twice.
    handle->firestarter_set_control_register(handle, eprom_hv_route_mask(handle), 1);

    delay(100);
    uint16_t vpp_mv = rurp_read_voltage_mv();
    LOG_DEBUG_ID_SUB_U16(DBG_CHECKING_VPP_VOLTAGE, vpp_mv);
    if (vpp_mv > (uint32_t)handle->vpp_mv + 500) {
        {
            bool force = is_flag_set(FLAG_FORCE);
            mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv,
                                    force ? MSG_WARN_VPP_HIGH : MSG_ERR_VPP_HIGH,
                                    force ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
        }
    } else if (vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100) {
        {
            mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv,
                                    MSG_WARN_VPP_LOW, RESPONSE_CODE_WARNING);
        }
    }
    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // VPP-03: shared composite (was REGULATOR | DROP)
}

void eprom_internal_erase(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_INTERNAL_ERASE);
    rurp_chip_input();
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);  // Enable regulator without dropping resistor
    delay(100);
    handle->firestarter_set_address(handle, 0x0000);
    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE, 1);  // Erase with VPE - assumes CTRL_VPP_VPE_DROP_ENABLE isn't set and left active previously
    delay(100);
    rurp_chip_enable();
    /* Debug session w27c512-devtest-all-bad: the ERASE pulse, not the
     * program pulse. This spent `handle->pulse_delay` until now -- the
     * per-byte PROGRAM width the host sends from the database's
     * `pulse_duration_us` (100 us for W27C512) -- against a datasheet CE
     * erase pulse width T_PWE of 95/100/105 ms, so the part only ever
     * partially erased and `dev test` reported four BAD steps for one
     * cause. See include/eprom.h's EPROM_ERASE_PULSE_US comment for the
     * bounds, the 28-row blast radius, and why no per-row erase width
     * exists. Still routed through mem_util_delay_us (LOOP-07/D-06 site 2):
     * 100000 us is far over the AVR delayMicroseconds ceiling, which is
     * precisely what that split helper is for. */
    mem_util_delay_us(EPROM_ERASE_PULSE_US);
    // After the erase pulse, we should disable the chip to end the programming cycle.
    rurp_chip_disable();

    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // VPP-03: shared composite (was REGULATOR | A9 | VPE)
}

void eprom_generic_init(firestarter_handle_t* handle) {
    eprom_check_vpp(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        return;
    }
    if (handle->chip_id > 0) {
        eprom_internal_check_chip_id(handle, is_flag_set(FLAG_FORCE) ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
    }
}

void eprom_internal_check_chip_id(firestarter_handle_t* handle, uint8_t error_code) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID);
    uint16_t chip_id = eprom_get_chip_id(handle);
    mem_util_report_chip_id(handle, chip_id, error_code == RESPONSE_CODE_WARNING);
}
// Use this function to set the control register and flip CTRL_VPE_ENABLE bit to CTRL_VPE_ENABLE or CTRL_VPP_P1_ENABLE
void eprom_internal_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool state) {
    if (bit & CTRL_VPE_ENABLE && using_p1_as_vpp(handle)) {
        bit &= ~CTRL_VPE_ENABLE;
        bit |= CTRL_VPP_P1_ENABLE;
    }
    ep_set_control_register(handle, bit, state);
}
