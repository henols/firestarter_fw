/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The const, protocol_id-keyed EPROM parameter table and its fail-closed
 * linear-scan accessor.
 *
 * Do NOT include an Arduino framework header here -- it would add
 * macro-redefinition warnings on the native build.
 *
 * The accessor is a linear SCAN, never a switch: a switch here would be
 * exactly the second dispatch selector this table exists not to have.
 */
#include "eprom_params.h"
#include "eprom_rows.h"

/* Lookup key array, positionally parallel to EPROM_PARAMS below. */
static const uint8_t EPROM_PARAM_KEYS[] PROGMEM = { 0x07, 0x08, 0x0B };

/*
 * Per-cell attribution -- family, representative part, datasheet revision, or
 * an explicit "no datasheet basis" note -- lives in the sidecar
 * tests/golden/eprom_params_citations.json.
 *
 * Known divergence: the Intel-family 1 ms parts on 0x07 want a margin pulse.
 * The firmware has no overprogram pulse, so serving them would need that
 * feature back and a second 0x07 row, which the table's no-second-dispatch-key
 * rule forbids.
 */
/* energy_cap_us and max_pulses come from eprom_rows.h, generated from the
 * meta repo's eprom_rows.toml. The host's write timeout uses the same values. */
static const eprom_params_t EPROM_PARAMS[] PROGMEM = {
    /* 0x07 PROTO_EPROM_28PIN */ { EPROM_ROW_07_ENERGY_CAP_US, EPROM_ROW_07_MAX_PULSES, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR, 0 },
    /* 0x08 PROTO_EPROM_32PIN */ { EPROM_ROW_08_ENERGY_CAP_US, EPROM_ROW_08_MAX_PULSES, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR, 0 },
    /* 0x0B PROTO_EPROM_24PIN */ { EPROM_ROW_0B_ENERGY_CAP_US, EPROM_ROW_0B_MAX_PULSES, VERIFY_PER_PULSE,            VPP_PATH_DIRECT_VPE,    0 },
};

/* noinline: it has several callers in eprom.cpp, and LTO inlining it into
 * each one costs more flash than the call. */
const eprom_params_t* __attribute__((noinline)) eprom_params_for(uint32_t protocol) {
    for (size_t i = 0; i < sizeof(EPROM_PARAM_KEYS) / sizeof(EPROM_PARAM_KEYS[0]); i++) {
        if ((uint32_t)pgm_read_byte(&EPROM_PARAM_KEYS[i]) == protocol) {
            return &EPROM_PARAMS[i];
        }
    }
    return NULL; /* Fail closed: a null pointer with zero hardware side effects, never &EPROM_PARAMS[0]. */
}
