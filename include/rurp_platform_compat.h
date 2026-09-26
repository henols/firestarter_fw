/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __RURP_PLATFORM_COMPAT_H__
#define __RURP_PLATFORM_COMPAT_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 * AVR keeps selected constants in a separate program-memory address space.
 * Unified-address-space targets use direct accesses for the same declarations.
 */
#if defined(__AVR__)
#include <avr/pgmspace.h>
#else

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef PGM_P
#define PGM_P const char*
#endif

#ifndef PSTR
#define PSTR(value) (value)
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(address) (*(const uint8_t*)(address))
#endif

#ifndef pgm_read_word
#define pgm_read_word(address) (*(const uint16_t*)(address))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(address) (*(const uint32_t*)(address))
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(address) (*(const void* const*)(address))
#endif

#ifndef memcpy_P
#define memcpy_P(destination, source, size) memcpy((destination), (source), (size))
#endif

#ifndef strcpy_P
#define strcpy_P(destination, source) strcpy((destination), (source))
#endif

#ifndef strncpy_P
#define strncpy_P(destination, source, size) strncpy((destination), (source), (size))
#endif

#ifndef strlen_P
#define strlen_P(value) strlen(value)
#endif

#ifndef strcmp_P
#define strcmp_P(left, right) strcmp((left), (right))
#endif

#ifndef strncmp_P
#define strncmp_P(left, right, size) strncmp((left), (right), (size))
#endif

#ifndef sprintf_P
#define sprintf_P sprintf
#endif

/* Arduino cores provide their own F() definition. Native builds do not. */
#if !defined(ARDUINO) && !defined(F)
#define F(value) (value)
#endif

#endif /* __AVR__ */

#endif /* __RURP_PLATFORM_COMPAT_H__ */
