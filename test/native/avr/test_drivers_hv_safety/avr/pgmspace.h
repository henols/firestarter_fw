/*
 * host-side stub for <avr/pgmspace.h>
 *
 * The dispatch test runs on platform = native (no AVR libc available).
 * `rurp_shield.h` unconditionally `#include <avr/pgmspace.h>` to get the
 * `PROGMEM` storage attribute and `pgm_read_*` accessors. On host we just
 * neutralize these to no-ops / direct memory access so the test binary
 * can link.
 *
 * Scope: ONLY for the dispatch unit test. Not for production builds.
 * Production builds use the real AVR libc header via the Arduino framework.
 */
#ifndef _AVR_PGMSPACE_H_STUB_
#define _AVR_PGMSPACE_H_STUB_

#include <stdint.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef PSTR
#define PSTR(s) (s)
#endif

/* PGM_P is referenced by rurp_shield.h's rurp_log /
 * rurp_log_P prototypes. ArduinoFake provides its own arduino/pgmspace.h
 * later in the include order, which also defines PGM_P. Both definitions
 * resolve to `const char *`, so guard with #ifndef to avoid clashes. */
#ifndef PGM_P
#define PGM_P const char *
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(void**)(addr))
#endif

#ifndef strcpy_P
#define strcpy_P(dst, src) strcpy((dst), (src))
#endif

#ifndef strlen_P
#define strlen_P(s) strlen((s))
#endif

#ifndef memcpy_P
#define memcpy_P(dst, src, n) memcpy((dst), (src), (n))
#endif

#endif /* _AVR_PGMSPACE_H_STUB_ */
