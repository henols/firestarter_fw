#pragma once

/*
 * VPP control capability seam: declares whether a board has a VPP DAC or is
 * adjusted by hand, and nothing else. Bodies (refusal-shaped) live in
 * src/rurp_vpp.cpp.
 *
 * Dependency-free by design -- <stdint.h> only. Anything wanting configuration
 * or register access must add that dependency deliberately rather than inherit
 * it here.
 *
 * For non-AVR targets RURP_HAS_VPP_DAC is supplied ONLY by the board/platform
 * build; this header never defines it, only tests it. A board that forgets to
 * supply it fails at the preprocessor rather than silently inheriting manual
 * control near an unregulated rail.
 *
 * PERMANENT, not provisional: no Arduino-class RURP board carries a VPP DAC
 * and none will -- the rail is set by the operator's potentiometer.
 *
 * Uses __AVR__ rather than the tree's RURP_PLATFORM_AVR deliberately. __AVR__
 * is compiler-supplied and unconditional, which keeps this header
 * dependency-free. RURP_PLATFORM_AVR is derived from __AVR__ anyway, lives in
 * a header no AVR build includes, and that header carries a terminal #error
 * whose native escape arm names a macro defined nowhere. Do not "improve" the
 * predicate below to use it.
 */

#include <stdint.h>

#if !defined(RURP_HAS_VPP_DAC)
#  if defined(__AVR__)
/* Permanent, not provisional: no Arduino/AVR-class RURP board carries a
 * VPP DAC -- the rail is set by the operator's pot. Operator, 2026-07-31. */
#    define RURP_HAS_VPP_DAC 0
#  else
#    error "RURP_HAS_VPP_DAC must be supplied by the board/platform build (for py32f071: platform/py32f071/CMakeLists.txt's target_compile_definitions), not by this header."
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RURP_VPP_CONTROL_MANUAL = 0,
    RURP_VPP_CONTROL_DAC = 1,
} rurp_vpp_control_mode_t;

typedef enum {
    RURP_VPP_OK = 0,
    RURP_VPP_MANUAL_ADJUSTMENT_REQUIRED = 1,
} rurp_vpp_result_t;

rurp_vpp_control_mode_t rurp_vpp_control_mode(void);
rurp_vpp_result_t rurp_set_vpp_target_mv(uint16_t target_mv, uint16_t tolerance_mv, uint16_t timeout_ms);
void rurp_disable_vpp_control(void);

#ifdef __cplusplus
}
#endif
