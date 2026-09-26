/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * eprom_params_t: a const, protocol_id-keyed parameter table for the three
 * 27C protocols (0x07/0x08/0x0B). Type, enums and accessor only; the PROGMEM
 * storage lives in src/proms/eprom_params.cpp.
 *
 * Do NOT add an Arduino framework include here or to anything this includes:
 * pairing it with the PROGMEM shim emits 14 macro-redefinition warnings, and
 * the native warning watermark has zero headroom.
 *
 * Fields are ordered largest-first, deliberately: the AVR toolchain gives every
 * type 1-byte struct alignment and a 64-bit host does not, and this order is
 * what keeps sizeof() == 12 on both.
 *
 * NO PULSE-WIDTH COLUMN. Pulse width is handle->pulse_delay; the per-protocol
 * fallbacks stay in configure_eprom's switch and are not duplicated here.
 *
 * Per-cell datasheet attribution lives in the gate-enforced sidecar
 * tests/golden/eprom_params_citations.json, not here.
 */
#ifndef __EPROM_PARAMS_H__
#define __EPROM_PARAMS_H__

#include <stdint.h>
#include "rurp_platform_compat.h" /* PROGMEM + pgm_read_* on AVR and host alike */

/* verify_mode: WHEN to verify, never at what VCC. The datasheets'
 * raised-VCC verify margin is unreachable on this shield's ~6.25V ceiling,
 * so no value in this column may ever encode a verify VCC. */
enum { VERIFY_PER_PULSE = 0, VERIFY_PER_PULSE_PLUS_FINAL = 1 };

/* vpp_path names an ABSTRACT route, not a control-register bitmask -- the
 * mask sets are owned by the write path, and naming a mask here would force
 * this dependency-free header to pull in the shield's register header. */
enum { VPP_PATH_DROP_RESISTOR = 0, VPP_PATH_DIRECT_VPE = 1 };

/* Six columns, largest-first (see (b) above). No pulse-width field of any
 * name exists here -- no pulse_delay, no pulse_width_us, no
 * fallback_pulse_us. */
typedef struct {
    uint32_t overprogram_cap_us;  /* clamp for min(3 x overprogram_factor x pulse, cap) */
    uint32_t energy_cap_us;       /* accumulated per-byte program-time budget; 0 = uncapped */
    uint8_t  max_pulses;          /* structural retry/backstop ceiling */
    uint8_t  overprogram_factor;  /* margin-pulse multiplier; 0 = no overprogram */
    uint8_t  verify_mode;         /* VERIFY_PER_PULSE / VERIFY_PER_PULSE_PLUS_FINAL */
    uint8_t  vpp_path;            /* VPP_PATH_DROP_RESISTOR / VPP_PATH_DIRECT_VPE */
} eprom_params_t;

/* Compile-time sizeof check only -- NOT the rejected __attribute__((used)) / force-the-table-into-the-image pattern; do not delete this thinking it is one. */
#ifdef __cplusplus
static_assert(sizeof(eprom_params_t) == 12,
              "sizeof(eprom_params_t) must be 12 on every target -- Pitfall 2: "
              "field order is largest-first because avr-gcc gives every type "
              "1-byte alignment while a 64-bit host does not.");
#endif

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Linear-scans the protocol_id-keyed table and returns a POINTER INTO
 * PROGMEM -- every field must be read back with pgm_read_byte /
 * pgm_read_dword, never dereferenced directly (a direct read compiles and
 * silently returns RAM garbage on AVR). Returns NULL when no row matches
 * `protocol`, so an unrecognised value fails closed with zero hardware
 * side effects; it never returns a default row.
 */
const eprom_params_t* eprom_params_for(uint32_t protocol);
#ifdef __cplusplus
}
#endif

#endif /* __EPROM_PARAMS_H__ */
