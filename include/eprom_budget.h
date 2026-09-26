/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Per-block worst-case write-time budget arithmetic. Implementation in
 * src/proms/eprom_budget.cpp.
 *
 * Do NOT add an Arduino framework include here, or to anything this header
 * includes: pairing it with the platform PROGMEM shim emits 14
 * macro-redefinition warnings and the native build's warning watermark has
 * zero headroom. <stdint.h> only.
 */
#ifndef __EPROM_BUDGET_H__
#define __EPROM_BUDGET_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Worst-case pulse COUNT for one byte.
 *
 * energy_cap_us == 0 means UNCAPPED, not "cap at zero" — 0x07 and 0x08 both
 * ship 0, so an unguarded min() would clamp their every pulse to zero.
 *
 * The count is min(max_pulses, ceil(energy_cap_us / pulse_us)). The ceil is
 * load-bearing: the program loop adds to the accumulator BEFORE testing it,
 * so a byte can take one more pulse than a plain division suggests. At
 * pulse_us=49999 against energy_cap_us=50000 the loop needs two pulses
 * (99998 us total) where division implies one — a 2x under-estimate.
 *
 * pulse_us == 0 returns max_pulses rather than dividing by zero. Guard only;
 * configure_eprom's fallback switch has resolved a zero pulse_delay before
 * any ack is packed.
 */
uint32_t eprom_worst_pulses(uint8_t max_pulses, uint32_t pulse_us, uint32_t energy_cap_us);

/*
 * Worst-case per-byte program TIME in us: pulse count x pulse width, plus the
 * overprogram duration for that same count.
 *
 * Get the overprogram term by CALLING eprom_overprogram_us (eprom.h) — do not
 * restate its formula as `3 * overprogram_factor * pulse_us`. The 3 is
 * already the factor inside that function's contract, so restating it
 * under-estimates by 8.3x for any future row with a non-zero factor.
 */
uint32_t eprom_per_byte_budget_us(uint8_t max_pulses, uint32_t pulse_us, uint32_t energy_cap_us,
                                   uint8_t overprogram_factor, uint32_t overprogram_cap_us);

/*
 * Advertised per-block write-time budget in whole SECONDS, already padded:
 *
 *   padded_s = ceil(raw_pulse_only_us / 1e6) * 2 + 2
 *
 * The host cannot see this rule from the wire. It is a multiplier rather than
 * a constant because the fixed per-pulse overhead scales with pulse COUNT:
 * raw counts pulse widths only, while the real block also pays the 500 ms VPE
 * settle, a pre-pulse settle and verify strobe per pulse, shift-register
 * writes per address change, the final full-block verify on 0x07/0x08, and
 * ~41 ms of serial transport per 1024 B. The +2 gives a one-second floor, so
 * no separate clamp is needed.
 *
 * Returns 0 when the protocol has no eprom_params row. 0 means "advertise
 * nothing", NOT "no time needed" — the host then falls back to its own
 * default rather than treating the block as instantaneous.
 */
uint16_t eprom_block_budget_s(uint32_t protocol, uint32_t pulse_us, uint32_t block_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __EPROM_BUDGET_H__ */
