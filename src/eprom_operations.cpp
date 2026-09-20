/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "eprom_operations.h"

#include "firestarter.h"
#include "logging_id.h"
#include "memory_utils.h"
#include "messages.h"
#include "operation_utils.h"
#include "rurp_shield.h"

static inline bool _process_incoming_data(firestarter_handle_t* handle);
static inline bool _process_outgoing_data(firestarter_handle_t* handle);

bool eprom_read(firestarter_handle_t* handle) {
    return op_execute_stateful_operation(_process_outgoing_data, handle);
}

bool eprom_write(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_WRITE_EPROM);
    return op_execute_stateful_operation(_process_incoming_data, handle);
}

// Return true if the operation is done, otherwise false
bool eprom_verify(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_VERIFY_PROM);
    return op_execute_stateful_operation(_process_incoming_data, handle);
}

bool eprom_erase(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_ERASE_PROM);
    if (!is_flag_set(FLAG_CAN_ERASE)) {
        LOG_ERROR_ID(MSG_ERR_NOT_SUPPORTED);
        return true;
    }
    return op_execute_simple_operation(handle);
}

bool eprom_check_chip_id(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_CHECK_CHIP_ID_OP);
    if (handle->chip_id == 0) {
        LOG_ERROR_ID(MSG_ERR_NO_CHIP_ID);
        return true;
    }
    return op_execute_simple_operation(handle);
}

bool eprom_blank_check(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_BLANK_CHECK_PROM);
    return op_execute_simple_operation(handle);
}

// Standalone entry points for CMD_SDP_UNLOCK / CMD_SDP_LOCK
// (0x0D-only ops; configure_eeprom28c sets firestarter_operation_main to
// eeprom28c_sdp_unlock_execute / eeprom28c_sdp_lock_execute). Deliberately no
// LOG_DEBUG_ID_SUB line here -- eprom_read has none either, and adding one
// would require a new DBG_* catalog id this phase does not need.
// Deliberately no precondition check -- eprom_erase's FLAG_CAN_ERASE test has
// no SDP analogue; the op-layer NULL-main guard is what
// refuses these commands on a protocol whose handler set no main.
bool eprom_sdp_unlock(firestarter_handle_t* handle) {
    return op_execute_simple_operation(handle);
}

bool eprom_sdp_lock(firestarter_handle_t* handle) {
    return op_execute_simple_operation(handle);
}

// CMD_LOCK_STATUS entry point, in eprom_blank_check's
// single-step shape above -- but deliberately WITHOUT a LOG_DEBUG_ID_SUB
// line. CMD_LOCK_STATUS (16) is numerically greater
// than CMD_READ_VPP (11), so it falls outside the second, independent
// diagnostic-ordinal range in firestarter.cpp, and this command emits none
// of that range's three DBG_* lines.
// Adding a DBG_* call here would need a new [debug] catalog entry that is
// deliberately not minted -- a chosen consequence, not an oversight.
bool eprom_lock_status(firestarter_handle_t* handle) {
    return op_execute_simple_operation(handle);
}

// Returns true on success/continue, false on error.
static inline bool _process_incoming_data(firestarter_handle_t* handle) {
    // op_end is the operation's end, not the device's end: it equals
    // handle->mem_size whenever no region was supplied (D-04's
    // absent-semantics), and the same local bounds both eprom_write and
    // eprom_verify because they share this function (D-07). Under D-05
    // host and firmware agree on the region by construction, so the
    // out-of-range refusal below becomes a fail-closed guard against a
    // host/file mismatch rather than a live path.
    const uint32_t op_end = mem_util_operation_end(handle);

    // The operation is "pull" based. The firmware requests a data chunk when it's ready.
    // This provides software flow control and allows for larger data chunks, improving speed.
    // We use a state flag to track if we are waiting for data.
    if (handle->address >= op_end) {
        set_operation_to_done(handle);
        return true;
    }

    // 1. Check for an incoming message from the host first. This prevents a race
    // condition where the firmware requests data after the host has already sent "DONE".
    op_message_type msg_type = op_get_message(handle);

    if (msg_type == OP_MSG_INCOMPLETE) {
        // No message from host. If we are not already waiting for data, request it.
        if (!is_operation_waiting_for_data(handle)) {
            // On leonardo the firmware emits MSG_DATA_PROGRESS from inside the
            // per-byte program loop (src/proms/eprom.cpp), so the host's
            // progress bar can reflect bytes actually programmed rather than
            // only bytes handed to the firmware at each chunk request below.
            // On SERIAL_ON_IO targets (uno/uno328pb) that emission is compiled
            // out, so the host's own chunk-handoff bar -- driven by this
            // MSG_OK_REQ_DATA request/response cadence -- remains the only
            // progress source there. The non-claim, restated: intra-block
            // write progress is emitted on the EPROM path only, and delivered
            // on leonardo only.
            LOG_OK_ID(MSG_OK_REQ_DATA);
            set_operation_waiting_for_data(handle);
        }
        return true;  // Continue waiting.
    }

    // We have received a message. Clear the flag so we can request the next chunk.
    clear_operation_waiting_for_data(handle);

    switch (msg_type) {
        case OP_MSG_DONE:
            // The host has signaled it has no more data to send.
            set_operation_to_done(handle);
            return true;
        case OP_MSG_DATA:
            // The host sent a data packet.
            if (handle->address + handle->data_size > op_end) {
                LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
                return false;
            }
            break;
        default:
            // An error occurred in op_get_message or an unexpected message was received.
            return false;
    }

    if (!op_execute_function(handle->firestarter_operation_main, handle)) {
        return false;
    }

    handle->address += handle->data_size;
    return true;
}

// Returns true on success/continue, false on error.
static inline bool _process_outgoing_data(firestarter_handle_t* handle) {
    if (!op_execute_function(handle->firestarter_operation_main, handle)) {
        return false;  // Error, so finished.
    }

    // Wrap the raw chip-byte chunk in a MSG_DATA_CHUNK ID frame.
    // MSG_DATA_SENDING signals the start of the batch (host may see this before
    // the first chunk). Then the actual data follows as a framed MSG_DATA_CHUNK.
    LOG_DATA_ID(MSG_DATA_SENDING);
    rurp_log_id_wide(MSG_DATA_CHUNK,
                     (const uint8_t*)handle->data_buffer,
                     (uint16_t)handle->data_size);

    if (!op_wait_for_ack(handle)) {
        return false;
    }

    handle->address += handle->data_size;
    if (handle->address >= handle->mem_size) {
        set_operation_to_done(handle);
    }
    return true;
}
