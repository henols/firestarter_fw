/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * rurp_pinout.h — canonical CTRL_* / PIN_* / RES_* / JMP_* alias substrate
 * for the silkscreen-label → code-alias migration.
 *
 * This header introduces ONLY the new canonical declarations. No
 * backward-compat alias block is included (no `#define <old_name>
 * <new_name>` lines). The old #defines in rurp_shield.h:25-94 remain in
 * place during Wave 1 and serve all existing call-sites; Waves 2 and 3
 * migrate call-sites individually and Wave 3's final task atomically
 * deletes the rurp_shield.h:25-94 block.
 *
 * The #ifndef HARDWARE_REVISION / #else / #endif structure mirrors
 * rurp_shield.h:24-94 VERBATIM (Pitfalls 1 + 2):
 *   - Legacy branch: CTRL_ADDRESS_LINE_16 == CTRL_VPP_VPE_DROP_ENABLE
 *     (macro-alias-as-macro, NOT duplicate hex)
 *   - HARDWARE_REVISION branch: CTRL_VPP_VPE_DROP_ENABLE == 0x100
 *     (NOT 0x01 — value differs from legacy)
 */

#ifndef __RURP_PINOUT_H__
#define __RURP_PINOUT_H__

// NOTE: this header intentionally does NOT include <Arduino.h>. The `A2`/`A3`
// pin macros below expand to plain integer literals from Arduino's
// pins_arduino.h, which every consumer pulls via <Arduino.h> in their own TU.
// Including <Arduino.h> here would force C++ classes (String, etc.) through
// the extern-C wrappers of upstream headers like firestarter.h / flash_utils.h
// — a latent bracketing bug uncovered when rurp_shield.h started including
// rurp_pinout.h.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Section 1: Arduino-pin assignments (PIN_*) ------------------------

#define PIN_VPP_VOLTAGE_ADC A2

#ifdef HARDWARE_REVISION
#define PIN_HW_REVISION_DETECT_ADC A3
#endif

// ---- Section 1b: ADC voltage-band thresholds (HARDWARE_REVISION-gated) -------
// Consumed by rurp_detect_hardware_revision() in rurp_hw_rev_utils.h.
// Thresholds UNCHANGED — bench measurement (2026-05-26) validated 0/15 reads
// in the [200, 220) guard gap across
// 3 shield revisions; the existing 20-count gap is empirically sufficient.
// SEMANTIC NOTE, since the INPUT high-Z fix: bands now characterize
// A3-net composition (R41-only-to-GND = low; external-pull-up-to-+5V = mid;
// floating = high), NOT R41 value alone. The internal pull-up R_top the
// original band-math assumed (INPUT_PULLUP) is now disabled — R41 value no
// longer drives ADC variance. A future Rev 2.4 PCB could add an external
// R_top to restore the original divider semantics.
// #define (NOT constexpr) — preprocessor constants resolve
// at compile time and contribute 0 B to the .hex until referenced.
#ifdef HARDWARE_REVISION
#define ADC_BAND_R41_4K7_HIGH 200  // upper edge of low band (R41-only-to-GND; Rev 2.0/2.1/2.2 + Rev 2.3 stock)
#define ADC_BAND_R41_10K_LOW  220  // lower edge of mid band (external pull-up active — operator-reworked boards); [200, 220) -> REVISION_UNKNOWN guard gap
#define ADC_BAND_R41_10K_HIGH 600  // upper edge of mid band; above -> high band / floating / no R41
#endif

// ---- Section 2: Control-register bits (CTRL_*) -------------------------
// Mirrors rurp_shield.h:24-53 VERBATIM under new names.

#ifndef HARDWARE_REVISION
#define CTRL_VPP_VPE_DROP_ENABLE      0x01
#define CTRL_ADDRESS_LINE_16          CTRL_VPP_VPE_DROP_ENABLE
#define CTRL_VPP_A9_ENABLE            0x02
#define CTRL_VPE_ENABLE               0x04
#define CTRL_VPP_P1_ENABLE            0x08
#define CTRL_ADDRESS_LINE_17          0x10
#define CTRL_ADDRESS_LINE_18          0x20
#define CTRL_READ_WRITE               0x40
#define CTRL_VPP_REGULATOR_ENABLE     0x80

#else
// HARDWARE_REVISION wide layout — same names, distinct values for the
// ADDRESS_LINE_16 / VPP_VPE_DROP_ENABLE pair (Pitfall 2).
#define CTRL_ADDRESS_LINE_16          0x01
#define CTRL_VPP_A9_ENABLE            0x02
#define CTRL_VPE_ENABLE               0x04
#define CTRL_VPP_P1_ENABLE            0x08
#define CTRL_ADDRESS_LINE_17          0x10
#define CTRL_ADDRESS_LINE_18          0x20
#define CTRL_READ_WRITE               0x40
#define CTRL_VPP_REGULATOR_ENABLE     0x80
#define CTRL_VPP_VPE_DROP_ENABLE      0x100
#endif

// ---- Section 2b: EPROM high-voltage composite masks (EPROM_HV_*) -------
// Defined beside the CTRL_* bits they are built from so they cannot drift.
//
//   - LOGICAL masks. The physical effect is produced by
//     rurp_map_ctrl_reg_for_hardware_revision() at the register write, so this
//     header states intent, not the wire pattern.
//   - The EPROM_ prefix is deliberate: the native warning watermark has zero
//     headroom and every recorded warning is a macro redefinition, so a generic
//     name risks colliding with an ArduinoFake or pgmspace macro.
//   - On the legacy (!HARDWARE_REVISION) arm CTRL_VPP_VPE_DROP_ENABLE IS
//     CTRL_ADDRESS_LINE_16 -- a real alias. Both composites therefore also
//     clear A16 there. Harmless: they are only ever used to DISABLE the route.
//   - CTRL_VPP_P1_ENABLE is DELIBERATELY ABSENT from EPROM_HV_ALL_OFF_MASK.
//     eprom_internal_set_control_register() strips CTRL_VPE_ENABLE and
//     substitutes P1 when using_p1_as_vpp(handle) holds; naming BOTH bits would
//     leave the physical VPE line never cleared on that path. On Rev 2-class,
//     A18 and P1 collapse onto the same physical bit anyway.
//   - A PRESERVE/HOLD mask cannot be a #define: its drop-bit membership depends
//     on a runtime revision read and on handle->pins. That stays inside
//     mem_util_calculate_top_address_register.
//   - #define, not constexpr: a composite nothing references costs 0 B.
#define EPROM_HV_ROUTE_MASK    (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE)
#define EPROM_HV_ALL_OFF_MASK  (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE)

#define RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV 17380

#define CTRL_ADDRESS_LINE_13          0x20  // reserved — no current call-site

// ---- Section 3: Per-rev variants (CTRL_*_REV1 / CTRL_*_REV2) -----------
// Mirrors rurp_shield.h:70-94 VERBATIM with suffix-family rename per
// RESEARCH "State of the Art" deprecation table.

#ifdef HARDWARE_REVISION
// REV 1
#define CTRL_VPP_VPE_DROP_ENABLE_REV1      0x01
#define CTRL_VPP_A9_ENABLE_REV1            0x02
#define CTRL_VPE_ENABLE_REV1               0x04
#define CTRL_VPP_P1_ENABLE_REV1            0x08
#define CTRL_ADDRESS_LINE_17_REV1          0x10
#define CTRL_ADDRESS_LINE_18_REV1          0x20
#define CTRL_READ_WRITE_REV1               0x40
#define CTRL_VPP_REGULATOR_ENABLE_REV1     0x80

#define CTRL_ADDRESS_LINE_16_REV1          CTRL_VPP_VPE_DROP_ENABLE_REV1

// REV 2
#define CTRL_VPP_VPE_DROP_ENABLE_REV2      0x01
#define CTRL_VPP_A9_ENABLE_REV2            0x02
#define CTRL_VPE_ENABLE_REV2               0x04
#define CTRL_VPP_P1_ENABLE_REV2            0x08
#define CTRL_ADDRESS_LINE_17_REV2          0x10
#define CTRL_ADDRESS_LINE_16_REV2          0x20
#define CTRL_READ_WRITE_REV2               0x40
#define CTRL_VPP_REGULATOR_ENABLE_REV2     0x80

#define CTRL_ADDRESS_LINE_18_REV2          CTRL_VPP_P1_ENABLE_REV2
#endif

#ifdef __cplusplus
}
#endif

#endif // __RURP_PINOUT_H__
