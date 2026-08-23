/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include <stddef.h>
#include <string.h>

#include "json_parser.h"
#include <stdio.h>

#include "jsmn.h"

uint8_t get_cmd(const char* json, jsmntok_t* tokens, int pos);

int parse_bus_config(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle);

bool get_flags(const char* json, jsmntok_t* tokens, int pos, firestarter_handle_t* handle);

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

const char key_mem_size[] PROGMEM = "memory-size";
const char key_address[] PROGMEM = "address";
const char key_flags[] PROGMEM = "flags";
const char key_chip_id[] PROGMEM = "chip-id";
const char key_pin_count[] PROGMEM = "pin-count";
const char key_pulse_delay[] PROGMEM = "pulse-delay";
const char key_vpp_mv[] PROGMEM = "vpp_mv";
const char key_algorithm[] PROGMEM = "algorithm";
/* Phase 44 — host-tunable read-timing knobs (D-04 sweep params) */
const char key_read_settling[] PROGMEM = "read-settling-delay";
const char key_read_strobe[]   PROGMEM = "read-strobe-us";
/* Phase 149 — per-chip page-write size delivered by the host (PGSZ-01/PGSZ-02).
 * Wire key is the HYPHEN form "page-size" -- the internal database key
 * programming.page_size uses an underscore, so a PROGMEM string written
 * against the underscore form would silently never match. */
const char key_page_size[]     PROGMEM = "page-size";

/* T-44-01 sane max (~1ms); caps both read-timing knobs. Hoisted above the
 * field table below, which is now the site that applies it. */
#define READ_TIMING_MAX_US 1000UL   /* T-44-01 sane max (~1ms); caps both knobs */

/* Phase-agnostic field table (replaces key_parser_t / key_parsers[]).
 *
 * WHY THIS SHAPE. The old table matched the wire key, then called a get_*
 * stub that RE-MATCHED the very same key via extract_num's hidden jsoneq --
 * so every key lived in flash twice and every field cost a redundant
 * jsoneq_ call. Worse, those stubs were reached through a PROGMEM function
 * pointer, so gcc could not inline them: each kept a full 4-argument ABI
 * prologue for what is one strtoul and one store. Measured at 1012 B across
 * 11 stubs, against 0 B for the five IDENTICAL stubs (get_r1/r2/rev/rw_pin/
 * vpp_pin) that are called directly with a literal key and inline away.
 *
 * This table is now the single source of truth for wire key -> handle field
 * -> clamp. `width` is derived from the member itself with sizeof, so it can
 * never drift from the field it writes.
 *
 * get_flags is deliberately NOT here: json_parse_config calls it directly at
 * two sites, where it must still match its own key. It inlines there, so it
 * costs nothing. */
typedef struct {
    PGM_P    key;
    uint8_t  offset; /* offsetof() into firestarter_handle_t */
    uint8_t  width;  /* 1, 2 or 4 -- sizeof the member, never hand-written */
    uint16_t clamp;  /* 0 = unclamped */
} field_desc_t;

#define FIELD(k, member, cl)                                     \
    { k, (uint8_t)offsetof(firestarter_handle_t, member),          \
      (uint8_t)sizeof(((firestarter_handle_t*)0)->member), (cl) }

static const field_desc_t FIELDS[] PROGMEM = {
    FIELD(key_mem_size,      mem_size,          0),
    FIELD(key_address,       address,           0),
    FIELD(key_flags,         ctrl_flags,        0),
    FIELD(key_chip_id,       chip_id,           0),
    FIELD(key_pin_count,     pins,              0),
    FIELD(key_pulse_delay,   pulse_delay,       0),
    FIELD(key_vpp_mv,        vpp_mv,            0),
    FIELD(key_algorithm,     protocol,          0),
    /* Phase 44 -- read-timing sweep knobs, clamp preserved from the deleted
     * get_read_settling / get_read_strobe stubs (T-44-01). */
    FIELD(key_read_settling, read_settling_us,  READ_TIMING_MAX_US),
    FIELD(key_read_strobe,   read_strobe_us,    READ_TIMING_MAX_US),
    /* Phase 149 -- page-size seam (PGSZ-01/PGSZ-02) */
    FIELD(key_page_size,     page_size,         0),
};

/* Every field above must sit below data_buffer, or a uint8_t offset truncates
 * and this writes into the wrong member. Guarded, not assumed. */
_Static_assert(offsetof(firestarter_handle_t, page_size) < 256,
               "firestarter_handle_t reordered: a FIELDS offset no longer fits uint8_t");

/* Writes the low `width` bytes of v at the member's offset. Correct on AVR and
 * on the little-endian PY32F071 ARM port; a big-endian target would break HERE
 * and nowhere else. */
static void store_field(firestarter_handle_t* handle, const field_desc_t* fd,
                        unsigned long v) {
    uint16_t clamp = pgm_read_word(&fd->clamp);
    if (clamp && v > clamp) {
        v = clamp;
    }
    uint8_t width = pgm_read_byte(&fd->width);
    /* Saturate rather than truncate. A wire value wider than its member would
     * otherwise have its high bytes silently dropped -- and for `algorithm`
     * that is a SAFETY issue, not cosmetics: 0x105 would land as 0x05 and
     * dispatch into configure_flash_5v_page instead of reaching
     * configure_memory's fail-closed tail. Saturating sends it to the member's
     * max instead, which is not a known protocol, so it still fail-closes.
     * One site covers every narrow field (pins, chip_id, vpp_mv, page_size),
     * which the deleted per-stub form could never do. */
    if (width < sizeof(v)) {
        unsigned long max = (1UL << (width * 8)) - 1UL;
        if (v > max) {
            v = max;
        }
    }
    memcpy((uint8_t*)handle + pgm_read_byte(&fd->offset), &v, width);
}
int json_parse(const char* json, jsmntok_t* tokens, int token_count, firestarter_handle_t* handle) {
    handle->address = 0;
    handle->ctrl_flags = 0;
    handle->bus_config.rw_line = 0xFF;
    handle->bus_config.vpp_line = 0xFF;
    handle->bus_config.address_lines[0] = 0xFF;
    handle->bus_config.address_mask = 0;
    handle->bus_config.static_high_mask = 0;
    handle->chip_id = 0;
    /* D-05: page_size resets to 0 exactly like chip_id above. handle is a
     * single file-scope global with no per-command memset, and page-size is
     * emit-when-present, so without this reset a 128 parsed for one chip
     * would persist into the next command and "absent means 64" becomes
     * false in practice -- the exact overrun PGSZ-02 exists to prevent.
     * The two Phase 44 read-timing knobs (read_settling_us, read_strobe_us)
     * are NOT added to this reset block by this phase (deliberately -- a
     * pre-existing latent instance of the same defect, filed as a todo by
     * plan 07); their absence here is not an oversight this phase
     * introduced. */
    handle->page_size = 0;

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
        for (uint8_t j = 0; j < sizeof(FIELDS) / sizeof(FIELDS[0]); j++) {
            PGM_P key = (PGM_P)pgm_read_ptr(&FIELDS[j].key);
            if (jsoneq_(json, key_token, key) == 0) {
                store_field(handle, &FIELDS[j],
                            simple_strtoul(json + tokens[token_idx + 1].start));
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



/*
 * Phase 149 — page-size seam (PGSZ-01/PGSZ-02, D-07).
 *
 * Deliberately the plain one-line extract_int form (get_chip_id's model),
 * NOT the Phase 44 clamp form above: validation (power-of-two, range, the
 * silent fallback) lives in the 0x0D handler (eeprom28c_page_mask), which
 * keeps json_parse algorithm-agnostic and costs the fewest bytes here.
 */
