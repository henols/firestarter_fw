/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __RURP_TYPES_H__
#define __RURP_TYPES_H__

#include <stdint.h>

#ifndef HARDWARE_REVISION
#define rurp_register_t uint8_t
#else
#define rurp_register_t uint16_t
#endif

typedef struct rurp_configuration {
    char version[6];
    long r1;
    long r2;
    uint8_t hardware_revision;
    /*
     * The measured internal bandgap of THIS MCU, in millivolts.
     *
     * Appended at the end, never inserted: the ARM dual-slot record embeds
     * this struct byte-for-byte and validates by length + CRC32, so growing
     * it makes an old record fail validation and fall back to defaults, which
     * is the designed recovery path. On AVR the `version` string still
     * discriminates at the same EEPROM offset.
     *
     * Defaults to RURP_BANDGAP_NOMINAL_MV, which reproduces the pre-
     * calibration arithmetic exactly, so an uncalibrated board behaves as it
     * always did.
     */
    uint16_t bandgap_mv;
} rurp_configuration_t;

#endif // __RURP_TYPES_H__