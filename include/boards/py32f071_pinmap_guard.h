#pragma once

/*
 * PY32F071 pin-map "configured for a real build" guard.
 *
 * This file includes NOTHING, deliberately. py32f071_rurp_shield.h pulls in
 * PY32 HAL headers that no local toolchain here can resolve, so hoisting the
 * guard into a dependency-free fragment is what lets a plain host preprocessor
 * evaluate it standalone -- which is what makes it testable at all.
 *
 * It only TESTS RURP_PY32F071_PINMAP_CONFIGURED; the ARM build's
 * target_compile_definitions supplies it. A build that forgets now fails at
 * the preprocessor instead of silently compiling an unconfigured, provisional
 * pin map that could energise a PROM.
 *
 * `!defined(X) || !X` rather than a bare `#if !X`: the explicit defined() test
 * states the "unset must fail" intent and survives a later -Wundef.
 *
 * tests/test_pinmap_guard_fires.py preprocesses this file standalone across
 * three arms -- unset, =1, =0 -- and asserts the exit codes and error text.
 */

#if !defined(RURP_PY32F071_PINMAP_CONFIGURED) || !RURP_PY32F071_PINMAP_CONFIGURED
#error "RURP_PY32F071_PINMAP_CONFIGURED is not set: the PY32F071 Firestarter wiring is not configured for this build. This macro must be supplied by the build system (platform/py32f071/CMakeLists.txt's target_compile_definitions), not by this header."
#endif
