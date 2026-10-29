/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __EPROM_H__
#define __EPROM_H__

#include "firestarter.h"
#ifdef __cplusplus
extern "C" {
#endif

    void configure_eprom(firestarter_handle_t* handle);

    /*
     * Which high-voltage route to assert (direct-VPE vs drop-resistor),
     * from the eprom_params vpp_path column, though not from that column
     * alone: a required voltage above RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV
     * (rurp_pinout.h) also selects the direct-VPE path. FLAG_VPE_AS_VPP
     * forces the direct-VPE path regardless of what the table says. Returns
     * a fail-closed mask when the protocol has no row.
     */
    rurp_register_t eprom_hv_route_mask(firestarter_handle_t* handle);

    /* Widest pulse configure_eprom accepts, in microseconds. A wider pulse is
     * refused, never clamped. */
    #define EPROM_PULSE_MAX_US 65535UL

    /* Intra-block progress cadence. The emission it paces is compiled out
     * entirely on uno/uno328pb — see eprom.cpp for why. */
    #define EPROM_PROGRESS_EMIT_INTERVAL_MS 1000

    /*
     * Settle either side of the program-voltage route assert wrapping every
     * pulse pass.
     *
     * The route must come down for every verify read. On this family OE/VPP
     * and the program voltage share one pin: the W27C512 operating-mode table
     * enters Program with CE=VIL and OE/VPP at 12 V, but Program Verify
     * requires OE/VPP LOW. So the route comes down for each scan pass and goes
     * back up for the next pulse pass. Asserting once per block leaves the part
     * in Output Disable for every verify and the loop never converges.
     *
     * Datasheet floors (W27C512 AC programming characteristics): TOES
     * ("OE/VPP setup") 2.0 us min, TVS ("OE/VPP valid after CE high") 2.0 us
     * min. These values are far above them because the RURP shield's own VPE
     * switch rise time is uncharacterised.
     *
     * Upper bound: the setup is paid once per pulse pass, and a large value
     * lengthens every block of a write until the host transport times out.
     * Also stay under the AVR delayMicroseconds() ceiling of 16383 us.
     *
     * Diagnosing a bad value: too small and every pulse is ineffective,
     * surfacing as MSG_ERR_MAX_PULSES. MARGINAL is nastier — it passes the
     * per-pulse verify on a weakly programmed cell and only fails later, as
     * MSG_ERR_VERIFY from the full-array pass. Raise these before concluding
     * a part is worn.
     */
    #define EPROM_VPP_SETUP_US 1000
    #define EPROM_VPP_HOLD_US  100

    /*
     * CE pulse width for eprom_internal_erase, spent with OE/VPP and A9 both
     * at VPE.
     *
     * This is the ERASE pulse, not the program pulse. Do not substitute
     * handle->pulse_delay here: that is the per-byte program width (100 us
     * for W27C512) and using it emits an erase pulse far below the
     * datasheet minimum, which partially erases the part.
     *
     * W27C512 T_PWE is 95 ms min / 100 ms typ / 105 ms max. The MAXIMUM is
     * real: over-erasing a flotox cell drives it toward depletion mode, so
     * do not raise this "for margin". 100 ms is the centred typical.
     *
     * In microseconds deliberately, so it routes through mem_util_delay_us —
     * 100000 us is far above the AVR delayMicroseconds() 16383 us ceiling.
     */
    #define EPROM_ERASE_PULSE_US 100000UL

#ifdef __cplusplus
}
#endif
#endif // __EPROM_H__
