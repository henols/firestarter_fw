#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifndef RURP_MICROPETAL_ROM_TOUCH
#define RURP_MICROPETAL_ROM_TOUCH 0
#endif
#if RURP_MICROPETAL_ROM_TOUCH && !defined(RURP_BOARD_MICROPETAL_UNO)
#error "ROM USB touch is an opt-in MicroPetal experiment"
#endif

/* CDC callbacks run in IRQ context. Request only; main performs the handoff.
 * Require 1200 baud plus DTR high->low, not an ordinary close at 250000 baud.
 */
typedef struct {
    bool armed;
    bool saw_dtr;
    bool pending;
} rurp_rom_touch_state_t;
static inline void rurp_rom_touch_rate(volatile rurp_rom_touch_state_t *s,
                                      uint32_t rate, bool dtr) {
    s->armed = rate == 1200U;
    s->saw_dtr = s->armed && dtr;
}
static inline void rurp_rom_touch_dtr(volatile rurp_rom_touch_state_t *s, bool dtr) {
    if (s->armed && s->saw_dtr && !dtr) {
        s->pending = true;
        s->armed = false;
    }
    if (s->armed && dtr) s->saw_dtr = true;
}
#ifdef __cplusplus
extern "C" {
#endif
bool py32_usb_take_rom_request(void);
void rurp_rom_boot_poll(void);
#ifdef __cplusplus
}
#endif
