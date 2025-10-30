/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>

#include <Arduino.h>  // For Serial and F() macro

#include "firestarter.h"
#include "rurp_shield.h"

// Forward declaration for binary protocol support
extern bool use_binary_protocol;

// Forward declarations for binary protocol functions
void send_binary_response(uint8_t status, const char* message);
void send_binary_response_format(uint8_t status, const char* format, ...);

// Declare logging type strings defined in logging.c
extern const char LOG_OK_MSG[] PROGMEM;
extern const char LOG_INIT_DONE_MSG[] PROGMEM;
extern const char LOG_MAIN_DONE_MSG[] PROGMEM;
extern const char LOG_END_DONE_MSG[] PROGMEM;
extern const char LOG_INFO_MSG[] PROGMEM;
extern const char LOG_DATA_MSG[] PROGMEM;
extern const char LOG_WARN_MSG[] PROGMEM;
extern const char LOG_ERROR_MSG[] PROGMEM;

#define send_main_done() \
    rurp_log_P(LOG_MAIN_DONE_MSG, PSTR(""))

#define send_init_done() \
    rurp_log_P(LOG_INIT_DONE_MSG, PSTR(""))

#define send_end_done() \
    rurp_log_P(LOG_END_DONE_MSG, PSTR(""))

// --- High-Level Logging Macros ---
#define log_info(msg)                               \
    if (is_flag_set(FLAG_VERBOSE) && (msg)[0] != '\0') { \
        rurp_log(LOG_INFO_MSG, msg);                \
    }

#define log_info_const(msg)                        \
    if (is_flag_set(FLAG_VERBOSE)) {               \
        rurp_log_P(LOG_INFO_MSG, PSTR(msg));       \
    }

#define log_info_format(cformat, ...)                       \
    if (is_flag_set(FLAG_VERBOSE)) {                        \
        format(handle->response_msg, cformat, __VA_ARGS__); \
        log_info(handle->response_msg)                      \
    }

#define log_data(msg) \
    rurp_log(LOG_DATA_MSG, msg)

#define log_data_const(msg)                    \
    rurp_log_P(LOG_DATA_MSG, PSTR(msg))

#define log_data_format(cformat, ...)                       \
    format(handle->response_msg, cformat, __VA_ARGS__);     \
    log_data(handle->response_msg)

#define log_warn(msg) \
    rurp_log(LOG_WARN_MSG, msg)

#define log_warn_const(msg)                    \
    rurp_log_P(LOG_WARN_MSG, PSTR(msg))

#define log_warn_format(cformat, ...) \
    log_warn_format_buf(handle->response_msg, cformat, __VA_ARGS__)

#define log_warn_format_buf(buf, cformat, ...) \
    format(buf, cformat, __VA_ARGS__);         \
    log_warn(buf)

#define log_error(msg) \
    rurp_log(LOG_ERROR_MSG, msg)

#define log_error_const(msg) \
    rurp_log_P(LOG_ERROR_MSG, PSTR(msg))

#define log_error_format(cformat, ...) \
    log_error_format_buf(handle->response_msg, cformat, __VA_ARGS__)

#define log_error_format_buf(buf, cformat, ...) \
    format(buf, cformat, __VA_ARGS__);          \
    log_error(buf)

#define copy_to_buffer(buf, msg) \
    strcpy_P(buf, PSTR(msg));

// Use sprintf_P to read the format string directly from PROGMEM.
// This avoids copying the format string to a temporary RAM buffer first.
#define format(buf, cformat, ...) \
    sprintf_P(buf, PSTR(cformat), __VA_ARGS__)


// --------------- 

#define format_P_int(buf, progmem_str, value) \
    {                                         \
        copy_to_buffer(buf, progmem_str);     \
        itoa(value, buf + strlen(buf), 10);   \
    }

#define format_P_char(buf, progmem_str, value) \
    {                                          \
        copy_to_buffer(buf, progmem_str);      \
        size_t len = strlen(buf);              \
        buf[len] = value;                      \
        buf[len + 1] = '\0';                   \
    }

#define log_error_P_int_buf(buf, progmem_str, value) \
    {                                                \
        format_P_int(buf, progmem_str, value);       \
        log_error(buf);                              \
    }

#define log_info_P_int_buf(buf, progmem_str, value) \
    {                                               \
        format_P_int(buf, progmem_str, value);      \
        log_info(buf);                              \
    }

#define log_info_P_char_buf(buf, progmem_str, value) \
    {                                                \
        format_P_char(buf, progmem_str, value);      \
        log_info(buf);                               \
    }

#define log_info_P_int(progmem_str, value) \
    log_info_P_int_buf(handle->response_msg, progmem_str, value)
#define log_info_P_char(progmem_str, value) \
    log_info_P_char_buf(handle->response_msg, progmem_str, value)

// --- ACK Macros ---
#define send_ack(msg) \
    do { \
        if (use_binary_protocol) { \
            send_binary_response(RESPONSE_CODE_OK, msg); \
        } else { \
            rurp_log(LOG_OK_MSG, msg); \
        } \
    } while(0)

#define send_ack_const(msg) \
    do { \
        if (use_binary_protocol) { \
            send_binary_response(RESPONSE_CODE_OK, msg); \
        } else { \
            rurp_log_P(LOG_OK_MSG, PSTR(msg)); \
        } \
    } while(0)

#define send_ack_format(cformat, ...)                       \
    do { \
        format(handle->response_msg, cformat, __VA_ARGS__); \
        send_ack(handle->response_msg); \
    } while(0)
#define log_error_P_int(progmem_str, value) \
    log_error_P_int_buf(handle->response_msg, progmem_str, value)

#ifdef SERIAL_DEBUG
extern char* debug_msg_buffer;

void debug_setup();
void debug_buf(const char* msg);

#define debug(msg)      \
    {                   \
        debug_buf(msg); \
    }

#define debug_format(cformat, ...)                      \
    {                                                   \
        format(debug_msg_buffer, cformat, __VA_ARGS__); \
        debug_buf(debug_msg_buffer);                    \
    }

#else
#define debug(msg)
#define debug_format(cformat, ...)
#define debug_setup()
#define debug_buf(msg)
#define log_debug(type, msg)
#endif

#define firestarter_data_response(msg) \
    firestarter_set_response(RESPONSE_CODE_DATA, msg)

#define firestarter_data_response_format(msg, ...) \
    firestarter_response_format(RESPONSE_CODE_DATA, msg, __VA_ARGS__)

#define firestarter_warning_response(msg) \
    firestarter_set_response(RESPONSE_CODE_WARNING, msg)

#define firestarter_warning_response_format(msg, ...) \
    firestarter_response_format(RESPONSE_CODE_WARNING, msg, __VA_ARGS__)

#define firestarter_error_response(msg) \
    firestarter_set_response(RESPONSE_CODE_ERROR, msg)

#define firestarter_error_response_format(msg, ...) \
    firestarter_response_format(RESPONSE_CODE_ERROR, msg, __VA_ARGS__)

// void _firestarter_set_response( uint8_t code, const char* msg, firestarter_handle_t* handle);

#define firestarter_set_response(code, msg)    \
    copy_to_buffer(handle->response_msg, msg); \
    handle->response_code = code;

#define firestarter_response_format(code, msg, ...) \
    format(handle->response_msg, msg, __VA_ARGS__); \
    handle->response_code = code;

#endif  // __LOGGING_H__
