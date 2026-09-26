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

/* Lookup key array, positionally parallel to EPROM_PARAMS below. */
static const uint8_t EPROM_PARAM_KEYS[] PROGMEM = { 0x07, 0x08, 0x0B };

/*
 * Per-cell attribution -- family, representative part, datasheet revision, or
 * an explicit "no datasheet basis" note -- lives in the gate-enforced sidecar
 * tests/golden/eprom_params_citations.json.
 *
 * Known divergence, recorded rather than dropped: the 22 Intel-family 1 ms
 * parts on 0x07 genuinely want a 3xN margin pulse. Serving them would need a
 * second 0x07 row, which the table's no-second-dispatch-key rule forbids.
 */
static const eprom_params_t EPROM_PARAMS[] PROGMEM = {
    /* 0x07 PROTO_EPROM_28PIN */ { 75000UL, 0UL,     25,  0, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR },
    /* 0x08 PROTO_EPROM_32PIN */ { 75000UL, 0UL,     25,  0, VERIFY_PER_PULSE_PLUS_FINAL, VPP_PATH_DROP_RESISTOR },
    /* 0x0B PROTO_EPROM_24PIN */ { 75000UL, 50000UL, 255, 0, VERIFY_PER_PULSE,            VPP_PATH_DIRECT_VPE    },
};

const eprom_params_t* eprom_params_for(uint32_t protocol) {
    for (size_t i = 0; i < sizeof(EPROM_PARAM_KEYS) / sizeof(EPROM_PARAM_KEYS[0]); i++) {
        if ((uint32_t)pgm_read_byte(&EPROM_PARAM_KEYS[i]) == protocol) {
            return &EPROM_PARAMS[i];
        }
    }
    return NULL; /* Fail closed: a null pointer with zero hardware side effects, never &EPROM_PARAMS[0]. */
}
