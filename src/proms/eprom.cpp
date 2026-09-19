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

    // Both refusals below run AFTER the fallback
    // switch above resolves pulse_delay: a pulse_delay of 0 would otherwise
    // compare as "not greater than the cap" vacuously, letting an
    // unresolved default slip past the protocol check.

    // Refusal 1: an unrecognised protocol fails closed
    // here rather than falling back to &EPROM_PARAMS[0], which would route
    // 13V through the drop resistor for an unknown part.
    const eprom_params_t* row = eprom_params_for(handle->protocol);
    if (row == NULL) {
        LOG_ERROR_ID_U8(MSG_ERR_PROTOCOL_NOT_IMPLEMENTED, (uint8_t)handle->protocol);
        handle->response_code = RESPONSE_CODE_ERROR;
        return;
    }

    // Refusal 2: a pulse wider than this row's program-energy budget is refused
    // pre-flight, before any high voltage is enabled, and NEVER silently clamped
    // -- clamping emits a pulse whose width no longer matches what the caller
    // asked for, then reports a verify failure: a misconfiguration wearing a
    // silicon-failure costume.
    //
    // energy_cap_us > 0 is mandatory: 0 means UNCAPPED, not "cap at zero". An
    // unguarded compare refuses every 0x07/0x08 pulse -- both ship 0.
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
 * DEFENSIVE, not corrective: neither exit of this body leaks a route today.
 * It is wrapped anyway because eprom_internal_erase's assert is the only
 * write-path code that raises A9 together with VPE, and that assert's safety
 * rests on there being no `return` between it and its own clear -- a property
 * a future edit could otherwise break silently.
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
 * Single-exit wrapper: whichever exit the body takes, control returns HERE,
 * and this is the only place that decides whether to clear the shared HV
 * composite. Structural, so a `return` added inside the body later cannot
 * bypass it. Conditional on RESPONSE_CODE_ERROR -- see eprom_write_execute's
 * wrapper below for why it must not be unconditional.
 */
void eprom_write_init(firestarter_handle_t* handle) {
    eprom_internal_write_init_body(handle);
    if (handle->response_code == RESPONSE_CODE_ERROR) {
        handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);
    }
}

/*
 * See include/eprom.h for the full rationale. factor == 0 (every shipped row) always yields 0, so the
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
 * The single place a per-byte program-budget failure is reported: disables the
 * VPP route, packs a 4-byte big-endian {addr_hi, addr_mid, addr_lo,
 * pulse_count} payload matching MSG_ERR_MAX_PULSES / MSG_ERR_ENERGY_CAP, emits
 * it, and sets response_code. The wrapper below covers the disable for every
 * other error exit; this function additionally emits the two messages, which
 * the wrapper does not.
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
 * ONE program pulse, with the program-voltage route asserted for its duration
 * and released before the caller's verify read. include/eprom.h's
 * EPROM_VPP_SETUP_US comment has the per-pulse constraint and the values.
 *
 * Name CTRL_VPE_ENABLE here, never CTRL_VPP_P1_ENABLE:
 * eprom_internal_set_control_register substitutes P1 for VPE when
 * using_p1_as_vpp(handle) holds, and naming P1 directly breaks that
 * substitution on the two protocols that need it.
 *
 * The route bit survives the address latch memory_set_data performs between
 * the assert and the CE strobe -- mem_util_calculate_top_address_register
 * preserves both route bits on every revision.
 *
 * Guarded by EPROM_OVERPROGRAM_SUPPORTED because the overprogram site is its
 * only caller; unguarded it is an unreferenced static on leonardo, and the
 * AVR warning policy is zero.
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
 * Resolves which high-voltage route to assert for the current handle. Called
 * from both the write body and eprom_check_vpp. Declared in eprom.h (exposed,
 * not file-static) so a native test can drive it directly.
 *
 * Resolution order, exactly:
 *   1. FLAG_VPE_AS_VPP -> CTRL_VPP_REGULATOR_ENABLE. Checked FIRST because it
 *      is a pure human override (25V NMOS parts, the manual-pot workflow) set
 *      by no database entry, so it wins over the table with no table read.
 *   2. row == NULL -> EPROM_HV_ROUTE_MASK, failing closed toward the
 *      drop-resistor path -- a regulated ~13V rather than an unregulated rail.
 *   3. row->vpp_path, read ONLY via pgm_read_byte (a direct read compiles and
 *      silently returns RAM garbage on AVR): VPP_PATH_DIRECT_VPE ->
 *      CTRL_VPP_REGULATOR_ENABLE; anything else, including unrecognised
 *      values, -> EPROM_HV_ROUTE_MASK, also failing closed toward the drop path.
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
    if (handle->vpp_mv > RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV) {
        return CTRL_VPP_REGULATOR_ENABLE;
    }
    return EPROM_HV_ROUTE_MASK;
}

/*
 * This is everything that used to be the public eprom_write_execute,
 * renamed static and stripped of
 * its own disable logic: see the public wrapper below for the single-exit
 * disable guarantee this body now relies on. A `return` added inside here
 * in the future cannot escape that guarantee, because this body has no
 * other way out.
 */
static void eprom_internal_write_execute_body(firestarter_handle_t* handle) {
    // --- once per block --- KEPT VERBATIM; only the line number
    // Route selection comes from eprom_hv_route_mask, driven by the eprom_params
    // vpp_path column.
    if (handle->firestarter_get_control_register(handle, CTRL_VPP_REGULATOR_ENABLE) == 0) {
        handle->firestarter_set_control_register(handle, eprom_hv_route_mask(handle), 1);
        delay(500);  // settle stays amortised once per block
    }

    // Do NOT add a pins>=32 clear here. mem_util_calculate_top_address_register's
    // revision-gated preserve mask lets CTRL_VPP_VPE_DROP_ENABLE survive a 32-pin
    // block's set_address() on Rev 2-class hardware; an explicit clear would strip
    // the very bit the guard above just asserted, on every revision.

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
    // Read only by the overprogram site below, so guarded with it: unused
    // locals are -Wunused-variable and the AVR warning policy is zero.
    uint32_t overprogram_cap_us = pgm_read_dword(&row->overprogram_cap_us);
    uint8_t  overprogram_factor = pgm_read_byte(&row->overprogram_factor);
#endif
    uint8_t  verify_mode        = pgm_read_byte(&row->verify_mode);
    // vpp_path is read by eprom_hv_route_mask
    // above, at the top of this function -- not hoisted here, since nothing
    // below needs the raw column value again once the block's route has
    // already been asserted.
    uint32_t org_delay = handle->pulse_delay;
    // Feeds the time-gated intra-block progress emission a few lines
    // below; see that block's own comment for the full rationale behind
    // the #ifndef SERIAL_ON_IO guard. The
    // declaration itself must be guarded too: an unreferenced local on
    // uno/uno328pb would be an unused-variable warning, and the AVR
    // warning policy is exactly zero.
#ifndef SERIAL_ON_IO
    uint32_t last_emit_ms = millis();
#endif

    /*
     * PASS-BATCHED program loop: a scan pass and a pulse pass, alternating.
     *   SCAN  (route down): read every byte still short of its target and flag it
     *         in `pending`. This read is also the post-pulse verify for the
     *         previous pass, so the read count per byte is unchanged.
     *   PULSE (route asserted once): strobe every flagged byte at org_delay.
     *
     * The batching is the point: asserting the route per BYTE costs 1100 us of
     * settle each time -- 1531 us/byte, ~106 s for a 64 KiB device, measured. One
     * settle per block instead of 1024 of them.
     *
     * Preserved from the per-byte loop, precisely:
     *   - A 0xFF target is never read and never pulsed; an already-matching byte
     *     is read once and never pulsed.
     *   - The pulse width is always org_delay, never grown.
     *   - `pulses` is the per-BYTE count, not a pass counter: every flagged byte
     *     is strobed exactly once per pulse pass.
     *   - max_pulses / energy_cap_us are tested only AFTER a failed verify and
     *     after the converged-early break, so a byte converging on its last
     *     permitted pulse still succeeds.
     *   - `pending` is deliberately NOT cleared between passes, so the scan can
     *     distinguish "was flagged, now matches" (converged this pass) from
     *     "never flagged" (matched all along) -- the state overprogram needs.
     *
     * MARGIN IS NOT TRADED FOR SPEED. The route is held up for the whole pulse
     * pass, so every strobe after the first sees a better-settled rail than the
     * per-pulse version gave. OE/VPP still comes fully down before every verify
     * read, which is the Program-Verify requirement that forces the assert.
     *
     * `pending` is a stack bitmask of DATA_BUFFER_SIZE/8 bytes. No static RAM.
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
         * Intra-block write progress. Compiled in on leonardo and native ONLY.
         *
         * On uno/uno328pb the whole loop runs inside one programmer-mode window, and
         * rurp_set_programmer_mode() tears the UART down for its duration. The Uno's
         * rurp_log_id() override defers rather than emits while com_mode is false,
         * into a 4-slot buffer; a 5th deferred frame is silently DROPPED. That
         * dropped slot would be one a subsequent MSG_ERR_MAX_PULSES frame needs,
         * turning a program FAILURE into a host transport timeout. Hence a
         * COMPILE-time guard, not a runtime one -- this site has no handle->cmd it
         * could test.
         *
         * Payload is (absolute chip address, mem_size), the same shape
         * mem_util_blank_check emits, so 0xE0 keeps exactly one payload contract.
         *
         * Placed BEFORE the skips below so the cadence is independent of how many
         * bytes are skipped. The unsigned-difference form means a millis() rollover
         * cannot stall it.
         */
#ifndef SERIAL_ON_IO
        // Gated on `first_pass`: the scan pass restarts at i == 0 every pass, so
        // emitting from a later pass would send a LOWER address than one already sent
        // and the host's write bar would visibly rewind. The first pass walks the
        // block in ascending order exactly once, which is the monotonicity the host
        // relies on.
        if (pulses == 0 &&
            (uint32_t)(millis() - last_emit_ms) >= EPROM_PROGRESS_EMIT_INTERVAL_MS) {
            last_emit_ms = millis();
            LOG_DATA_ID_U32_U32(MSG_DATA_PROGRESS, handle->address + i, handle->mem_size);
        }
#endif
            // Skips, before any pulse. 0xFF checked first, without a
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
                    // Unreachable from any current database row: no chip sets
                    // overprogram_factor, so eprom_overprogram_us returns 0 and no
                    // margin pulse is emitted.
                    //
                    // The org_delay save/restore is immediate, so no failure exit
                    // between save and restore can leak a modified pulse_delay into
                    // the handle. Route-wrapped per byte, because an overprogram pulse
                    // is a program pulse and this site runs with the pass's route down.
                    //
                    // COMPILED OUT ON LEONARDO to fund that target's flash -- see
                    // include/eprom.h's EPROM_OVERPROGRAM_SUPPORTED.
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

        // PULSE pass. The route assert is required: without it the CE strobe fires
        // with the 12 V rail generated but never switched onto the part, so no cell
        // changes and every byte exhausts max_pulses. Granularity is one per pass.
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
        accumulated += org_delay;  // pulse widths only
    }

    // verify_mode: 0x07/0x08 ship VERIFY_PER_PULSE_PLUS_FINAL -- one more
    // full-block read-and-compare pass, mirroring memory_verify_execute
    // (memory.cpp) exactly: same MSG_ERR_VERIFY id, same 5-byte payload,
    // same early return. 0x0B ships VERIFY_PER_PULSE -- no final pass.
    if (verify_mode == VERIFY_PER_PULSE_PLUS_FINAL) {
        memory_verify_execute(handle);
    }
}

/*
 * Single-exit wrapper. Whichever of the body's four error exits is taken (row
 * NULL, MAX_PULSES, ENERGY_CAP, VERIFY) or the success fall-through, control
 * returns HERE, and this is the ONLY place deciding whether to clear the
 * shared composite -- structural, so a later `return` inside the body cannot
 * bypass it.
 *
 * Conditional on RESPONSE_CODE_ERROR, NOT unconditional. An unconditional
 * disable re-arms the once-per-block guard above and re-pays delay(500) on the
 * next block too -- roughly 64 s added to a 64 K Uno write. A successful block
 * must leave the route asserted.
 *
 * The operation-level disable is command_done(), which zeroes CONTROL_REGISTER
 * on every command exit regardless.
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
    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // shared composite (was REGULATOR | A9)
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
    // Route selection via eprom_hv_route_mask --
    // see eprom_internal_write_execute_body's identical call, above, for
    // the full rationale. Replaces the byte-identical
    // protocol==0x0B||FLAG_VPE_AS_VPP fork this file used to carry twice.
    handle->firestarter_set_control_register(handle, eprom_hv_route_mask(handle), 1);

    delay(100);
    uint16_t vpp_mv = rurp_read_voltage_mv();
    LOG_DEBUG_ID_SUB_U16(DBG_CHECKING_VPP_VOLTAGE, vpp_mv);
    if (vpp_mv > (uint32_t)handle->vpp_mv + 500) {
        bool force = is_flag_set(FLAG_FORCE);
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv,
                                 force ? MSG_WARN_VPP_HIGH : MSG_ERR_VPP_HIGH,
                                 force ? RESPONSE_CODE_WARNING : RESPONSE_CODE_ERROR);
    } else if (vpp_mv < (uint32_t)handle->vpp_mv * 95 / 100) {
        mem_util_report_voltage(handle, vpp_mv, handle->vpp_mv, MSG_WARN_VPP_LOW, RESPONSE_CODE_WARNING);
    }
    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // shared composite (was REGULATOR | DROP)
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
    /* The ERASE pulse width, NOT the per-byte program width the host sends. Using
     * pulse_duration_us here (100 us for W27C512) against a T_PWE of 95/100/105 ms
     * only partially erases the part, and surfaces as four unrelated `dev test`
     * failures. include/eprom.h's EPROM_ERASE_PULSE_US has the bounds. Routed
     * through mem_util_delay_us because 100000 us is far over the AVR
     * delayMicroseconds ceiling. */
    mem_util_delay_us(EPROM_ERASE_PULSE_US);
    // After the erase pulse, we should disable the chip to end the programming cycle.
    rurp_chip_disable();

    handle->firestarter_set_control_register(handle, EPROM_HV_ALL_OFF_MASK, 0);  // shared composite (was REGULATOR | A9 | VPE)
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

// error_code stays a PARAMETER: the two callers disagree on policy by design.
// The standalone CMD_CHECK_CHIP_ID entry passes RESPONSE_CODE_ERROR
// unconditionally so it refuses regardless of --force, while eprom_generic_init
// passes a FLAG_FORCE-derived value. Do not fold the flag test inward.
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
