/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "operation_utils.h"

#include <Arduino.h>
#include <stdlib.h>

#include "firestarter.h"
#include "logging_id.h"
#include "messages.h"
#include "rurp_shield.h"
#include "timeout_utils.h"

#define ERROR -1
#define RETURN 0
#define CONTINUE 1

#define set_operation_state(state) \
    (handle->operation_state = (handle->operation_state & 0xc0) | state)

// Preserve flags (top 2 bits), increment state (lower 6 bits)
#define set_operation_state_done() \
    handle->operation_state = (handle->operation_state & 0xC0) | ((handle->operation_state & 0x3F) + 1)

// Check state (lower 6 bits), ignore flags (top 2 bits)
#define is_operation_started(state) \
    ((handle->operation_state & 0x3F) == state)

#define can_operation_start(state) \
    is_operation_started(state - 1)

#define execute_operation_state(state) \
    (can_operation_start(state) || is_operation_started(state))

#define is_all_operations_done() \
    is_operation_started(ENDED)

static inline bool _check_response(firestarter_handle_t* handle);
static inline int _execute_operation(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle);
static inline int _execute_operation_house_keeping(firestarter_handle_t* handle);
static inline int _execute_operation_house_keeping_func(void (*callback)(firestarter_handle_t* handle), int state, firestarter_handle_t* handle);
static inline bool _single_step_operation_callback(firestarter_handle_t* handle);

/* Returns true only when fully completed; false while still in progress
 * (e.g. waiting for ACKs). */
bool op_execute_simple_operation(firestarter_handle_t* handle) {
    return op_execute_stateful_operation(_single_step_operation_callback, handle);
}

bool op_execute_stateful_operation(bool (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle) {
    if (handle->firestarter_operation_main) {
        if (is_all_operations_done()) {
            // The operation is complete from the firmware's perspective.
            // The host sends a final ACK to close the transaction.
            // We wait for it and then signal that the command is finished.
            if (op_get_message(handle) == OP_MSG_INCOMPLETE) {
                return false;  // Not finished yet, waiting for final ACK
            }
            return true;  // Received final ACK (or junk), command is finished.
        }

        int res = _execute_operation_house_keeping(handle);
        if (res != CONTINUE) {
            return res != RETURN;
        }
        if (is_operation_started(MAIN)) {
            // Deliberate negation: the callback's convention is
            // true-on-success, the engine's is true-on-finished. This is the
            // only site where the two meet.
            return !callback(handle);
        }
        return false;
    }
    // No main phase installed for this (command, protocol): refuse rather
    // than fall through, which used to report OK having done nothing.
    // Returning true means FINISHED, not successful — response_code carries
    // the outcome, and command_done() still runs.
    LOG_ERROR_ID(MSG_ERR_NOT_SUPPORTED);
    handle->response_code = RESPONSE_CODE_ERROR;
    return true;
}

bool op_execute_function(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle) {
    if (callback != NULL) {
        return _execute_operation(callback, handle) != ERROR;
    }
    return false;
}

// The read code of the last '#' frame that failed its check.
static int16_t last_bad_data = 0;

void op_log_bad_data(void) {
    LOG_ERROR_ID_U16(MSG_ERR_DATA_ERR_N, (uint16_t)last_bad_data);
}

// Returns OP_MSG_ACK, OP_MSG_NAK (only when nak_allowed), or OP_MSG_ERROR
// after it sends the error frame. An "NK" that is not allowed is ignored.
static op_message_type _wait_for_reply(firestarter_handle_t* handle, bool nak_allowed) {
    const uint32_t started_ms = (uint32_t)millis();
    while (!fs_timeout_expired(started_ms, 1000U, (uint32_t)millis())) {
        op_message_type msg_type = op_get_message(handle);
        if (msg_type == OP_MSG_ACK || (msg_type == OP_MSG_NAK && nak_allowed)) {
            return msg_type;
        }
        if (msg_type == OP_MSG_BAD_DATA) {
            op_log_bad_data();
            return OP_MSG_ERROR;
        }
        if (msg_type == OP_MSG_DONE) {
            // The host ended the command where an ACK was expected. Report
            // it at once instead of waiting for the ACK timeout.
            LOG_ERROR_ID(MSG_ERR_CMD);
            return OP_MSG_ERROR;
        }
        delay(10);
    }
    LOG_ERROR_ID(MSG_ERR_TIMEOUT);
    return OP_MSG_ERROR;
}

bool op_wait_for_ack(firestarter_handle_t* handle) {
    return _wait_for_reply(handle, false) == OP_MSG_ACK;
}

op_message_type op_wait_for_ack_or_nak(firestarter_handle_t* handle) {
    return _wait_for_reply(handle, true);
}

/* Non-blocking. Consumes junk until a valid message start is found; returns
 * OP_MSG_INCOMPLETE when no complete message is available yet. */
op_message_type op_get_message(firestarter_handle_t* handle) {
    if (rurp_communication_available() <= 0) {
        return OP_MSG_INCOMPLETE;
    }
    while (rurp_communication_available() > 0) {
        int peek = rurp_communication_peak();
        switch (peek) {
            case 'O':  // Potential "OK"
                if (rurp_communication_available() < 2) {
                    return OP_MSG_INCOMPLETE;
                }
                rurp_communication_read();  // consume 'O'
                if (rurp_communication_peak() == 'K') {
                    rurp_communication_read();  // consume 'K'
                    return OP_MSG_ACK;
                }
                // Not "OK", 'O' is consumed. Loop will treat next char as junk.
                break;

            case 'N':  // Potential "NK"
                if (rurp_communication_available() < 2) {
                    return OP_MSG_INCOMPLETE;
                }
                rurp_communication_read();  // consume 'N'
                if (rurp_communication_peak() == 'K') {
                    rurp_communication_read();  // consume 'K'
                    return OP_MSG_NAK;
                }
                break;

            case 'D': {  // Potential "DONE"
                if (rurp_communication_available() < 4) {
                    return OP_MSG_INCOMPLETE;
                }
                // Consume a byte only while it continues "DONE". The first
                // byte that does not match stays in the buffer, so a '#'
                // data frame or an "OK" after a stray 'D' is still read.
                static const char done[] PROGMEM = "DONE";
                uint8_t matched = 0;
                while (matched < 4 &&
                       rurp_communication_peak() == (int)pgm_read_byte(&done[matched])) {
                    rurp_communication_read();
                    matched++;
                }
                if (matched == 4) {
                    return OP_MSG_DONE;
                }
                break;
            }

            case '#': {  // Data packet
                rurp_communication_read();  // consume '#'
                int res = rurp_communication_read_data(handle->data_buffer, DATA_BUFFER_SIZE);
                if (res < 0) {
                    last_bad_data = (int16_t)res;
                    return OP_MSG_BAD_DATA;
                }
                handle->data_size = res;
                return OP_MSG_DATA;
            }
            default:
                rurp_communication_read();
                break;
        }
    }
    return OP_MSG_INCOMPLETE;  // Nothing in buffer
}

void set_operation_to_done(firestarter_handle_t* handle) {
    LOG_INFO_ID(MSG_INFO_MAIN_DONE);
    set_operation_state_done();
    LOG_MAIN_ID(MSG_MAIN_DONE);
}

static inline int _execute_operation_house_keeping(firestarter_handle_t* handle) {
    if (is_all_operations_done()) {
        return CONTINUE;
    }
    if (is_operation_started(MAIN)) {
        return CONTINUE;
    }
    if (can_operation_start(MAIN)) {
        if (!op_wait_for_ack(handle)) {
            return ERROR;
        }
        op_reset_timeout();
        set_operation_state(MAIN);
        LOG_INFO_ID(MSG_INFO_MAIN_START);
        return CONTINUE;
    }
    int res = _execute_operation_house_keeping_func(handle->firestarter_operation_init, INIT, handle);
    if (res != CONTINUE) {
        return res;
    }

    return _execute_operation_house_keeping_func(handle->firestarter_operation_end, END, handle);
}

static inline int _execute_operation_house_keeping_func(void (*callback)(firestarter_handle_t* handle), int state, firestarter_handle_t* handle) {
    if (execute_operation_state(state)) {
        if (can_operation_start(state)) {
            if (!op_wait_for_ack(handle)) {
                return ERROR;
            }
            if (state == INIT) {
                LOG_INFO_ID(MSG_INFO_INIT_START);
            } else {
                LOG_INFO_ID(MSG_INFO_END_START);
            }
            set_operation_state(state);
        }

        if (_execute_operation(callback, handle) == ERROR) {
            return ERROR;
        }

        if (is_operation_in_progress(handle)) {
            return RETURN;
        }

        if (state == INIT) {
            LOG_INIT_ID(MSG_INIT_DONE);
        } else {
            LOG_END_ID(MSG_END_DONE);
        }
        set_operation_state_done();
    }
    return CONTINUE;
}

static inline bool _single_step_operation_callback(firestarter_handle_t* handle) {
    int res = _execute_operation(handle->firestarter_operation_main, handle);
    if (res == ERROR) {
        return false;
    }
    if (!is_operation_in_progress(handle)) {
        set_operation_to_done(handle);
    }
    return true;
}

static inline int _execute_operation(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle) {
    if (callback != NULL) {
        rurp_set_programmer_mode();
        callback(handle);
        rurp_set_communication_mode();
        return _check_response(handle) ? RETURN : ERROR;
    }
    return CONTINUE;
}

static inline bool _check_response(firestarter_handle_t* handle) {
    switch (handle->response_code) {
        case RESPONSE_CODE_OK:
            break;
        case RESPONSE_CODE_WARNING:
            break;
        case RESPONSE_CODE_DATA:
            rurp_communication_write(handle->data_buffer, handle->data_size);
            break;
        case RESPONSE_CODE_ERROR:
        default:
            return false;
    }
    op_reset_timeout();
    handle->response_code = RESPONSE_CODE_OK;
    return true;
}
