/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The Arduino analog-pin implementation of rurp_read_revision_adc10()
 * (declared in rurp_shield.h). PlatformIO compiles it for the AVR boards.
 * The PY32 build does not list this file; its board backend defines the
 * function.
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
