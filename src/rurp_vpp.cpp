/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "rurp_vpp.h"

/*
 * The VPP control capability
 * seam's implementation. Dependency-free by construction: the only include
 * is rurp_vpp.h itself. Zero production callers today -- these
 * three bodies exist so the seam compiles and its guards can be proven to
 * fire; nothing under src/ or platform/ calls them yet.
 *
 * Fire-proof: tests/test_vpp_seam_manual_on_every_board.py.
 */

#if RURP_HAS_VPP_DAC
#error "RURP_HAS_VPP_DAC=1 selects a closed-loop VPP DAC implementation that this branch does not provide"
#endif

rurp_vpp_control_mode_t rurp_vpp_control_mode(void) {
    return RURP_VPP_CONTROL_MANUAL;
}

rurp_vpp_result_t rurp_set_vpp_target_mv(uint16_t target_mv, uint16_t tolerance_mv, uint16_t timeout_ms) {
    (void)target_mv;
    (void)tolerance_mv;
    (void)timeout_ms;
    return RURP_VPP_MANUAL_ADJUSTMENT_REQUIRED;
}

void rurp_disable_vpp_control(void) {
}
