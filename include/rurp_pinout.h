/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * rurp_pinout.h — canonical CTRL_* / PIN_* / ADC_BAND_* / RURP_VPP_* definitions
 * for the RURP shield.
 *
 * The #ifndef HARDWARE_REVISION / #else / #endif structure below selects the
 * control-register layout:
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

// ADC hardware-revision bands use A3 with its internal pull-up disabled:
// R41-only-to-GND is low, an external pull-up is mid, and floating is high.
// The [200, 220) guard gap had no samples in measurements across three
// shield revisions; the band identifies the A3 net, not R41 alone.
#ifdef HARDWARE_REVISION
#define ADC_BAND_R41_4K7_HIGH 200  // upper edge of low band (R41-only-to-GND; Rev 2.0/2.1/2.2 + Rev 2.3 stock)
#define ADC_BAND_R41_10K_LOW  220  // lower edge of mid band (external pull-up active — operator-reworked boards); [200, 220) -> REVISION_UNKNOWN guard gap
#define ADC_BAND_R41_10K_HIGH 600  // upper edge of mid band; above -> high band / floating / no R41
#endif

// ---- Section 2: Control-register bits (CTRL_*) -------------------------
// Public logical names for RURP control-register bits.

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
// Hardware-revision layout separates ADDRESS_LINE_16 and the drop-enable bit.
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

// Bus lines 0-15 go to the LSB and MSB address latches. Bus line
// RURP_CTRL_ADDRESS_SHIFT + n is control-register bit n.
#define RURP_CTRL_ADDRESS_SHIFT       16

// The physical control-register bits that switch a VPP route onto the socket.
// They have the same positions on every revision.
#define RURP_SOCKET_VPP_ROUTE_BITS    (CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_P1_ENABLE)

// ---- Section 2b: EPROM high-voltage composite masks (EPROM_HV_*) -------
// Defined beside the CTRL_* bits they are built from so they cannot drift.
//
//   - LOGICAL masks. The physical effect is produced by
//     rurp_map_ctrl_reg_for_hardware_revision() at the register write, so this
//     header states intent, not the wire pattern.
//   - EPROM_ avoids collisions with platform and test macros.
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
#define EPROM_HV_ROUTE_MASK    (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE)
#define EPROM_HV_ALL_OFF_MASK  (CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_VPE_DROP_ENABLE | CTRL_VPP_A9_ENABLE | CTRL_VPE_ENABLE)

#define RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV 17380

/*
 * The VPP acceptance window, as a fraction of the row's target.
 *
 * Both bounds are sized against the measured post-calibration uncertainty of
 * this instrument (three boards, three shield revisions):
 *
 *   divider assumed nominal, measured 7.1974 vs 7.1364   0.85 %
 *   divider spread across boards (rails-off 136..138)    0.73 %
 *   calibration quantisation, one bandgap count          0.48 %
 *   operator meter accuracy, 3.5-digit DMM               0.50 %
 *   bandgap read jitter, +/-1 count                      0.45 %
 *   divider read quantisation, 1 count of ~341 at 12 V   0.29 %
 *   ------------------------------------------------------------
 *   RSS (typical) 1.42 %        linear sum (worst case) 3.30 %
 *
 * HIGH is a relative headroom with an absolute floor. It was a flat 500 mV,
 * which is +4.2 % at a 12000 mV target but only +2.0 % at 25000 -- tighter
 * than the instrument can resolve, potentially rejecting a correctly set
 * rail. The absolute floor protects lower-voltage parts.
 *
 * LOW stays at 5 %, comfortably outside the worst case, so it raises no false
 * warning and can now see a real shortfall -- which before calibration it
 * could not when uncalibrated readings exceeded the tolerance window.
 *
 * Neither figure is used for ROUTING. RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV
 * above decides the route from path capability, never from a reading.
 * These two values affect verification only.
 */
#define RURP_VPP_HIGH_HEADROOM_PCT 3
#define RURP_VPP_HIGH_HEADROOM_FLOOR_MV 500
#define RURP_VPP_LOW_TOLERANCE_PCT 5

// ---- Section 3: Per-rev variants (CTRL_*_REV1 / CTRL_*_REV2) -----------

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
