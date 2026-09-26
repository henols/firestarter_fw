/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __PROTO_CONSTANTS_H__
#define __PROTO_CONSTANTS_H__

#define PROTO_FLASH_5V_PAGE 0x05
#define PROTO_FLASH_NOR_UNLOCK 0x06
#define PROTO_EPROM_28PIN 0x07
#define PROTO_EPROM_32PIN 0x08
#define PROTO_EPROM_24PIN 0x0B
#define PROTO_EEPROM_PARALLEL 0x0D
#define PROTO_SRAM_32PIN 0x0E
#define PROTO_FLASH_INTEL 0x10
#define PROTO_SRAM_24PIN 0x27
#define PROTO_SRAM_28PIN 0x28
#define PROTO_SRAM_32PIN_NVRAM 0x29
#define PROTO_EEPROM_8051BUS 0x34

// Phantom entries (operator-approved honest non-protocols):
// 0x35 = IC2_ALG_ITE (an ITE EC microcontroller label in minipro, NOT a
// memory algorithm); 0x39 = no IC2_ALG constant exists at all. Both have
// zero DB chips — firmware dispatch is preserved only for forward-compat.
// The `0x35`/`0x39` substrings are literal identifier text (valid C
// identifier chars), not hex literals; do NOT "fix" the spelling to
// PROTO_PHANTOM_35 / PROTO_PHANTOM_39.
#define PROTO_PHANTOM_0x35 0x35
#define PROTO_PHANTOM_0x39 0x39

#endif  // __PROTO_CONSTANTS_H__
