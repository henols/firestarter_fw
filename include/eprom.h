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
     * Overprogram (margin) pulse duration.
     *
     * The product is computed in uint32_t throughout: the worst named case
     * (factor=3, count=25, pulse_us=65535) is 4,915,125, which fits uint32_t
     * but overflows any uint16_t intermediate.
     *
     * cap_us == 0 yields 0, meaning "no clamp configured" — NOT "uncapped".
     * Reading it as uncapped would let the worst case above emit a
     * multi-second VPE pulse.
     */
    uint32_t eprom_overprogram_us(uint8_t pulse_count, uint32_t pulse_us, uint8_t factor, uint32_t cap_us);

    /*
     * Which high-voltage route to assert (direct-VPE vs drop-resistor),
     * from the eprom_params vpp_path column, though not from that column
     * alone: a required voltage above RURP_VPP_DROP_PATH_MAX_DELIVERABLE_MV
     * (rurp_pinout.h) also selects the direct-VPE path. FLAG_VPE_AS_VPP
     * forces the direct-VPE path regardless of what the table says. Returns
     * a fail-closed mask when the protocol has no row.
     */
    rurp_register_t eprom_hv_route_mask(firestarter_handle_t* handle);

    /* Intra-block progress cadence. The emission it paces is compiled out
     * entirely on uno/uno328pb — see eprom.cpp for why. */
    #define EPROM_PROGRESS_EMIT_INTERVAL_MS 1000

    /*
     * Compiles the overprogram call site in (1) or out (0).
     *
     * DO NOT "TIDY" THIS AWAY, AND DO NOT MAKE IT UNIFORM ACROSS TARGETS.
     * platformio.ini sets it to 0 for leonardo ONLY, to buy flash. The
     * ATmega32U4 has a bootloader cliff at 28672 B, and platformio.ini
     * raises the linker's reported ceiling to the chip's real 32768 B — so
     * NOTHING warns if the build grows past the cliff. It simply links over
     * the USB bootloader and the board loses bootloader entry.
     *
     * The difference is unobservable today: no chip_database.json row sets
     * overprogram_factor, so eprom_overprogram_us returns 0 and no margin
     * pulse is emitted on any target. Adding a row with a non-zero factor
     * makes uno/uno328pb and leonardo genuinely diverge — revisit this
     * define first and re-measure leonardo's headroom before setting it
     * back to 1.
     */
    #ifndef EPROM_OVERPROGRAM_SUPPORTED
    #define EPROM_OVERPROGRAM_SUPPORTED 1
    #endif

    /*
     * Settle either side of the program-voltage route assert wrapping every
     * program pulse.
     *
     * MUST be per pulse, not once per block. On this family OE/VPP and the
     * program voltage share one pin: the W27C512 operating-mode table enters
     * Program with CE=VIL and OE/VPP at 12 V, but Program Verify requires
     * OE/VPP LOW. So the route has to come down for the loop's verify read
     * and back up for the next pulse. Asserting once per block leaves the
     * part in Output Disable for every verify and the loop never converges.
     *
     * Datasheet floors (W27C512 AC programming characteristics): TOES
     * ("OE/VPP setup") 2.0 us min, TVS ("OE/VPP valid after CE high") 2.0 us
     * min. These values are far above them because the RURP shield's own VPE
     * switch rise time is uncharacterised.
     *
     * UPPER BOUND, and it is hard: at per-pulse granularity anything above
     * ~7 ms costs 1024 x that per block and turns a working write into a
     * host transport timeout. Also stay under the AVR delayMicroseconds()
     * ceiling of 16383 us.
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
     * for W27C512) and using it emits an erase pulse ~950x below the
     * datasheet minimum, which partially erases the part and then reports as
     * four unrelated `dev test` failures.
     *
     * W27C512 T_PWE is 95 ms min / 100 ms typ / 105 ms max. The MAXIMUM is
     * real: over-erasing a flotox cell drives it toward depletion mode, so
     * do not raise this "for margin". 100 ms is the centred typical, and
     * test_vpp_eprom_v131.cpp asserts both bounds against the measured
     * CE-low interval.
     *
     * In microseconds deliberately, so it routes through mem_util_delay_us —
     * 100000 us is far above the AVR delayMicroseconds() 16383 us ceiling.
     */
    #define EPROM_ERASE_PULSE_US 100000UL

#ifdef __cplusplus
}
#endif
#endif // __EPROM_H__
