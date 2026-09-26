/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "eeprom_28c.h"

#include <Arduino.h>

#include "firestarter.h"
#include "flash_utils.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "operation_utils.h"
#include "rurp_pinout.h"

/* Conservative page-size floor, used when the host delivers no page_size.
 * Do NOT replace it with a density-derived band table: AT28MC010 and AT28C010
 * are both 128 KB but have page sizes of 64 and 128, so density alone cannot
 * select the right value.
 *
 * AT28C_PAGE_SIZE_MAX is a board-INVARIANT validation ceiling, deliberately
 * not DATA_BUFFER_SIZE (512 on uno/native, 1024 on leonardo), so the
 * validation rule is the same on every build. It sits above the largest page
 * any database row carries. */
#define AT28C_PAGE_SIZE_FALLBACK 64
#define AT28C_PAGE_SIZE_MAX 512

// t_WC, ms: unconditional wall-clock floor before polling for SDP-disable
// completion [Microchip DS20006432B 6.6.2 p.10 / DS20006386B p.10]. Bounds
// the internal WRITE CYCLE after the sequence's last byte -- not the same
// thing as AT28C_TBLC_MAX_US, which bounds the inter-byte window.
#define AT28C_TWC_MAX_MS 10

// t_EC, ms: whole-device erase cycle following the six-byte erase sequence
// [Atmel AN "Software Chip Erase" Rev 0544B-10/98, t_EC 20 ms max]. The note
// forbids any byte load until the erase completes, so this must stay an
// UNCONDITIONAL DELAY -- the erase path must not reuse the SDP completion
// poll.
#define AT28C_TEC_MAX_MS 20

// t_BLC, us: datasheet MAXIMUM interval between consecutive byte loads within
// a command sequence [Microchip DS20006432B 6.6.2 p.10]. A budget to stay
// under, NOT a delay to insert -- the emit loop has no inter-byte wait and
// runs far under it on a 16 MHz AVR. Checked at runtime against the measured
// emit duration.
#define AT28C_TBLC_MAX_US 100

// DQ6 toggle bit sampled during an internal write cycle by the completion
// poll below.
#define AT28C_DQ6_TOGGLE_MASK 0x40

// An ITERATION COUNT, not a millis() deadline: the native SDP suites mock
// millis() to always return 0, so a wall-clock loop would never terminate
// under a deliberately non-settling mock.
#define AT28C_TOGGLE_POLL_MAX_READS 32

// The poll address may be any location but MUST NOT vary between reads
// [AT28C256 datasheet 6.16/6.17 note 3]. Named so the invariance is
// structural.
#define EEPROM28C_TOGGLE_POLL_ADDRESS 0x5555

// Data-polling bit. During an internal page-write cycle a read of the LAST
// BYTE WRITTEN returns the COMPLEMENT of that byte's DQ7; on completion DQ7
// matches. Compare ONLY this bit, never the whole byte -- a whole-byte
// compare conflates completion with verification and passes spuriously
// wherever the old byte already equals the new one (blank 0xFF regions).
#define AT28C_DQ7_MASK 0x80

// Iteration count, not a deadline -- same reason as
// AT28C_TOGGLE_POLL_MAX_READS above.
#define AT28C_PAGE_POLL_MAX_READS 2000

void eeprom28c_write_init(firestarter_handle_t* handle);
void eeprom28c_write_execute(firestarter_handle_t* handle);
static void eeprom28c_emit_command_sequence(firestarter_handle_t* handle, const byte_flip_t* sequence, size_t length);
static void eeprom28c_wait_for_sdp_completion(firestarter_handle_t* handle);
static bool eeprom28c_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected);
static bool eeprom28c_verify_page_readback(firestarter_handle_t* handle, uint32_t first_index, uint32_t last_index);
// The shared SDP timed-emit helper and the two standalone, payload-free
// lock/unlock operations it drives.
static void eeprom28c_emit_sdp_sequence_timed(firestarter_handle_t* handle, const byte_flip_t* sequence, size_t length,
                                               uint8_t emitted_msg_id, uint8_t done_us_msg_id);
static void eeprom28c_sdp_unlock_execute(firestarter_handle_t* handle);
static void eeprom28c_sdp_lock_execute(firestarter_handle_t* handle);
// The AN-0544B SOFTWARE six-byte chip erase -- deliberately NOT the
// datasheet's HARDWARE Chip Erase mode (12V on OE).
static void eeprom28c_erase_execute(firestarter_handle_t* handle);

// AT28C SDP disable: 6-write sequence to magic addresses. Byte-identical to
// flash_utils.h's FLASH_DISABLE_WRITE_PROTECTION; the duplication is
// deliberate and a cross-guard pins the two together so they cannot diverge.
//
// The `extern` below is load-bearing: in C++ a namespace-scope const array has
// INTERNAL linkage unless a prior extern declaration is visible, and the guard
// must read this production array rather than a test-local copy.
extern const byte_flip_t EEPROM_SDP_DISABLE[6];
const byte_flip_t EEPROM_SDP_DISABLE[6] = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x80},
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0x20},
};

// AT28C SDP enable: 3-write sequence, terminal byte 0xA0 [Atmel doc0270 rev
// 0270L-PEEPR-2/09 sec 19 note 2, corroborated by Microchip DS20006432B sec
// 6.18 note 2: the Write Protect state activates at the end of the write
// cycle EVEN IF NO OTHER DATA IS LOADED].
//
// SAFETY INVARIANT. These three writes are byte-identical to
// FLASH_ENABLE_WRITE, the protected-WRITE PREFIX. The ONLY thing separating
// "lock the chip" from "prefix a byte write" is that NO DATA WRITE FOLLOWS.
// The absence of a payload IS the semantics:
//
//   1. Do NOT dedupe these tables against flash_utils.h. Once the bytes
//      match, the array NAME is the only discriminator left.
//   2. The absence cannot be proven by comparing tables -- they are equal by
//      construction. It has to be asserted on the emitted STREAM.
//
// The `extern` below is load-bearing for the same linkage reason as above.
extern const byte_flip_t EEPROM_SDP_ENABLE[3];
const byte_flip_t EEPROM_SDP_ENABLE[3] = {
    {0x5555, 0xAA},
    {0x2AAA, 0x55},
    {0x5555, 0xA0},
};

void configure_eeprom28c(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CONFIGURING_EEPROM_28C);
    // AT28C page write timing requires fast consecutive writes; no pulse delay needed
    handle->pulse_delay = 0;
    // Do NOT add a default: arm here. configure_memory pre-sets the generic
    // main for CMD_READ/CMD_WRITE before calling this, so a blanket default
    // would overwrite it and refuse read on every 0x0D chip. (This comment
    // used to also name the verify command's wire ordinal here, retired in
    // 3.1.0; configure_memory's switch pre-sets only CMD_READ and CMD_WRITE
    // now.) Unsupported commands are refused generically by the operation
    // layer's NULL-main guard instead.
    switch (handle->cmd) {
        case CMD_WRITE:
            handle->firestarter_operation_init = eeprom28c_write_init;
            handle->firestarter_operation_main = eeprom28c_write_execute;
            break;
        case CMD_ERASE:
            handle->firestarter_operation_main = eeprom28c_erase_execute;
            break;
        case CMD_SDP_UNLOCK:
            handle->firestarter_operation_main = eeprom28c_sdp_unlock_execute;
            break;
        case CMD_SDP_LOCK:
            handle->firestarter_operation_main = eeprom28c_sdp_lock_execute;
            break;
    }
}

// A9-12V chip-identification check for the AT28C EEPROM family.
// Mirrors eprom_get_chip_id (eprom.cpp:186-197) for the read mechanism and
// flash_intel_check_chip_id (flash_intel.cpp) for compare + response.
// Read addresses are derived from mem_size: AT28C256 = 0x7FC0/0x7FC1,
// AT28C64 = 0x1FC0/0x1FC1, etc. Caller-visible via response_code only;
// no declaration in eeprom_28c.h (static — internal linkage only).
static void eeprom28c_check_chip_id(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID_28C);
    // Underflow guard: mem_size < 64 would wrap mfr_addr to ~0xFFFFFFC0 and drive
    // 12V on A9 of an arbitrary address. Canonical DB entries are >= 2 KiB, but
    // hand-crafted JSON could reach here. Treat as configuration error.
    if (handle->mem_size < 64) {
        if (is_flag_set(FLAG_FORCE)) {
            LOG_WARN_ID_U32(MSG_WARN_MEM_SIZE_TOO_SMALL, (uint32_t)handle->mem_size);
            handle->response_code = RESPONSE_CODE_WARNING;
        } else {
            LOG_ERROR_ID_U32(MSG_ERR_MEM_SIZE_TOO_SMALL, (uint32_t)handle->mem_size);
            handle->response_code = RESPONSE_CODE_ERROR;
        }
        return;
    }
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE, 1);
    delay(50);
    handle->firestarter_set_control_register(handle, CTRL_VPP_A9_ENABLE, 1);
    delay(100);
    uint32_t mfr_addr = handle->mem_size - 64;  // 0x7FC0 (AT28C256) / 0x1FC0 (AT28C64) / ...
    uint16_t chip_id = handle->firestarter_get_data(handle, mfr_addr) << 8;
    chip_id |= handle->firestarter_get_data(handle, mfr_addr + 1);
    handle->firestarter_set_control_register(handle, CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_A9_ENABLE, 0);
    mem_util_report_chip_id(handle, chip_id, is_flag_set(FLAG_FORCE));
}

// Remap-aware command-sequence emitter. Every write goes through
// handle->firestarter_set_data (memory_set_data), which applies the full
// address remap and rewrites CONTROL_REGISTER on each address change --
// unlike flash_utils.cpp's path, which writes raw address bytes only.
//
// HARD CONSTRAINT: nothing bus-visible in this body beyond the data-direction
// call and the set_data loop. The recorded goldens come from a bare set_data
// loop, so any extra bus-visible call -- a set_control_register bracket in
// particular -- appends strobes and breaks full-stream equality. No LOG_ calls
// either: report lines go before or after the sequence, never inside it.
static void eeprom28c_emit_command_sequence(firestarter_handle_t* handle, const byte_flip_t* sequence, size_t length) {
    // Required: memory_set_data never sets the bus direction, memory_get_data
    // sets INPUT, and the chip-id reads sit immediately upstream. Without this
    // the OUTPUT direction would be correct only incidentally.
    rurp_set_data_output();
    for (size_t i = 0; i < length; i++) {
        handle->firestarter_set_data(handle, sequence[i].address, sequence[i].byte);
    }
}

// NEVER read back a command-sequence byte and compare it: both datasheets
// state such a byte "is not written to the device", so comparing a read at
// address 0x5555 against 0x20 can only pass when the sequence was NOT
// recognised -- an inverted check.
//
// Waits t_WC unconditionally, then polls the invariant address until two
// consecutive samples agree on the DQ6 toggle bit, bounded by iteration count.
//
// NEVER writes handle->response_code and emits NO LOG_ call on any path. A
// stuck internal cycle stays silent here and surfaces as the first page
// write's poll failure instead -- one failure path per fault, so clobbering a
// prior severity is structurally impossible.
//
// Reads go through handle->firestarter_get_data, never fu_flash_data_poll():
// that helper emits four recorded strobes per read, which would inject
// entries into the compared stream.
static void eeprom28c_wait_for_sdp_completion(firestarter_handle_t* handle) {
    delay(AT28C_TWC_MAX_MS);
    uint8_t previous = handle->firestarter_get_data(handle, EEPROM28C_TOGGLE_POLL_ADDRESS);
    for (uint8_t j = 0; j < AT28C_TOGGLE_POLL_MAX_READS; j++) {
        delayMicroseconds(10);
        uint8_t observed = handle->firestarter_get_data(handle, EEPROM28C_TOGGLE_POLL_ADDRESS);
        if ((observed & AT28C_DQ6_TOGGLE_MASK) == (previous & AT28C_DQ6_TOGGLE_MASK)) {
            return;
        }
        previous = observed;
    }
    // Bound exhausted -- fall out silently. No response_code write, no LOG_
    // call, on this path either.
}

// Wraps an SDP command sequence with its report pair, a micros() bracket and
// the t_BLC budget check. Shared by the auto-unlock and both standalone ops.
//
// The micros() reads bracket ONLY the emit call, so they do not perturb
// inter-byte timing. The completion wait that follows a sequence stays at each
// call site -- the unlock polls, the lock just delays t_WC, and
// check_no_log_in_sdp_window.py needs the wait anchor inside the caller's body.
//
// The budget is derived from `length`, never a literal: 6 writes gives 600 us,
// 3 gives 300 us. A bench run measured 572 us against the unlock's 600 us, so
// this check is live, not latent.
//
// LOG_ calls belong here BY DESIGN. Do not add this function as a scanned
// window in check_no_log_in_sdp_window.py -- the real timing window is
// eeprom28c_emit_command_sequence's body.
static void eeprom28c_emit_sdp_sequence_timed(firestarter_handle_t* handle, const byte_flip_t* sequence, size_t length,
                                               uint8_t emitted_msg_id, uint8_t done_us_msg_id) {
    LOG_ID(emitted_msg_id);
    uint32_t sdp_emit_start_us = micros();
    eeprom28c_emit_command_sequence(handle, sequence, length);
    uint32_t sdp_emit_us = (uint32_t)(micros() - sdp_emit_start_us);
    LOG_ID_U32(done_us_msg_id, sdp_emit_us);

    uint32_t sdp_tblc_budget_us = (uint32_t)length * AT28C_TBLC_MAX_US;
    if (sdp_emit_us > sdp_tblc_budget_us) {
        LOG_WARN_ID_U32(MSG_WARN_SDP_TBLC_EXCEEDED, sdp_emit_us);
    }
}

// The standalone unlock reuses the existing ids (MSG_INFO_SDP_UNLOCK /
// MSG_INFO_SDP_UNLOCK_DONE_US) so an SDP unlock reads identically on the
// wire however it was triggered -- from eeprom28c_write_init's auto-unlock or
// from this standalone CMD_SDP_UNLOCK op. Reusing the SAME completion wait
// (eeprom28c_wait_for_sdp_completion) is what makes the standalone unlock's
// emitted stream byte-identical to the auto-unlock's -- an equality the
// native suite asserts. This op writes no handle->response_code.
static void eeprom28c_sdp_unlock_execute(firestarter_handle_t* handle) {
    size_t sdp_seq_len = sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]);
    eeprom28c_emit_sdp_sequence_timed(handle, EEPROM_SDP_DISABLE, sdp_seq_len, MSG_INFO_SDP_UNLOCK,
                                       MSG_INFO_SDP_UNLOCK_DONE_US);
    eeprom28c_wait_for_sdp_completion(handle);
}

// The lock is exactly three writes plus the t_WC delay, and NOTHING else.
// It deliberately does NOT call eeprom28c_wait_for_sdp_completion: that adds
// reads, and a read folds READ_FLAG into CONTROL bit 0x10, injecting churn
// into the lock goldens. A settled toggle bit would prove a write cycle
// finished, not that protection latched, so its outcome could never be
// reported as lock evidence anyway. No response_code write, no read, no
// completion poll, no data write.
static void eeprom28c_sdp_lock_execute(firestarter_handle_t* handle) {
    size_t sdp_seq_len = sizeof(EEPROM_SDP_ENABLE) / sizeof(EEPROM_SDP_ENABLE[0]);
    eeprom28c_emit_sdp_sequence_timed(handle, EEPROM_SDP_ENABLE, sdp_seq_len, MSG_INFO_SDP_LOCK,
                                       MSG_INFO_SDP_LOCK_DONE_US);
    delay(AT28C_TWC_MAX_MS);
}

// Six-byte SOFTWARE chip erase [Atmel AN "Software Chip Erase" Rev
// 0544B-10/98]. Drives every byte to 0xFF; the device times the cycle
// internally (t_EC), so no completion poll is permitted. SDP remains ENABLED
// afterwards -- this does not lock or unlock as a side effect.
//
// NOT the datasheet's HARDWARE chip-erase mode, which drives 12V onto OE
// (DIP28_28C256 pin 22). This handler energises no programming rail at all.
//
// Prefixed with an SDP-disable even though the application note is silent on
// whether the erase code is decoded on a protected part. The asymmetry
// justifies it: if it is not decoded, the result is a phantom erase reporting
// OK having erased nothing, and SDP state is unreadable on this family so no
// oracle could catch it afterwards. On an already-unprotected part the cost
// is six harmless writes and one t_WC wait.
//
// Device-global by construction: it ignores any sector address, and no
// post-erase blank check is wired.
static void eeprom28c_erase_execute(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHIP_ERASE);
    eeprom28c_sdp_unlock_execute(handle);
    // eeprom28c_wait_for_sdp_completion (inside the prefix above) ends in
    // reads through handle->firestarter_get_data, which leaves the data bus
    // configured as an input. Re-arm it for output before the first erase
    // write below, or every erase byte is silently dropped.
    rurp_set_data_output();
    handle->firestarter_set_data(handle, 0x5555, 0xAA);
    handle->firestarter_set_data(handle, 0x2AAA, 0x55);
    handle->firestarter_set_data(handle, 0x5555, 0x80);
    handle->firestarter_set_data(handle, 0x5555, 0xAA);
    handle->firestarter_set_data(handle, 0x2AAA, 0x55);
    handle->firestarter_set_data(handle, 0x5555, 0x10);
    delay(AT28C_TEC_MAX_MS);
}

void eeprom28c_write_init(firestarter_handle_t* handle) {
    // Check chip identity via A9-12V BEFORE SDP-disable: failing fast on
    // identity leaves the chip write-protected on mismatch.
    if (handle->chip_id > 0) {
        eeprom28c_check_chip_id(handle);
        if (handle->response_code == RESPONSE_CODE_ERROR) {
            return;
        }
    }
    if (!is_flag_set(FLAG_SKIP_SDP_UNLOCK)) {
        // Sequence length hoisted to ONE expression: the t_BLC runtime
        // budget check derives from this exact local, so there is exactly
        // one length expression in this function -- a second copy would be
        // a silent second source of truth.
        size_t sdp_seq_len = sizeof(EEPROM_SDP_DISABLE) / sizeof(EEPROM_SDP_DISABLE[0]);

        // The report pair below must stay UNCONDITIONAL (bare LOG_ID on an
        // INFO-band id, never the FLAG_VERBOSE-gated family) -- gating it
        // leaves a default `firestarter write at28c256` completely silent.
        //
        // Disable SDP before writing, routed through firestarter_set_data so
        // the full address remap and CONTROL_REGISTER rewrite apply on every
        // address change. Routing it any other way reintroduces both the
        // /WE-inhibit defect and the upper-address staleness gap.
        eeprom28c_emit_sdp_sequence_timed(handle, EEPROM_SDP_DISABLE, sdp_seq_len, MSG_INFO_SDP_UNLOCK,
                                           MSG_INFO_SDP_UNLOCK_DONE_US);

        // Wait for the SDP-disable internal write cycle to complete. There
        // is no valid form of a guarded read-back at address 0x5555
        // comparing against terminal byte 0x20 -- see
        // eeprom28c_wait_for_sdp_completion's comment above for why -- so
        // none is done here. This wait never aborts write-init and never
        // touches response_code.
        eeprom28c_wait_for_sdp_completion(handle);
    } else {
        // The skip path replaces the whole unlock block -- report pair, bracket,
        // budget check and completion wait -- with one unconditional WARN. With no
        // sequence emitted there is no internal write cycle to wait for.
        LOG_WARN_ID(MSG_WARN_SDP_UNLOCK_SKIPPED);
    }
    // No pre-write blank check on this protocol: the silicon auto-erases per page
    // during the write, and verify_page_readback checks every page afterwards. A
    // blank check here was a false precondition that made a non-blank part
    // un-writable without a flag. As of release 3.1.0 the host owns this
    // refusal instead -- do not restore a firmware-side conditional here; the
    // control flag that used to gate it is retired and gone.
    // `blank` remains available as its own step.
}

// Resolve the validated flush mask from a delivered page-size. Returns
// `requested - 1` when `requested` is a power of two in
// [1, AT28C_PAGE_SIZE_MAX], and AT28C_PAGE_SIZE_FALLBACK - 1 otherwise.
//
// Zero MUST be rejected before the subtraction -- the check below tests
// `requested == 0` first, deliberately, because the power-of-two test alone
// (`(requested & (requested - 1)) == 0`) admits 0. `0 - 1` on an unsigned
// type wraps to an all-ones mask, which would flush almost never -- the
// dangerous direction, since a page load that never flushes never gets
// read-back-verified until the very last byte.
//
// Falling back is silent by design -- the host guarantees a power-of-two
// page_size in range for every chip. The return value is only ever ANDed with
// an address, never used to index memory, so a wrong value costs flush
// granularity, not a buffer overrun.
static uint32_t eeprom28c_page_mask(uint16_t requested) {
    if (requested == 0) {
        return (uint32_t)AT28C_PAGE_SIZE_FALLBACK - 1;
    }
    if (requested <= AT28C_PAGE_SIZE_MAX && (requested & (requested - 1)) == 0) {
        return (uint32_t)requested - 1;
    }
    return (uint32_t)AT28C_PAGE_SIZE_FALLBACK - 1;
}

void eeprom28c_write_execute(firestarter_handle_t* handle) {
    // Resolved ONCE here, never per byte, and never as a runtime `%` by a
    // variable divisor -- that pulls __udivmodsi4 into a build with no flash
    // headroom. It must live here rather than in write_init: write_init
    // returns early on a chip-ID mismatch, and the native suites drive
    // operation_main directly without ever calling operation_init.
    const uint32_t page_mask = eeprom28c_page_mask(handle->page_size);
    // Read-back covers only the CURRENT flush window, not the whole physical
    // page: bytes an earlier chunk wrote are no longer in data_buffer, and
    // each chunk already verified its own. window_start <= i < data_size by
    // construction, so no index here can exceed the buffer.
    uint32_t window_start = 0;
    // Runs with pulse_delay = 0 and no inter-byte wait, under the same t_BLC
    // maximum as the SDP command sequence.
    //
    // page_load_worst_us is the interval between consecutive set_data calls as
    // measured by the MCU's own micros(). It says nothing about whether the
    // die accepted a byte, and nothing about whether t_BLC is met as seen by
    // the die -- that is not provable without a part on the bench.
    //
    // The compare below tracks a MAXIMUM; it is deliberately not a budget
    // check. A per-byte comparison against t_BLC would cost flash and cycles
    // in a hot loop on a target with hundreds of bytes of headroom.
    uint32_t page_load_worst_us = 0;
    uint32_t page_load_previous_us = micros();
    // The loop exits by break, not early return, so the worst-interval report
    // below is reached on BOTH paths. With an empty socket the very first
    // page's poll fails, and a report placed only at the normal exit would
    // emit nothing. On an aborted write the figure covers only the bytes
    // loaded before the abort.
    bool page_load_aborted = false;
    for (uint32_t i = 0; i < handle->data_size; i++) {
        uint32_t address = handle->address + i;
        uint8_t data = handle->data_buffer[i];
        handle->firestarter_set_data(handle, address, data);

        uint32_t page_load_now_us = micros();
        uint32_t page_load_interval_us = (uint32_t)(page_load_now_us - page_load_previous_us);
        if (page_load_interval_us > page_load_worst_us) {
            page_load_worst_us = page_load_interval_us;
        }
        page_load_previous_us = page_load_now_us;

        bool page_end = ((address + 1) & page_mask) == 0;
        bool last_byte = (i == handle->data_size - 1);
        if (page_end || last_byte) {
            // Two questions, two functions: wait_for_page_write answers only
            // "is the internal cycle done", verify_page_readback only "did the
            // data land". Do not merge them into one whole-byte compare.
            if (!eeprom28c_wait_for_page_write(handle, address, data)) {
                page_load_aborted = true;
                break;
            }
            if (!eeprom28c_verify_page_readback(handle, window_start, i)) {
                page_load_aborted = true;
                break;
            }
            window_start = i + 1;
        }
    }
    (void)page_load_aborted;  // recorded for reader clarity only; both exits report identically
    LOG_ID_U32(MSG_INFO_PAGE_LOAD_WORST_US, page_load_worst_us);
}

// Completion detection ONLY -- a DQ7-complement poll. Compares just that one
// bit, never the whole byte.
//
// The match must hold on TWO CONSECUTIVE reads, so a single transient sample
// landing mid-toggle cannot end the poll early.
//
// Reads go through handle->firestarter_get_data, never fu_flash_data_poll():
// that helper emits four recorded strobes per read and would inject entries
// into the compared stream.
static bool eeprom28c_wait_for_page_write(firestarter_handle_t* handle, uint32_t address, uint8_t expected) {
    uint8_t observed = 0;
    for (uint16_t j = 0; j < AT28C_PAGE_POLL_MAX_READS; j++) {
        delayMicroseconds(10);
        observed = handle->firestarter_get_data(handle, address);
        if ((observed & AT28C_DQ7_MASK) == (expected & AT28C_DQ7_MASK)) {
            uint8_t confirm = handle->firestarter_get_data(handle, address);
            if ((confirm & AT28C_DQ7_MASK) == (expected & AT28C_DQ7_MASK)) {
                return true;
            }
            observed = confirm;
        }
    }
    {
        uint8_t _b[5];
        _b[0] = (uint8_t)((address >> 16) & 0xFF);
        _b[1] = (uint8_t)((address >> 8) & 0xFF);
        _b[2] = (uint8_t)(address & 0xFF);
        _b[3] = (uint8_t)expected;
        _b[4] = (uint8_t)observed;
        LOG_ERROR_ID_BYTES(MSG_ERR_EEPROM_TIMEOUT, _b, 5);
    }
    handle->response_code = RESPONSE_CODE_ERROR;
    return false;
}

// Data-landed proof ONLY -- a per-byte read-back over the current flush
// window, reusing memory_verify_execute's mismatch payload order: expected,
// observed, addr>>16, addr>>8, addr, where addr is the FAILING address. Says
// nothing about whether the internal cycle finished -- that is
// wait_for_page_write's job, and keeping the two separate is what stops a
// whole-byte compare from passing spuriously on unchanged bytes.
static bool eeprom28c_verify_page_readback(firestarter_handle_t* handle, uint32_t first_index, uint32_t last_index) {
    for (uint32_t k = first_index; k <= last_index; k++) {
        uint8_t expected = (uint8_t)handle->data_buffer[k];
        uint32_t addr = handle->address + k;
        uint8_t observed = handle->firestarter_get_data(handle, addr);
        if (observed != expected) {
            {
                uint8_t _b[5];
                _b[0] = expected;
                _b[1] = observed;
                _b[2] = (uint8_t)((addr >> 16) & 0xFF);
                _b[3] = (uint8_t)((addr >> 8) & 0xFF);
                _b[4] = (uint8_t)(addr & 0xFF);
                LOG_ERROR_ID_BYTES(MSG_ERR_VERIFY, _b, 5);
            }
            handle->response_code = RESPONSE_CODE_ERROR;
            return false;
        }
    }
    return true;
}
