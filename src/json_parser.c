/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "json_parser.h"
#include <stdio.h>

#include "jsmn.h"

uint8_t get_cmd(const char* json, jsmntok_t* tokens, int pos);

int parse_bus_config(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle);

bool get_flags(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_memory_size(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_address(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_chip_id(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_type(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_pin_count(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_delay(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_vpp_mv(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_algorithm(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_read_settling(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_read_strobe(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_page_size(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);

bool get_rw_pin(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
bool get_vpp_pin(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);

bool get_r1(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config);
bool get_r2(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config);
bool get_rev(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config);

static int jsoneq_(const char* json, jsmntok_t* tok, const char* s);

static unsigned long simple_strtoul(const char* s) {
    unsigned long val = 0;
    // Note: This simple implementation only handles positive decimal numbers.
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

#define jsoneq(json, tok, s) \
    jsoneq_(json, tok, PSTR(s))

int json_init(const char* json, int len, jsmntok_t* tokens) {
    jsmn_parser parser;
    jsmn_init(&parser);
    return jsmn_parse(&parser, json, len, tokens, sizeof(tokens) / sizeof(tokens[0]));
}

const char key_mem_size[] PROGMEM = "memory-size";
const char key_address[] PROGMEM = "address";
const char key_flags[] PROGMEM = "flags";
const char key_chip_id[] PROGMEM = "chip-id";
const char key_pin_count[] PROGMEM = "pin-count";
const char key_pulse_delay[] PROGMEM = "pulse-delay";
const char key_vpp_mv[] PROGMEM = "vpp_mv";
const char key_type[] PROGMEM = "type";
const char key_algorithm[] PROGMEM = "algorithm";
/* Phase 44 — host-tunable read-timing knobs (D-04 sweep params) */
const char key_read_settling[] PROGMEM = "read-settling-delay";
const char key_read_strobe[]   PROGMEM = "read-strobe-us";
/* PGSZ-03 / CR-01 — per-chip page size from DB (mirrors key_read_strobe pattern) */
const char key_page_size[]     PROGMEM = "page-size";

typedef struct {
    PGM_P key;
    bool (*parser_func)(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);
} key_parser_t;

static const key_parser_t key_parsers[] PROGMEM = {
    {key_mem_size, get_memory_size}, {key_address, get_address},         {key_flags, get_flags},
    {key_chip_id, get_chip_id},      {key_pin_count, get_pin_count},     {key_pulse_delay, get_delay},
    {key_vpp_mv, get_vpp_mv},        {key_type, get_type},               {key_algorithm, get_algorithm},
    /* Phase 44 — read-timing sweep knobs (RCA-01 causal proof, D-04) */
    {key_read_settling, get_read_settling},                              {key_read_strobe, get_read_strobe},
    /* PGSZ-03 / CR-01 — per-chip page size (mirrors key_read_strobe registration) */
    {key_page_size, get_page_size},
};

int json_parse(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle) {
    handle->address = 0;
    handle->ctrl_flags = 0;
    handle->bus_config.rw_line = 0xFF;
    handle->bus_config.vpp_line = 0xFF;
    handle->bus_config.address_lines[0] = 0xFF;
    handle->bus_config.address_mask = 0;
    handle->bus_config.static_high_mask = 0;
    handle->chip_id = 0;

    if (token_count < 1 || tokens[0].type != JSMN_OBJECT) {
        return -1; // Not a JSON object
    }

    int num_pairs = tokens[0].size;
    int token_idx = 1;

    for (int i = 0; i < num_pairs; i++) {
        if (token_idx >= token_count) {
            return -1; // Should not happen with valid JSON
        }

        jsmntok_t* key_token = &tokens[token_idx];

        // The 'cmd' key is handled by json_get_cmd before this function is called.
        // We just need to identify and skip it here.
        if (jsoneq(json, key_token, "cmd") == 0 || jsoneq(json, key_token, "state") == 0) {
            token_idx += 2; // Skip key and value
            continue;
        }

        bool found = false;
        for (size_t j = 0; j < sizeof(key_parsers) / sizeof(key_parsers[0]); j++) {
            PGM_P key = (PGM_P)pgm_read_ptr(&key_parsers[j].key);
            if (jsoneq_(json, key_token, key) == 0) {
                bool (*parser_func)(const char*, jsmntok_t*, int, firestarter_handle_t*) = (void*)pgm_read_ptr(&key_parsers[j].parser_func);
                parser_func(json, tokens, token_idx, handle);
                token_idx += 2; // Skip key and simple value
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        if (jsoneq(json, key_token, "bus-config") == 0) {
            int consumed = parse_bus_config(json, &tokens[token_idx], token_count - token_idx, handle);
            if (consumed < 0) return -1;
            token_idx += 1 + consumed; // Advance past the key and the entire object value
        } else {
            // Unknown field — skip key + value token (forward-compatible with new Python fields)
            token_idx += 2;
        }
    }
    if (handle->bus_config.address_lines[0] == 0xFF) {
        handle->bus_config.address_mask = 0xFFFF;
    }
    return 0;
}

int json_parse_config(const char* json, jsmntok_t* tokens, int token_count, rurp_configuration_t* config, firestarter_handle_t* handle) {
    int res = 0;
    for (int i = 1; i < token_count; i++) {
        if (get_cmd(json, tokens, i) != 0xFF) {
            i++;
        } else if (get_flags(json, tokens, i, handle)) {
            i++;
        }
#ifdef HARDWARE_REVISION
        else if (get_rev(json, tokens, i, config)) {
            i++;
            res = 1;
        }
#endif
        else if (get_r1(json, tokens, i, config)) {
            i++;
            res = 1;
        } else if (get_r2(json, tokens, i, config)) {
            i++;
            res = 1;
        } else {
            return -1;
        }
    }
    return res;
}

uint8_t json_get_cmd(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle) {
    handle->cmd = 0xFF;
    handle->ctrl_flags = 0;
    bool found_flags = false;
    for (int i = 0; i < token_count; i++) {
        uint8_t cmd = get_cmd(json, tokens, i);
        if (cmd != 0xFF) {
            handle->cmd = cmd;
            i++;
        } else if (get_flags(json, tokens, i, handle)) {
            i++;
            found_flags = true;
        }
        if (handle->cmd != 0xFF && found_flags) {
            break;
        }
    }
    return handle->cmd;
}

uint8_t get_cmd(const char* json, jsmntok_t* tokens, int pos) {
    if (jsoneq(json, &tokens[pos], "cmd") == 0 || jsoneq(json, &tokens[pos], "state") == 0) {
        return simple_strtoul(json + tokens[pos + 1].start);
    }
    return 0xFF;
}

int parse_bus_config(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle) {
    // tokens[0] is the "bus-config" key.
    // tokens[1] is the object token. Its `size` is the number of key-value pairs.
    if (token_count < 2 || tokens[1].type != JSMN_OBJECT) {
        return 0;
    }

    int num_pairs = tokens[1].size;
    int total_consumed_tokens = 1; // Account for the object token itself.
    int current_token_idx = 2;     // Start at the first key inside the object.

    for (int i = 0; i < num_pairs; i++) {
        if (current_token_idx >= token_count) {
            return -1; // Should not happen with valid JSON
        }
        jsmntok_t* key_token = &tokens[current_token_idx];

        if (jsoneq(json, key_token, "bus") == 0) {
            jsmntok_t* array_token = &tokens[current_token_idx + 1];
            if (array_token->type != JSMN_ARRAY) return -1;

            int bus_array_size = array_token->size;
            int bus_array_start_idx = current_token_idx + 1;

            handle->bus_config.matching_lines = 0xff;
            for (int j = 0; j < bus_array_size && j < ADDRESS_LINES_SIZE; j++) {
                handle->bus_config.address_lines[j] = simple_strtoul(json + tokens[bus_array_start_idx + j + 1].start);
                handle->bus_config.address_mask |= 1UL << handle->bus_config.address_lines[j];
                if (handle->bus_config.matching_lines == 0xff && handle->bus_config.address_lines[j] != j) {
                    handle->bus_config.matching_lines = j;
                }
            }
            if (handle->bus_config.matching_lines == 0xff) { handle->bus_config.matching_lines = bus_array_size; }
            if (bus_array_size < ADDRESS_LINES_SIZE) { handle->bus_config.address_lines[bus_array_size] = 0xFF; }

            int pair_tokens = 1 + 1 + bus_array_size; // key + array_token + elements
            total_consumed_tokens += pair_tokens;
            current_token_idx += pair_tokens;
        } else if (jsoneq(json, key_token, "static-high") == 0) {
            jsmntok_t* array_token = &tokens[current_token_idx + 1];
            if (array_token->type != JSMN_ARRAY) return -1;

            int sh_array_size = array_token->size;
            int sh_array_start_idx = current_token_idx + 1;
            for (int j = 0; j < sh_array_size; j++) {
                uint8_t line = simple_strtoul(json + tokens[sh_array_start_idx + j + 1].start);
                handle->bus_config.static_high_mask |= 1UL << line;
            }

            int pair_tokens = 1 + 1 + sh_array_size; // key + array_token + elements
            total_consumed_tokens += pair_tokens;
            current_token_idx += pair_tokens;
        } else if (get_rw_pin(json, tokens, current_token_idx, handle)) {
            total_consumed_tokens += 2;
            current_token_idx += 2;
        } else if (get_vpp_pin(json, tokens, current_token_idx, handle)) {
            total_consumed_tokens += 2;
            current_token_idx += 2;
        } else {
            // Unknown key — skip key + value tokens
            total_consumed_tokens += 2;
            current_token_idx += 2;
        }
    }
    return total_consumed_tokens;
}

static int jsoneq_(const char* json, jsmntok_t* tok, const char* s) {
    if (tok->type == JSMN_STRING && (int)strlen_P(s) == tok->end - tok->start &&
        strncmp_P(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

#define extract_num(element, register, type)           \
    if (jsoneq(json, &tokens[pos], element) == 0) {    \
        register = type(json + tokens[pos + 1].start); \
        return 1;                                      \
    }                                                  \
    return 0;

#define extract_long(element, register) \
    extract_num(element, register, simple_strtoul)

#define extract_int(element, register) extract_long(element, register)

bool get_flags(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_long("flags", handle->ctrl_flags);
}

bool get_memory_size(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_long("memory-size", handle->mem_size);
}

bool get_address(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_long("address", handle->address);
}

bool get_chip_id(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("chip-id", handle->chip_id);
}

bool get_pin_count(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("pin-count", handle->pins);
}

bool get_type(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("type", handle->mem_type);
}

bool get_delay(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_long("pulse-delay", handle->pulse_delay);
}

bool get_vpp_mv(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("vpp_mv", handle->vpp_mv);
}

bool get_algorithm(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_long("algorithm", handle->protocol);
}

bool get_rw_pin(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("rw-pin", handle->bus_config.rw_line);
}

bool get_vpp_pin(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    extract_int("vpp-pin", handle->bus_config.vpp_line);
}

bool get_r1(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config) {
    extract_long("r1", config->r1);
}

bool get_r2(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config) {
    extract_long("r2", config->r2);
}

bool get_rev(const char* json, jsmntok_t* tokens, int pos, rurp_configuration_t* config) {
    extract_int("rev", config->hardware_revision);
}

/*
 * Phase 44 — read-timing sweep knobs (RCA-01 / D-04).
 *
 * T-44-01 cap: both knobs are clamped to READ_TIMING_MAX_US at parse time so
 * an absurd JSON value cannot pass an unbounded value to delayMicroseconds()
 * in the read loop.  Values < 3µs are below delayMicroseconds() accuracy on
 * 16 MHz AVR (Pitfall 5) — documented by the caller in memory_get_data().
 *
 * Zero-ambiguity:
 *   read_settling_us == 0 → no settling delay (explicit test point; D-04)
 *   read_strobe_us   == 0 → use firmware default 3µs (preserves current behaviour)
 */
#define READ_TIMING_MAX_US 1000UL   /* T-44-01 sane max (~1ms); caps both knobs */

bool get_read_settling(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    if (jsoneq(json, &tokens[pos], "read-settling-delay") == 0) {
        unsigned long v = simple_strtoul(json + tokens[pos + 1].start);
        handle->read_settling_us = (uint32_t)(v > READ_TIMING_MAX_US ? READ_TIMING_MAX_US : v);
        return 1;
    }
    return 0;
}

bool get_read_strobe(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    if (jsoneq(json, &tokens[pos], "read-strobe-us") == 0) {
        unsigned long v = simple_strtoul(json + tokens[pos + 1].start);
        handle->read_strobe_us = (uint32_t)(v > READ_TIMING_MAX_US ? READ_TIMING_MAX_US : v);
        return 1;
    }
    return 0;
}

/*
 * PGSZ-03 / CR-01 — per-chip page size parser (mirrors get_read_strobe pattern).
 *
 * Parses the "page-size" JSON key into handle->page_size.
 * Bound-check (V5 / T-94-PGSZBAD): a 0 or absurd value is treated as unset
 * so flash4_write_execute falls back to flash4_page_size(handle->mem_size).
 * Absurd cap: page sizes > 65536 bytes would exceed any flash4 chip's capacity;
 * a value that large indicates a malformed/hand-crafted JSON command and must
 * degrade safely to the heuristic (handle->page_size == 0 → fallback).
 * The maximum legitimate page size for any FLASH-AMD-STD chip is 256 bytes.
 */
#define PAGE_SIZE_MAX_BYTES 65536UL  /* sane cap; any value beyond this is malformed */

bool get_page_size(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle) {
    if (jsoneq(json, &tokens[pos], "page-size") == 0) {
        unsigned long v = simple_strtoul(json + tokens[pos + 1].start);
        /* V5: 0 or absurd value → leave handle->page_size at 0 so firmware falls back
         * to flash4_page_size(handle->mem_size) heuristic (PGSZ-02 safe fallback). */
        handle->page_size = (uint32_t)((v == 0 || v > PAGE_SIZE_MAX_BYTES) ? 0 : v);
        return 1;
    }
    return 0;
}
