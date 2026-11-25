#pragma once
/*
 * The Arduino analog-pin implementation of rurp_read_revision_adc10()
 * (declared in rurp_shield.h). Include it once, in the board backend that
 * includes rurp_register_utils.h. A board without Arduino analog pins (PY32)
 * defines the function in its own backend and does not include this file;
 * a board that has neither fails at link time.
 */
#ifdef HARDWARE_REVISION
#include <stdint.h>
#include <Arduino.h>
#include "rurp_pinout.h"
#include "rurp_shield.h"

/* Preserve AVR behavior: A3 averaged eight times; legacy A2 check once. */
bool rurp_read_revision_adc10(bool divider, uint16_t *out)
{
    const uint8_t pin = divider ? PIN_VPP_VOLTAGE_ADC : PIN_HW_REVISION_DETECT_ADC;
    pinMode(PIN_HW_REVISION_DETECT_ADC, INPUT);
    pinMode(PIN_VPP_VOLTAGE_ADC, INPUT);
    if (divider) *out = static_cast<uint16_t>(analogRead(pin));
    else {
        uint16_t sum = 0;
        for (uint8_t i = 0; i < 8; ++i) sum += static_cast<uint16_t>(analogRead(pin));
        *out = sum >> 3;
    }
    return true;
}
#endif
