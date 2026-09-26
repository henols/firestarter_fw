/*
 * Compatibility wrapper for legacy source files that still include
 * <avr/pgmspace.h> directly.
 */

#ifndef __RURP_AVR_PGMSPACE_COMPAT_H__
#define __RURP_AVR_PGMSPACE_COMPAT_H__

#if defined(__AVR__)
#include_next <avr/pgmspace.h>
#else
#include "../rurp_platform_compat.h"
#endif

#endif /* __RURP_AVR_PGMSPACE_COMPAT_H__ */
