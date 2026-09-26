/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Convenience macros for the ID-encoded log path.
 *
 * These macros wrap rurp_log_id() so call-sites can emit catalog-driven
 * binary frames without manually packing param byte arrays. Multi-byte
 * params are encoded MSB-first.
 *
 * Coexists with the legacy log_*_const / log_*_format macros in logging.h
 * No existing call-site is converted by this header — these macros
 * are the surface Phases 7-8 will migrate call-sites to.
 */

#ifndef __LOGGING_ID_H__
#define __LOGGING_ID_H__

#include "firestarter.h"
#include "messages.h"
#include "rurp_shield.h"

// --- Unconditional ID-frame emit ---

// Zero-param: emit just the ID, no params.
#define LOG_ID(id) rurp_log_id((id), NULL, 0)

// Fixed-shape param macros — thin wrappers over the rurp_log_id_u{8,16,24,32}
// helpers in rurp_serial_utils.cpp. The macros exist so call sites keep the
// LOG_ID_U* spelling, but no byte-array build is inlined at each invocation.
#define LOG_ID_U8(id, p1)     rurp_log_id_u8((id), (uint8_t)(p1))
#define LOG_ID_U16(id, p1)    rurp_log_id_u16((id), (uint16_t)(p1))
#define LOG_ID_U24(id, p1)    rurp_log_id_u24((id), (uint32_t)(p1))
#define LOG_ID_U32(id, p1)    rurp_log_id_u32((id), (uint32_t)(p1))

// Escape hatch — caller-supplied buffer + count.
#define LOG_ID_BYTES(id, buf_array, count) \
    rurp_log_id((id), (const uint8_t*)(buf_array), (uint8_t)(count))

// --- FLAG_VERBOSE-gated variants (INFO severity equivalent) ---

#define LOG_INFO_ID(id)                                                \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID(id);                                                \
        }                                                              \
    } while (0)

#define LOG_INFO_ID_U8(id, p1)                                         \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID_U8((id), (p1));                                     \
        }                                                              \
    } while (0)

#define LOG_INFO_ID_U16(id, p1)                                        \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID_U16((id), (p1));                                    \
        }                                                              \
    } while (0)

#define LOG_INFO_ID_U24(id, p1)                                        \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID_U24((id), (p1));                                    \
        }                                                              \
    } while (0)

#define LOG_INFO_ID_U32(id, p1)                                        \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID_U32((id), (p1));                                    \
        }                                                              \
    } while (0)

#define LOG_INFO_ID_BYTES(id, buf_array, count)                        \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            LOG_ID_BYTES((id), (buf_array), (count));                  \
        }                                                              \
    } while (0)

// ascii_str param: length-prefix byte + N string bytes (capped at 32).
// Wire shape: [slen][s_data...] — matches catalog `type = "ascii_str"` so
// the host decoder reads slen first, then exactly slen string bytes.
// string.h (strlen + memcpy) available via rurp_shield.h transitively.
#define LOG_INFO_ID_ASTR(id, str_ptr)                                  \
    do {                                                               \
        if (is_flag_set(FLAG_VERBOSE)) {                               \
            const char* _s = (str_ptr);                                \
            uint8_t _slen = (uint8_t)strlen(_s);                       \
            if (_slen > 32) _slen = 32;                                \
            uint8_t _b[1 + 32];                                        \
            _b[0] = _slen;                                             \
            memcpy(_b + 1, _s, _slen);                                 \
            rurp_log_id((id), _b, (uint8_t)(1 + _slen));               \
        }                                                              \
    } while (0)

// --- Unconditional ERROR severity ---

#define LOG_ERROR_ID(id)               LOG_ID(id)
#define LOG_ERROR_ID_U8(id, p1)        LOG_ID_U8((id), (p1))
#define LOG_ERROR_ID_U16(id, p1)       LOG_ID_U16((id), (p1))
#define LOG_ERROR_ID_U24(id, p1)       LOG_ID_U24((id), (p1))
#define LOG_ERROR_ID_U32(id, p1)       LOG_ID_U32((id), (p1))
#define LOG_ERROR_ID_BYTES(id, b, n)   LOG_ID_BYTES((id), (b), (n))

// --- Unconditional WARN severity ---

#define LOG_WARN_ID(id)                LOG_ID(id)
#define LOG_WARN_ID_U8(id, p1)         LOG_ID_U8((id), (p1))
#define LOG_WARN_ID_U16(id, p1)        LOG_ID_U16((id), (p1))
#define LOG_WARN_ID_U24(id, p1)        LOG_ID_U24((id), (p1))
#define LOG_WARN_ID_U32(id, p1)        LOG_ID_U32((id), (p1))
#define LOG_WARN_ID_BYTES(id, b, n)    LOG_ID_BYTES((id), (b), (n))

// --- Unconditional OK severity ---

#define LOG_OK_ID(id)                  LOG_ID(id)
#define LOG_OK_ID_U8(id, p1)           LOG_ID_U8((id), (p1))
#define LOG_OK_ID_U16(id, p1)          LOG_ID_U16((id), (p1))
#define LOG_OK_ID_U24(id, p1)          LOG_ID_U24((id), (p1))
#define LOG_OK_ID_U32(id, p1)          LOG_ID_U32((id), (p1))
#define LOG_OK_ID_BYTES(id, b, n)      LOG_ID_BYTES((id), (b), (n))

// --- Unconditional INIT severity ---

#define LOG_INIT_ID(id)                LOG_ID(id)
#define LOG_INIT_ID_U8(id, p1)         LOG_ID_U8((id), (p1))
#define LOG_INIT_ID_U16(id, p1)        LOG_ID_U16((id), (p1))
#define LOG_INIT_ID_U24(id, p1)        LOG_ID_U24((id), (p1))
#define LOG_INIT_ID_U32(id, p1)        LOG_ID_U32((id), (p1))
#define LOG_INIT_ID_BYTES(id, b, n)    LOG_ID_BYTES((id), (b), (n))

// --- Unconditional MAIN severity ---

#define LOG_MAIN_ID(id)                LOG_ID(id)
#define LOG_MAIN_ID_U8(id, p1)         LOG_ID_U8((id), (p1))
#define LOG_MAIN_ID_U16(id, p1)        LOG_ID_U16((id), (p1))
#define LOG_MAIN_ID_U24(id, p1)        LOG_ID_U24((id), (p1))
#define LOG_MAIN_ID_U32(id, p1)        LOG_ID_U32((id), (p1))
#define LOG_MAIN_ID_BYTES(id, b, n)    LOG_ID_BYTES((id), (b), (n))

// --- Unconditional END severity ---

#define LOG_END_ID(id)                 LOG_ID(id)
#define LOG_END_ID_U8(id, p1)          LOG_ID_U8((id), (p1))
#define LOG_END_ID_U16(id, p1)         LOG_ID_U16((id), (p1))
#define LOG_END_ID_U24(id, p1)         LOG_ID_U24((id), (p1))
#define LOG_END_ID_U32(id, p1)         LOG_ID_U32((id), (p1))
#define LOG_END_ID_BYTES(id, b, n)     LOG_ID_BYTES((id), (b), (n))

// --- Unconditional DATA severity ---

#define LOG_DATA_ID(id)                LOG_ID(id)
#define LOG_DATA_ID_U8(id, p1)         LOG_ID_U8((id), (p1))
#define LOG_DATA_ID_U16(id, p1)        LOG_ID_U16((id), (p1))
#define LOG_DATA_ID_U24(id, p1)        LOG_ID_U24((id), (p1))
#define LOG_DATA_ID_U32(id, p1)        LOG_ID_U32((id), (p1))
#define LOG_DATA_ID_BYTES(id, b, n)    LOG_ID_BYTES((id), (b), (n))

// --- DATA multi-param composites ---

// Two u16 values packed as 4 big-endian bytes.
#define LOG_DATA_ID_U16_U16(id, p1, p2)                                \
    do {                                                               \
        uint16_t _v1 = (uint16_t)(p1); uint16_t _v2 = (uint16_t)(p2); \
        uint8_t _b[4] = {                                              \
            (uint8_t)((_v1 >> 8) & 0xFF), (uint8_t)(_v1 & 0xFF),      \
            (uint8_t)((_v2 >> 8) & 0xFF), (uint8_t)(_v2 & 0xFF)       \
        };                                                             \
        rurp_log_id((id), _b, 4);                                      \
    } while (0)

// Two u32 values packed as 8 big-endian bytes.
#define LOG_DATA_ID_U32_U32(id, p1, p2)                                \
    do {                                                               \
        uint32_t _v1 = (uint32_t)(p1); uint32_t _v2 = (uint32_t)(p2); \
        uint8_t _b[8] = {                                              \
            (uint8_t)((_v1 >> 24) & 0xFF), (uint8_t)((_v1 >> 16) & 0xFF), \
            (uint8_t)((_v1 >> 8)  & 0xFF), (uint8_t)(_v1 & 0xFF),     \
            (uint8_t)((_v2 >> 24) & 0xFF), (uint8_t)((_v2 >> 16) & 0xFF), \
            (uint8_t)((_v2 >> 8)  & 0xFF), (uint8_t)(_v2 & 0xFF)      \
        };                                                             \
        rurp_log_id((id), _b, 8);                                      \
    } while (0)

// Four u16 values packed as 8 big-endian bytes.
// Used by MSG_DATA_VPP_VOLTAGE / MSG_DATA_VPE_VOLTAGE (catalog: 4 x u16 params).
#define LOG_DATA_ID_U16x4(id, p1, p2, p3, p4)                         \
    do {                                                               \
        uint16_t _a = (uint16_t)(p1); uint16_t _b2 = (uint16_t)(p2); \
        uint16_t _c = (uint16_t)(p3); uint16_t _d  = (uint16_t)(p4); \
        uint8_t _b[8] = {                                              \
            (uint8_t)((_a  >> 8) & 0xFF), (uint8_t)(_a  & 0xFF),      \
            (uint8_t)((_b2 >> 8) & 0xFF), (uint8_t)(_b2 & 0xFF),      \
            (uint8_t)((_c  >> 8) & 0xFF), (uint8_t)(_c  & 0xFF),      \
            (uint8_t)((_d  >> 8) & 0xFF), (uint8_t)(_d  & 0xFF)       \
        };                                                             \
        rurp_log_id((id), _b, 8);                                      \
    } while (0)

// --- OK multi-param composites (P-02) ---

// P-02: two u8 values packed as 2 bytes.
// Used by MSG_OK_REV (physical hw revision + effective/override, 0xFF = no override).
#define LOG_OK_ID_U8_U8(id, p1, p2)                                    \
    do {                                                               \
        uint8_t _b[2] = { (uint8_t)(p1), (uint8_t)(p2) };             \
        rurp_log_id((id), _b, 2);                                      \
    } while (0)

// --- SERIAL_DEBUG-gated DEBUG severity (B-01..B-04) ---
//
// In production builds (SERIAL_DEBUG undefined) every macro expands to nothing:
//   • zero flash cost — debug string literals never enter PROGMEM
//   • zero runtime cost — no branch, no buffer, no wire byte
// In development builds (-D SERIAL_DEBUG) each macro emits an ID frame:
//   MAGIC | len_u16 | MSG_DEBUG | sub_id [params] | crc | 0x0A
// The sub_id identifies the specific debug message in the [debug] catalog section
// (messages.toml / messages.h DBG_* defines).

#ifdef SERIAL_DEBUG

// Zero-param: sub_id only (1 wire byte of params after MSG_DEBUG id byte).
// Reuses LOG_ID_U8 treating sub_id as the single u8 param of MSG_DEBUG.
#define LOG_DEBUG_ID_SUB(sub_id)    LOG_ID_U8(MSG_DEBUG, (sub_id))

// One u8 param: sub_id + 1 byte.
#define LOG_DEBUG_ID_SUB_U8(sub_id, p1)                                    \
    do {                                                                    \
        uint8_t _b[2] = { (uint8_t)(sub_id), (uint8_t)(p1) };              \
        rurp_log_id(MSG_DEBUG, _b, 2);                                      \
    } while (0)

// Two u8 params: sub_id + 2 bytes.
#define LOG_DEBUG_ID_SUB_U8_U8(sub_id, p1, p2)                             \
    do {                                                                    \
        uint8_t _b[3] = { (uint8_t)(sub_id), (uint8_t)(p1),                \
                          (uint8_t)(p2) };                                  \
        rurp_log_id(MSG_DEBUG, _b, 3);                                      \
    } while (0)

// Three u8 params: sub_id + 3 bytes.
#define LOG_DEBUG_ID_SUB_U8_U8_U8(sub_id, p1, p2, p3)                      \
    do {                                                                    \
        uint8_t _b[4] = { (uint8_t)(sub_id), (uint8_t)(p1),                \
                          (uint8_t)(p2), (uint8_t)(p3) };                   \
        rurp_log_id(MSG_DEBUG, _b, 4);                                      \
    } while (0)

// Two u16 params: sub_id + 4 bytes MSB-first (for pairs of values that may exceed u8).
#define LOG_DEBUG_ID_SUB_U16_U16(sub_id, p1, p2)                            \
    do {                                                                    \
        uint16_t _v1 = (uint16_t)(p1); uint16_t _v2 = (uint16_t)(p2);      \
        uint8_t _b[5] = { (uint8_t)(sub_id),                               \
                          (uint8_t)((_v1 >> 8) & 0xFF), (uint8_t)(_v1 & 0xFF), \
                          (uint8_t)((_v2 >> 8) & 0xFF), (uint8_t)(_v2 & 0xFF) }; \
        rurp_log_id(MSG_DEBUG, _b, 5);                                      \
    } while (0)

// One u16 param: sub_id + 2 bytes MSB-first.
#define LOG_DEBUG_ID_SUB_U16(sub_id, p1)                                    \
    do {                                                                    \
        uint16_t _v = (uint16_t)(p1);                                       \
        uint8_t _b[3] = { (uint8_t)(sub_id),                               \
                          (uint8_t)((_v >> 8) & 0xFF),                      \
                          (uint8_t)(_v & 0xFF) };                           \
        rurp_log_id(MSG_DEBUG, _b, 3);                                      \
    } while (0)

// One u24 param: sub_id + 3 bytes MSB-first (top byte of u32 dropped).
#define LOG_DEBUG_ID_SUB_U24(sub_id, p1)                                    \
    do {                                                                    \
        uint32_t _v = (uint32_t)(p1);                                       \
        uint8_t _b[4] = { (uint8_t)(sub_id),                               \
                          (uint8_t)((_v >> 16) & 0xFF),                     \
                          (uint8_t)((_v >>  8) & 0xFF),                     \
                          (uint8_t)(_v & 0xFF) };                           \
        rurp_log_id(MSG_DEBUG, _b, 4);                                      \
    } while (0)

// One u32 param: sub_id + 4 bytes MSB-first.
#define LOG_DEBUG_ID_SUB_U32(sub_id, p1)                                    \
    do {                                                                    \
        uint32_t _v = (uint32_t)(p1);                                       \
        uint8_t _b[5] = { (uint8_t)(sub_id),                               \
                          (uint8_t)((_v >> 24) & 0xFF),                     \
                          (uint8_t)((_v >> 16) & 0xFF),                     \
                          (uint8_t)((_v >>  8) & 0xFF),                     \
                          (uint8_t)(_v & 0xFF) };                           \
        rurp_log_id(MSG_DEBUG, _b, 5);                                      \
    } while (0)

// ascii_str param: sub_id + length-prefix byte + N string bytes (capped at 32).
// Wire shape: [sub_id][slen][s_data ...]. string.h (strlen + memcpy) available
// via rurp_shield.h transitively.
#define LOG_DEBUG_ID_SUB_ASTR(sub_id, str_ptr)                              \
    do {                                                                    \
        const char* _s = (str_ptr);                                         \
        uint8_t _slen = (uint8_t)strlen(_s);                                \
        if (_slen > 32) _slen = 32;                                         \
        uint8_t _b[1 + 1 + 32];                                             \
        _b[0] = (uint8_t)(sub_id);                                          \
        _b[1] = _slen;                                                      \
        memcpy(_b + 2, _s, _slen);                                          \
        rurp_log_id(MSG_DEBUG, _b, (uint8_t)(2 + _slen));                   \
    } while (0)

#else  // !SERIAL_DEBUG — production no-op fallback

#define LOG_DEBUG_ID_SUB(sub_id)
#define LOG_DEBUG_ID_SUB_U8(sub_id, p1)
#define LOG_DEBUG_ID_SUB_U8_U8(sub_id, p1, p2)
#define LOG_DEBUG_ID_SUB_U8_U8_U8(sub_id, p1, p2, p3)
#define LOG_DEBUG_ID_SUB_U16_U16(sub_id, p1, p2)
#define LOG_DEBUG_ID_SUB_U16(sub_id, p1)
#define LOG_DEBUG_ID_SUB_U24(sub_id, p1)
#define LOG_DEBUG_ID_SUB_U32(sub_id, p1)
#define LOG_DEBUG_ID_SUB_ASTR(sub_id, str_ptr)

#endif  // SERIAL_DEBUG

#endif  // __LOGGING_ID_H__
