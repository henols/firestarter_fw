/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __OPERATION_UTILS_H__
#define __OPERATION_UTILS_H__

/* INIT-MAIN-END state machine, ACK/DONE/DATA message passing and state
 * management for multi-step programmer operations. */
#include "firestarter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INIT 1
#define MAIN 3
#define END 5
#define ENDED 6

#define OPERATION_IN_PROGRESS 0x40
#define OPERATION_WAITING_FOR_DATA 0x80

enum op_message_type {
    OP_MSG_ACK,
    OP_MSG_DONE,
    OP_MSG_DATA,
    OP_MSG_INCOMPLETE,
    OP_MSG_ERROR
};


static inline void set_operation_in_progress(firestarter_handle_t* handle) {
    handle->operation_state |= OPERATION_IN_PROGRESS;
}

static inline void clear_operation_in_progress(firestarter_handle_t* handle) {
    handle->operation_state &= ~OPERATION_IN_PROGRESS;
}

static inline bool is_operation_in_progress(const firestarter_handle_t* handle) {
    return (handle->operation_state & OPERATION_IN_PROGRESS) == OPERATION_IN_PROGRESS;
}

static inline void set_operation_waiting_for_data(firestarter_handle_t* handle) {
    handle->operation_state |= OPERATION_WAITING_FOR_DATA;
}

static inline void clear_operation_waiting_for_data(firestarter_handle_t* handle) {
    handle->operation_state &= ~OPERATION_WAITING_FOR_DATA;
}

static inline bool is_operation_waiting_for_data(const firestarter_handle_t* handle) {
    return (handle->operation_state & OPERATION_WAITING_FOR_DATA) == OPERATION_WAITING_FOR_DATA;
}

/* For operations with a main body but no host data exchange (blank check, chip
 * ID). Returns true only when FULLY completed; false while still in progress. */
bool op_execute_simple_operation(firestarter_handle_t* handle);

/* The engine: manages the INIT, MAIN and END phases and calls `callback` for
 * the MAIN phase. Returns true when FULLY completed, false while in progress. */
bool op_execute_stateful_operation(bool (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle);

/* Runs `callback` in programmer mode, restoring communication mode afterwards.
 * Returns false only on RESPONSE_CODE_ERROR -- a WARNING is still true. */
bool op_execute_function(void (*callback)(firestarter_handle_t* handle), firestarter_handle_t* handle);

void op_reset_timeout();

/**
 * @brief Parses the incoming serial stream for messages from the host.
 *
 * This function is non-blocking. It checks for "OK" (ACK), "DONE", and data packets ('#').
 *
 * @param handle Pointer to the firestarter handle, used to store incoming data.
 * @return An op_message_type enum value indicating the message found, or OP_MSG_INCOMPLETE if no full message is available.
 */
op_message_type op_get_message(firestarter_handle_t* handle);

/**
 * @brief Waits for an "OK" (ACK) message from the host.
 *
 * This is a blocking function with a timeout.
 *
 * @param handle Pointer to the firestarter handle.
 * @return true if an ACK was received, false on timeout or error.
 */
bool op_wait_for_ack(firestarter_handle_t* handle);

/**
 * @brief Marks the MAIN phase of an operation as complete.
 *
 * This function sends the "MAIN: Done" message to the host and advances the internal state machine.
 *
 * @param handle Pointer to the firestarter handle.
 */
void set_operation_to_done(firestarter_handle_t* handle) ;

#ifdef __cplusplus
}
#endif

#endif  // __OPERATION_UTILS_H__