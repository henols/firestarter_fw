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
#include "logging.h"
#include "rurp_shield.h"
#ifndef DISABLE_BINARY_PROTOCOL
#include "binary_protocol.h"
#else
// Stub declarations for binary protocol when disabled
bool use_binary_protocol = false;
void send_binary_response(uint8_t code, const char* msg) { (void)code; (void)msg; }
void send_binary_response_with_data(uint8_t code, const char* buffer, size_t size) { (void)code; (void)buffer; (void)size; }
#endif

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

/**
 * @brief Executes a simple, non-stateful operation.
 *
 * This is a wrapper around op_execute_stateful_operation for operations that are
 * expected to complete within a single logical step (e.g., blank check). It uses
 * a callback that executes the main operation logic and marks the operation as
 * done immediately after.
 *
 * @param handle Pointer to the firestarter handle.
 * @return true if the operation is still ongoing (e.g., waiting for ACKs), false when fully completed.
 */
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
                return true;  // Not finished yet, waiting for final ACK
            }
            return false;  // Received final ACK (or junk), command is finished.
        }

        int res = _execute_operation_house_keeping(handle);
        if (res != CONTINUE) {
            return res == RETURN;
        }
        // log_info_format("Operation started: %d, state: %d", is_operation_started(OPERATION), handle->operation_state);
        if (is_operation_started(MAIN)) {
            // log_info_const("Execute operation");
            return callback(handle);
        }
        return true;
    }
    return false;
}

/**
 * @brief Executes a provided function with standard operation wrappers.
 *
 * This function wraps the execution of a given callback. It sets the programmer mode,
 * executes the callback, resets the communication mode, resets the timeout, and
 * checks the response from the handle.
 *
 * @param callback The function to execute.
 * @param handle Pointer to the firestarter handle.
 * @return true if the operation was successful or is waiting for data, false on error.
 */
bool op_execute_function(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle) {
    if (callback != NULL) {
        return _execute_operation(callback, handle) != ERROR;
    }
    return false;
}

bool op_wait_for_ack(firestarter_handle_t* handle) {
    unsigned long timeout = millis() + 1000;
    while (millis() < timeout) {
        op_message_type msg_type = op_get_message(handle);
        if (msg_type == OP_MSG_ACK) {
            return true;
        }
        if (msg_type == OP_MSG_ERROR) {
            return false;
        }
        delay(10);
    }
    log_error_const("Timeout");
    return false;
}

/**
 * @brief Parses the incoming serial stream for messages from the host.
 *
 * This function is non-blocking. It checks for "OK" (ACK), "DONE", and data packets ('#').
 * It consumes junk characters until a valid message start is found.
 * @param handle Pointer to the firestarter handle, used to store incoming data.
 * @return An op_message_type enum value indicating the message found, or OP_MSG_INCOMPLETE if no full message is available.
 */
op_message_type op_get_message(firestarter_handle_t* handle) {
    if (rurp_communication_available() <= 0) {
        return OP_MSG_INCOMPLETE;
    }
    // log_info_format("op_get_message: %d bytes available", rurp_communication_available());
    while (rurp_communication_available() > 0) {
        int peek = rurp_communication_peak();
        switch (peek) {
            case 'O':  // Potential "OK"
                // log_info_const("op_get_message: Found 'O'");
                if (rurp_communication_available() < 2) {
                    return OP_MSG_INCOMPLETE;
                }
                rurp_communication_read();  // consume 'O'
                if (rurp_communication_peak() == 'K') {
                    rurp_communication_read();  // consume 'K'
                    // log_info_const("op_get_message: ACK received");
                    return OP_MSG_ACK;
                }
                // Not "OK", 'O' is consumed. Loop will treat next char as junk.
                break;

            case 'D':  // Potential "DONE"
                if (rurp_communication_available() < 4) {
                    return OP_MSG_INCOMPLETE;
                }
                char done_buf[4];
                rurp_communication_read_bytes(done_buf, 4);
                if (strncmp_P(done_buf, PSTR("DONE"), 4) == 0) {
                    return OP_MSG_DONE;
                }
                // Not "DONE", 4 bytes consumed. This is a risk.
                break;

            case '#': {  // Data packet
                if (rurp_communication_available() < 4) {
                    return OP_MSG_INCOMPLETE;
                }
                rurp_communication_read();  // consume '#'
                int res = rurp_communication_read_data(handle->data_buffer);
                if (res < 0) {
                    log_error_P_int("Data err: ", res);
                    return OP_MSG_ERROR;
                }
                handle->data_size = res;
                return OP_MSG_DATA;
            } 
            default:
                // log_info_format("op_get_message: Consuming junk char '%c' (0x%02x)", (char)peek, peek);
                rurp_communication_read();
                break;
        }
    }
    return OP_MSG_INCOMPLETE;  // Nothing in buffer
}

void set_operation_to_done(firestarter_handle_t* handle) {
    log_info_const("Main done");
    set_operation_state_done();
    send_main_done();
}

/**
 * @brief Manages the state transitions of an operation (INIT, MAIN, END).
 *
 * This function orchestrates the execution of the init and end phases of an operation,
 * ensuring they run in the correct order around the main operation logic.
 *
 * @param handle Pointer to the firestarter handle.
 * @return CONTINUE if the state machine should proceed, RETURN or ERROR to stop.
 */
static inline int _execute_operation_house_keeping(firestarter_handle_t* handle) {
    // log_info_format("Housekeeping: state=0x%02x", handle->operation_state);
    if (is_all_operations_done()) {
        // log_info_const("Operations done");
        return CONTINUE;
    }
    if (is_operation_started(MAIN)) {
        // log_info_const("Operation is started");
        return CONTINUE;
    }
    // log_info_format("Can operation start: %d", can_operation_start(OPERATION));
    if (can_operation_start(MAIN)) {
        if (!op_wait_for_ack(handle)) {
            return ERROR;
        }
        op_reset_timeout();
        set_operation_state(MAIN);
        log_info_const("Main start");
        return CONTINUE;
    }
    int res = _execute_operation_house_keeping_func(handle->firestarter_operation_init, INIT, handle);
    if (res != CONTINUE) {
        return res;
    }

    return _execute_operation_house_keeping_func(handle->firestarter_operation_end, END, handle);
}

/**
 * @brief Executes a specific housekeeping phase (INIT or END) for an operation.
 *
 * This function handles the execution loop for a specific state (INIT or END).
 * It waits for an ACK, runs the provided callback, and sends a completion message.
 *
 * @param callback The function to call (e.g., firestarter_operation_init).
 * @param state The operation state to manage (e.g., INIT).
 * @param handle Pointer to the firestarter handle.
 * @return CONTINUE if the phase is complete, RETURN or ERROR to stop.
 */
static inline int _execute_operation_house_keeping_func(void (*callback)(firestarter_handle_t* handle), int state, firestarter_handle_t* handle) {
    // log_info_format("%s function, state: %d, current state: %d", name, state, handle->operation_state);
    if (execute_operation_state(state)) {
        // log_info_format("%s function, state: %d, can start: %d", name, state, can_operation_start(state));
        if (can_operation_start(state)) {
            // log_info_format("Waiting for ACK to start %s phase", name);
            if (!op_wait_for_ack(handle)) {
                return ERROR;
            }
            if (state == INIT) {
                log_info_const("Init start");
            } else {
                log_info_const("End start");
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
            send_init_done();
        } else {
            send_end_done();
        }
        set_operation_state_done();
    }
    return CONTINUE;
}

/**
 * @brief A callback for simple operations that completes in one step.
 *
 * This function is used as a callback for op_execute_simple_operation.
 * It calls the main operation function and, if successful, immediately marks the operation as done.
 *
 * @param handle Pointer to the firestarter handle.
 * @return true on success, false on error.
 */
static inline bool _single_step_operation_callback(firestarter_handle_t* handle) {
    int res = _execute_operation(handle->firestarter_operation_main, handle);
    if (res == ERROR) {
        return false;
    }
    if (!is_operation_in_progress(handle)) {
        set_operation_to_done(handle);
    }
    return true;  // Success,
}

/**
 * @brief Executes a callback function, wrapping it with mode changes and response checking.
 *
 * @param callback The function to execute.
 * @param handle Pointer to the firestarter handle, which will be passed to the callback.
 * @return true if the operation was successful, false on error.
 */
static inline int _execute_operation(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle) {
    if (callback != NULL) {
        handle->response_msg[0] = '\0';
        rurp_set_programmer_mode();
        callback(handle);
        rurp_set_communication_mode();
        return _check_response(handle) ? RETURN : ERROR;
    }
    return CONTINUE;
}

/**
 * @brief Checks and handles the response code in the firestarter handle.
 *
 * Logs messages based on the response code and sends data if required.
 *
 * @param handle Pointer to the firestarter handle.
 * @return true if the response is OK, WARNING, or DATA. false if it's an ERROR.
 */
static inline bool _check_response(firestarter_handle_t* handle) {
#ifndef DISABLE_BINARY_PROTOCOL
    extern bool use_binary_protocol;
    
    if (use_binary_protocol) {
        // Handle binary protocol responses
        switch (handle->response_code) {
            case RESPONSE_CODE_OK:
                send_binary_response(RESPONSE_CODE_OK, handle->response_msg);
                break;
            case RESPONSE_CODE_WARNING:
                send_binary_response(RESPONSE_CODE_WARNING, handle->response_msg);
                break;
            case RESPONSE_CODE_DATA:
                send_binary_response_with_data(RESPONSE_CODE_DATA, handle->data_buffer, handle->data_size);
                break;
            case RESPONSE_CODE_ERROR:
            default:
                send_binary_response(RESPONSE_CODE_ERROR, handle->response_msg);
                op_reset_timeout();
                handle->response_code = RESPONSE_CODE_OK;
                return false;
        }
    } else 
#endif
    {
        // Handle JSON protocol responses
        switch (handle->response_code) {
            case RESPONSE_CODE_OK:
                log_info(handle->response_msg);
                break;
            case RESPONSE_CODE_WARNING:
                log_warn(handle->response_msg);
                break;
            case RESPONSE_CODE_DATA:
                log_data(handle->response_msg);
                rurp_communication_write(handle->data_buffer, handle->data_size);
                break;
            case RESPONSE_CODE_ERROR:
            default:
                log_error(handle->response_msg);
                op_reset_timeout();
                handle->response_code = RESPONSE_CODE_OK;
                return false;
        }
    }
    op_reset_timeout();
    handle->response_code = RESPONSE_CODE_OK;
    return true;
}
