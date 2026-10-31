/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Checks that configure_memory() dispatches a handle to one named
 * configure_<family>() function: the operation pointers that configure_memory
 * sets must be the pointers that the family function sets on a fresh copy of
 * the same handle.
 */

#pragma once

#include <stdio.h>
#include <unity.h>

#include "firestarter.h"
#include "memory.h"

/* configure_memory() sets these generic operations before it calls the family
 * function. They have no header, because only memory.cpp uses them. */
void memory_read_execute(firestarter_handle_t* handle);
void memory_write_execute(firestarter_handle_t* handle);
void memory_set_control_register(firestarter_handle_t* handle, rurp_register_t bit, bool state);

typedef void (*configure_family_fn_t)(firestarter_handle_t*);

/* Asserts that configure_memory(base) sets the same three operation pointers
 * as `family` on a fresh copy of base, and that dispatch does not refuse. The
 * family function runs first: configure_eprom() keeps the control-register
 * setter of the handle it last saw in a file-scope static, so configure_memory
 * must be the last call. */
static inline void assert_dispatches_to(const firestarter_handle_t& base, configure_family_fn_t family,
                                        const char* ctx) {
    char msg[192];

    firestarter_handle_t direct = base;
    direct.firestarter_operation_init = NULL;
    direct.firestarter_operation_main = NULL;
    direct.firestarter_operation_end = NULL;
    direct.firestarter_set_control_register = memory_set_control_register;
    if (base.cmd == CMD_READ) {
        direct.firestarter_operation_main = memory_read_execute;
    } else if (base.cmd == CMD_WRITE) {
        direct.firestarter_operation_main = memory_write_execute;
    }
    family(&direct);

    firestarter_handle_t dispatched = base;
    configure_memory(&dispatched);

    snprintf(msg, sizeof(msg), "%s: configure_memory must not refuse", ctx);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, dispatched.response_code, msg);
    snprintf(msg, sizeof(msg), "%s: operation_init", ctx);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)direct.firestarter_operation_init,
                                  (const void*)dispatched.firestarter_operation_init, msg);
    snprintf(msg, sizeof(msg), "%s: operation_main", ctx);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)direct.firestarter_operation_main,
                                  (const void*)dispatched.firestarter_operation_main, msg);
    snprintf(msg, sizeof(msg), "%s: operation_end", ctx);
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const void*)direct.firestarter_operation_end,
                                  (const void*)dispatched.firestarter_operation_end, msg);
}
