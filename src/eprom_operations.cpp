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

bool eprom_erase(firestarter_handle_t* handle) {
    LOG_DEBUG_ID_SUB(DBG_ERASE_PROM);
    if (!is_flag_set(FLAG_CAN_ERASE)) {
        LOG_ERROR_ID(MSG_ERR_NOT_SUPPORTED);
        handle->response_code = RESPONSE_CODE_ERROR;
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

// Returns true on success/continue, false on error.
static inline bool _process_incoming_data(firestarter_handle_t* handle) {
    // The operation's exclusive end bounds each received chunk; it defaults
    // to the device size when no region is supplied.
    const uint32_t op_end = mem_util_operation_end(handle);

    // Transfer is pull-based: request the next chunk only when ready.
    if (handle->address > op_end) {
        LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
        handle->response_code = RESPONSE_CODE_ERROR;
        return false;
    }
    if (handle->address == op_end) {
        set_operation_to_done(handle);
        return true;
    }

    // 1. Check for an incoming message from the host first. This prevents a race
    // condition where the firmware requests data after the host has already sent "DONE".
    op_message_type msg_type = op_get_message(handle);

    if (msg_type == OP_MSG_INCOMPLETE) {
        if (!is_operation_waiting_for_data(handle)) {
            // Leonardo also emits per-byte EPROM progress; on Uno-class
            // boards these chunk exchanges are the only progress source.
            LOG_OK_ID(MSG_OK_REQ_DATA);
            set_operation_waiting_for_data(handle);
        }
        return true;
    }

    clear_operation_waiting_for_data(handle);

    switch (msg_type) {
        case OP_MSG_DONE:
            // Without a region-end the host may end the write at any cursor.
            // With one, the write is complete only at op_end, and the check
            // above finishes it there before any message is read. A DONE
            // before op_end means the host sent less data than it declared.
            if (handle->region_end != 0) {
                LOG_ERROR_ID(MSG_ERR_CMD);
                handle->response_code = RESPONSE_CODE_ERROR;
                return false;
            }
            set_operation_to_done(handle);
            return true;
        case OP_MSG_DATA:
            if (handle->data_size > DATA_BUFFER_SIZE ||
                !mem_util_range_fits(handle->address, handle->data_size, op_end)) {
                LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
                handle->response_code = RESPONSE_CODE_ERROR;
                return false;
            }
            break;
        default:
            // op_get_message already logged a data error (OP_MSG_ERROR). Any
            // other message here, for example a stray ACK, is a host
            // protocol error: refuse it with an error frame, so the host
            // does not wait for a timeout.
            if (msg_type != OP_MSG_ERROR) {
                LOG_ERROR_ID(MSG_ERR_CMD);
            }
            handle->response_code = RESPONSE_CODE_ERROR;
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
    // A read ends at region-end when the command has one, else at mem_size.
    const uint32_t op_end = mem_util_operation_end(handle);
    if (handle->address > op_end) {
        LOG_ERROR_ID(MSG_ERR_OUT_OF_RANGE);
        handle->response_code = RESPONSE_CODE_ERROR;
        return false;
    }
    if (handle->address == op_end) {
        set_operation_to_done(handle);
        return true;
    }
    if (!op_execute_function(handle->firestarter_operation_main, handle)) {
        return false;  // Error, so finished.
    }

    // The read handler fills up to a whole buffer within mem_size. Cut the
    // chunk at the end of the operation's region.
    if (handle->data_size > op_end - handle->address) {
        handle->data_size = op_end - handle->address;
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
    if (handle->address >= op_end) {
        set_operation_to_done(handle);
    }
    return true;
}
