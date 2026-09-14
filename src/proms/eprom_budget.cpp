/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Per-block worst-case write-time budget arithmetic, declared in
 * eprom_budget.h. Reads the PROGMEM eprom_params table and CALLS the shipped
 * overprogram-duration function rather than restating either.
 *
 * Do NOT include an Arduino framework header here: this file follows
 * not_implemented.cpp's include discipline so it adds zero macro-redefinition
 * warnings on the native build. PROGMEM access comes transitively through
 * eprom_params.h.
 *
 * Keep this as its OWN translation unit. protocol_branch_inventory.json pins
 * the blob SHAs of eprom.cpp and eprom_params.cpp, so folding this arithmetic
 * into either turns that golden RED.
 */
#include "eprom_budget.h"
#include "eprom_params.h"
#include "eprom.h"

/*
 * BF-3(a): the pulse count ceils. See eprom_budget.h's declaration comment
 * for the full rule and the 49999/50000 worked example; the ceil below is
 * exactly `(energy_cap_us + pulse_us - 1) / pulse_us`, which mirrors the
 * shipped loop's "increment, then test" statement order (src/proms/
 * eprom.cpp's inner `for (;;)`) rather than a naive division.
 */
uint32_t eprom_worst_pulses(uint8_t max_pulses, uint32_t pulse_us, uint32_t energy_cap_us) {
    if (energy_cap_us == 0U || pulse_us == 0U) {
        /* energy_cap_us == 0: UNCAPPED (eprom_params.h) -- guarded exactly
         * as eprom.cpp's own pre-flight refusal guards it, with
         * `energy_cap_us > 0` rather than an unguarded compare.
         * pulse_us == 0: guard only: configure_eprom's fallback switch
         * always resolves a zero pulse_delay before this could ever be
         * called with the live value. Neither arm may divide. */
        return (uint32_t)max_pulses;
    }
    uint32_t by_energy = (energy_cap_us + pulse_us - 1U) / pulse_us;  /* ceil(C/P) -- BF-3 */
    return by_energy < (uint32_t)max_pulses ? by_energy : (uint32_t)max_pulses;
}

uint32_t eprom_per_byte_budget_us(uint8_t max_pulses, uint32_t pulse_us, uint32_t energy_cap_us,
                                   uint8_t overprogram_factor, uint32_t overprogram_cap_us) {
    uint32_t n = eprom_worst_pulses(max_pulses, pulse_us, energy_cap_us);
    /* The (uint8_t) narrowing below is safe by construction: n never
     * exceeds max_pulses (see eprom_worst_pulses above), and max_pulses is
     * itself a uint8_t. BF-3(b): CALL the shipped overprogram-duration
     * function with this pulse count -- never restate its
     * factor*pulse_count*pulse_us formula here. */
    return n * pulse_us + eprom_overprogram_us((uint8_t)n, pulse_us, overprogram_factor, overprogram_cap_us);
}

uint16_t eprom_block_budget_s(uint32_t protocol, uint32_t pulse_us, uint32_t block_bytes) {
    const eprom_params_t* row = eprom_params_for(protocol);
    if (row == NULL) {
        /* Non-EPROM protocol: advertise nothing, not "no time needed" --
         * see eprom_budget.h's returned-value contract. */
        return 0U;
    }

    /* block_bytes clamp: the ceil remainder term further below multiplies
     * a per-byte-time remainder (up to 999999) by block_bytes, and
     * 999999 * 4096 + 999999 = 4 096 995 903 is the largest such product
     * that still fits uint32_t (max 4 294 967 295). 4096 is also the host's
     * plausibility ceiling for an advertised buffer size, and the real
     * DATA_BUFFER_SIZE is 512 (Uno) or 1024 (Leonardo) -- so this clamp
     * never binds on any value this table could plausibly be asked about. */
    if (block_bytes > 4096UL) {
        block_bytes = 4096UL;
    }

    /* Every PROGMEM column read individually with pgm_read_* -- never a
     * struct dereference, which compiles and silently returns RAM garbage
     * on AVR (eprom_params.h's own PROGMEM contract). verify_mode and
     * vpp_path are not read here -- neither one is part of the time
     * budget. */
    uint8_t  max_pulses         = pgm_read_byte(&row->max_pulses);
    uint8_t  overprogram_factor = pgm_read_byte(&row->overprogram_factor);
    uint32_t energy_cap_us      = pgm_read_dword(&row->energy_cap_us);
    uint32_t overprogram_cap_us = pgm_read_dword(&row->overprogram_cap_us);

    uint32_t per_byte_us = eprom_per_byte_budget_us(max_pulses, pulse_us, energy_cap_us,
                                                     overprogram_factor, overprogram_cap_us);

    /* Divide BEFORE multiplying so the block-scale multiply below cannot
     * overflow uint32_t on its own -- only the remainder (< 1 000 000) is
     * ever multiplied by block_bytes, and the block_bytes clamp above
     * bounds that product. */
    uint32_t whole = per_byte_us / 1000000UL;
    uint32_t rem   = per_byte_us % 1000000UL;
    /* The "+ 999999UL" is the ceil: any nonzero remainder-seconds rounds
     * UP to the next whole second, matching the "ceil-rounded to whole
     * seconds first" padding rule stated in eprom_budget.h. */
    uint32_t raw_s = whole * block_bytes + (rem * block_bytes + 999999UL) / 1000000UL;

    /* "Twice the pulse-only worst case, plus two seconds". The "+ 2"
     * makes the one-second floor automatic, so no separate floor test is
     * needed anywhere in this budget. */
    uint32_t padded = raw_s * 2UL + 2UL;
    if (padded > 65535UL) {
        padded = 65535UL;
    }
    return (uint16_t)padded;
}
