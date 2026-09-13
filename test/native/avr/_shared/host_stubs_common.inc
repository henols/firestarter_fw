/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Shared host-stub implementations for the [env:native] test suites.
 *
 * Phase 6 WR-06: this file consolidates the ~120 lines of `rurp_*` no-op
 * stubs, `LOG_*_MSG` PROGMEM strings, and the `Serial_::operator bool()`
 * link-only definition that previously lived (duplicated) in every per-suite
 * `host_stubs.cpp`. When a new `rurp_*` symbol is added to the firmware,
 * this is now the single edit point — suite-specific `host_stubs.cpp` files
 * only carry their unique mock state.
 *
 * Inclusion model: this file is `.inc`, not `.cpp` — each suite's
 * `host_stubs.cpp` `#include`s it once with a relative path. There is no
 * shared-TU link step, so multiple test binaries compile their own copy
 * (which is exactly what we want: each binary is a single statically-linked
 * artifact and never sees a sibling suite's stubs).
 *
 * Per-suite opt-outs (define BEFORE including this file):
 *   - HOST_STUBS_CUSTOM_VOLTAGE_MV   — suite provides its own
 *                                      rurp_read_voltage_mv (e.g.,
 *                                      test_flash_intel_vpp mocks it).
 *   - HOST_STUBS_CUSTOM_HW_REVISION  — suite provides its own
 *                                      rurp_get_hardware_revision (e.g.,
 *                                      test_flash_intel_vpp mocks it).
 *   - HOST_STUBS_CUSTOM_READ_DATA_BUFFER — suite provides its own stateful
 *                                      rurp_read_data_buffer that models chip
 *                                      read-back (e.g., test_trace_eprom_v131
 *                                      converges the write-verify loop).
 *
 * Scope: only compiled into [env:native] via PIO's automatic discovery of
 * files under test/. Production builds (env:uno, env:leonardo) never see
 * this file because their src_filter excludes test/.
 *
 * Behavioural contract: every stub here is intentionally a no-op (or
 * returns 0). The dispatch tests only verify that configure_memory()
 * routes to the correct configure_*() function — they never assert on
 * hardware-register side effects. If a future test starts caring about
 * register writes, the stubs can grow to record calls; today they are
 * deliberately minimal.
 */

/* rurp_shield register I/O — the dispatch path's only direct hardware use
 * is mem_util_set_address(handle, 0) -> rurp_write_to_register(...). Stub it
 * so the call resolves and returns.
 *
 * Recording bus stub — Phase 71 HARN-01 / D-04.
 * Define HOST_STUBS_RECORD_BUS before including this file to activate the
 * recording version. Existing suites (test_dispatch, test_cobs_*,
 * test_read_timing, test_messages, test_data_input, test_not_implemented)
 * MUST NOT define this flag — flag off = today's no-op
 * behavior is preserved byte-exactly. This is an opt-IN guard (inverse of
 * the opt-OUT HOST_STUBS_CUSTOM_VOLTAGE_MV / HOST_STUBS_CUSTOM_HW_REVISION
 * guards above). */

/* Ordered strobe recorder (flag: HOST_STUBS_REAL_REGISTER_UTILS) — Phase 116 TRACE-01 / D-05 / D-07.
 * Define HOST_STUBS_REAL_REGISTER_UTILS before including this file to
 * activate this SECOND, independent opt-in layer (it composes with, does
 * not replace, HOST_STUBS_RECORD_BUS above). This is an opt-IN guard: no
 * existing suite may define it — flag off must stay byte-exact for all 14
 * pre-existing suites.
 *
 * (a) Opt-IN: the defining suite's host_stubs.cpp must `#define
 *     HOST_STUBS_REAL_REGISTER_UTILS` BEFORE `#include`-ing this file.
 * (b) Why: it exists so a suite can additionally `#include
 *     "rurp_register_utils.h"` (AFTER this file) and drive production's
 *     real cache-compare elision + latch-strobe sequencing directly,
 *     instead of a hand-maintained replica that can silently drift from
 *     `rurp_write_to_register` / `rurp_internal_write_to_register`.
 * (c) It therefore suppresses exactly the six symbols the real
 *     `rurp_register_utils.h` / `rurp_hw_rev_utils.h` header pair supplies,
 *     so the suite TU does not fail to compile with a redefinition error:
 *     `rurp_write_to_register`, `rurp_read_from_register`,
 *     `rurp_map_ctrl_reg_for_hardware_revision`,
 *     `rurp_get_physical_hardware_revision`,
 *     `rurp_detect_hardware_revision`, `rurp_get_hardware_revision`.
 * (d) The `HARDWARE_REVISION` collision only exists because `[env:native]`
 *     inherits `-D HARDWARE_REVISION` from `[env]`'s `build_flags` — every
 *     native suite TU sees that macro whether or not it cares about
 *     hardware-revision behavior.
 * (e) No existing suite may define this flag — flag off is byte-exact. */

/* Timing recorder (flag: HOST_STUBS_RECORD_TIMING) — Phase 138 PREP-03 / D-02.
 * Fail-closed guard: the timing recorder's sequence key is s_strobe_count,
 * which only exists when HOST_STUBS_REAL_REGISTER_UTILS is also defined.
 * Requesting HOST_STUBS_RECORD_TIMING without the strobe recorder would
 * silently produce a stream with no interleave key at all — refuse to
 * compile instead of composing into something unusable. */
#if defined(HOST_STUBS_RECORD_TIMING) && !defined(HOST_STUBS_REAL_REGISTER_UTILS)
#error "HOST_STUBS_RECORD_TIMING requires HOST_STUBS_REAL_REGISTER_UTILS (its sequence key is s_strobe_count, which only exists in that block)"
#endif

#ifdef HOST_STUBS_REAL_REGISTER_UTILS
/* rurp_write_to_register and rurp_read_from_register come from the REAL
 * include/rurp_register_utils.h, which the suite's host_stubs.cpp includes
 * AFTER this file — so neither may be defined here, and neither may the
 * four HARDWARE_REVISION stubs that rurp_hw_rev_utils.h (pulled in by that
 * header under the inherited -D HARDWARE_REVISION) already defines. */
#define HOST_STUBS_MAX_STROBES 512
#define HOST_STUBS_CUSTOM_CONTROL_PIN
#define HOST_STUBS_CUSTOM_DATA_BUFFER
#define HOST_STUBS_CUSTOM_HW_REVISION_BLOCK

enum { STROBE_KIND_DATA = 1, STROBE_KIND_PIN = 2 };
struct strobe_entry_t {
    uint8_t kind;
    uint8_t pin;
    uint8_t value;
};
static strobe_entry_t s_strobes[HOST_STUBS_MAX_STROBES];
static int s_strobe_count = 0;
static int s_strobe_overflow = 0;

extern "C" void clear_strobes()     { s_strobe_count = 0; s_strobe_overflow = 0; }
extern "C" int  strobe_count()      { return s_strobe_count; }
extern "C" int  strobe_overflowed() { return s_strobe_overflow; }
extern "C" uint8_t strobe_kind(int i)  { return s_strobes[i].kind; }
extern "C" uint8_t strobe_pin(int i)   { return s_strobes[i].pin; }
extern "C" uint8_t strobe_value(int i) { return s_strobes[i].value; }

static void strobe_push(uint8_t kind, uint8_t pin, uint8_t value) {
    if (s_strobe_count < HOST_STUBS_MAX_STROBES) {
        s_strobes[s_strobe_count].kind  = kind;
        s_strobes[s_strobe_count].pin   = pin;
        s_strobes[s_strobe_count].value = value;
        s_strobe_count++;
    } else {
        s_strobe_overflow = 1;  /* tail dropped; prefix stays valid — Pitfall 2 */
    }
}

/* Timing recorder (flag: HOST_STUBS_RECORD_TIMING) — Phase 138 PREP-03 / D-02.
 * This is a THIRD, independent opt-in layer, additive to the two recorders
 * above. It exists because neither HOST_STUBS_RECORD_BUS ((reg,data) pairs,
 * above) nor the ordered strobe recorder just above (kind,pin,value) can see
 * time: delay()/delayMicroseconds() are not stubbed anywhere in this file at
 * all — they are free functions DEFINED by ArduinoFake's FunctionFake.cpp and
 * only ever .AlwaysReturn()-mocked by every suite's setUp(). A definition
 * here would be a duplicate-symbol link error against FunctionFake.cpp.o; the
 * hook that calls timing_push() below lives in the DEFINING SUITE's own
 * setUp(), via fakeit's .AlwaysDo(lambda) — see test_trace_eprom_v131.cpp.
 *
 * (a) Opt-IN: the defining suite's host_stubs.cpp must `#define
 *     HOST_STUBS_RECORD_TIMING` BEFORE `#include`-ing this file, in addition
 *     to HOST_STUBS_REAL_REGISTER_UTILS (enforced above by the #error).
 * (b) Composition, stated correctly (unlike the strobe block's own comment
 *     above, which claims a composition the preprocessor does not actually
 *     provide): this guard lives INSIDE the HOST_STUBS_REAL_REGISTER_UTILS
 *     arm, with no #elif arm of its own, so it can only ever be compiled
 *     together with the strobe recorder — it can never silently replace it.
 * (c) The true pre-existing suite count, counted from this repo's own
 *     platformio.ini test_filter lists (both pinned native envs) at the time
 *     this block was added, is 17 — not the 14 the strobe block's own
 *     comment above states. That count is stale; it is recorded as F-138-06
 *     and deliberately left unfixed in this file (the correction lives in
 *     138-03-TRACE-CAPTURE.md instead), since this file's stability is
 *     load-bearing for other suites.
 * (d) Sequence key: each timing entry stores seq = s_strobe_count AT PUSH
 *     TIME, so the merged-stream comparator (eprom_v131_expected.h) can
 *     splice a timing entry immediately before the strobe at that index (or
 *     after the last strobe, if seq == strobe_count()).
 * (e) No existing suite may define this flag — flag off is byte-exact,
 *     because every new line below lives inside this #ifdef. */
#ifdef HOST_STUBS_RECORD_TIMING
#define HOST_STUBS_MAX_TIMINGS 512

enum { TIMING_KIND_DELAY_US = 3, TIMING_KIND_DELAY_MS = 4 };
struct timing_entry_t {
    uint8_t kind;
    uint32_t us;
    int seq;
};
static timing_entry_t s_timings[HOST_STUBS_MAX_TIMINGS];
static int s_timing_count = 0;
static int s_timing_overflow = 0;

extern "C" void clear_timings()     { s_timing_count = 0; s_timing_overflow = 0; }
extern "C" int  timing_count()      { return s_timing_count; }
extern "C" int  timing_overflowed() { return s_timing_overflow; }
extern "C" uint8_t  timing_kind(int i)          { return s_timings[i].kind; }
extern "C" uint32_t timing_us(int i)            { return s_timings[i].us; }
extern "C" int       timing_after_strobe(int i) { return s_timings[i].seq; }

extern "C" void timing_push(uint8_t kind, uint32_t us) {
    if (s_timing_count < HOST_STUBS_MAX_TIMINGS) {
        s_timings[s_timing_count].kind = kind;
        s_timings[s_timing_count].us   = us;
        s_timings[s_timing_count].seq  = s_strobe_count;
        s_timing_count++;
    } else {
        s_timing_overflow = 1;  /* tail dropped; prefix stays valid — Pitfall 2 */
    }
}
#endif /* HOST_STUBS_RECORD_TIMING */

/* The two hooks (D-07's scope — nothing wider). rurp_set_control_pin carries
 * LSB/MSB/CTRL latch strobes AND chip /CE and /OE in one distinguishable pin
 * namespace (rurp_shield.h: LEAST_SIGNIFICANT_BYTE/MOST_SIGNIFICANT_BYTE/
 * CONTROL_REGISTER/OUTPUT_ENABLE/CHIP_ENABLE), so no third hook and no wider
 * kind taxonomy is needed. */
extern "C" void rurp_write_data_buffer(uint8_t data) {
    strobe_push(STROBE_KIND_DATA, 0, data);
}
extern "C" void rurp_set_control_pin(uint8_t pin, uint8_t state) {
    strobe_push(STROBE_KIND_PIN, pin, state);
}
#elif defined(HOST_STUBS_RECORD_BUS)
#define HOST_STUBS_MAX_RECORDING 256

struct bus_record_entry_t {
    uint8_t reg;
    uint8_t data;
};
static bus_record_entry_t s_bus_recording[HOST_STUBS_MAX_RECORDING];
static int s_bus_recording_count = 0;

extern "C" void clear_bus_recording() { s_bus_recording_count = 0; }
extern "C" int  bus_recording_count() { return s_bus_recording_count; }
extern "C" uint8_t recorded_reg(int i)  { return s_bus_recording[i].reg; }
extern "C" uint8_t recorded_data(int i) { return s_bus_recording[i].data; }

extern "C" void rurp_write_to_register(uint8_t reg, rurp_register_t data) {
    if (s_bus_recording_count < HOST_STUBS_MAX_RECORDING) {
        s_bus_recording[s_bus_recording_count].reg  = reg;
        s_bus_recording[s_bus_recording_count].data = (uint8_t)data;
        s_bus_recording_count++;
    }
}
#else
extern "C" void rurp_write_to_register(uint8_t reg, rurp_register_t data) {
    (void)reg; (void)data;
}
#endif

#ifndef HOST_STUBS_REAL_REGISTER_UTILS
extern "C" rurp_register_t rurp_read_from_register(uint8_t reg) {
    (void)reg;
    return 0;
}
#endif

/* Chip / data bus helpers — referenced (transitively) by the proms TUs that
 * the [env:native] build links in. None are invoked by the dispatch test's
 * call path, but they must resolve at link time. */
#ifndef HOST_STUBS_CUSTOM_CONTROL_PIN
extern "C" void rurp_set_control_pin(uint8_t pin, uint8_t state) {
    (void)pin;
    (void)state;
}
#endif

extern "C" void rurp_set_data_output() {}
extern "C" void rurp_set_data_input() {}

#ifndef HOST_STUBS_CUSTOM_DATA_BUFFER
extern "C" void rurp_write_data_buffer(uint8_t data) {
    (void)data;
}
#endif

#ifndef HOST_STUBS_CUSTOM_READ_DATA_BUFFER
extern "C" uint8_t rurp_read_data_buffer() {
    return 0;
}
#endif

extern "C" uint16_t rurp_read_vcc_mv() {
    return 5000;
}

#ifndef HOST_STUBS_CUSTOM_VOLTAGE_MV
extern "C" uint16_t rurp_read_voltage_mv() {
    return 0;
}
#endif

extern "C" long rurp_get_bandgap_adc_reading() {
    return 0;
}

extern "C" uint8_t rurp_user_button_pressed() {
    return 0;
}

/* Hardware revision / config — referenced by some proms; return safe defaults. */
#if defined(HARDWARE_REVISION) && !defined(HOST_STUBS_CUSTOM_HW_REVISION_BLOCK)
extern "C" void rurp_detect_hardware_revision() {}

#ifndef HOST_STUBS_CUSTOM_HW_REVISION
extern "C" uint8_t rurp_get_hardware_revision() {
    return 0;
}
#endif

extern "C" uint8_t rurp_get_physical_hardware_revision() {
    return 0;
}

extern "C" uint8_t rurp_map_ctrl_reg_for_hardware_revision(rurp_register_t data) {
    return (uint8_t)data;
}
#endif

extern "C" void rurp_board_setup() {}
extern "C" void rurp_load_config() {}

static rurp_configuration_t s_host_config = {};
extern "C" rurp_configuration_t* rurp_get_config() {
    return &s_host_config;
}

extern "C" void rurp_save_config(rurp_configuration_t* config) {
    (void)config;
}

extern "C" void rurp_validate_config(rurp_configuration_t* config) {
    (void)config;
}

/* op_reset_timeout — defined in firestarter.cpp (loop()'s AVR-only command
 * timeout watchdog: `timeout = millis() + TIMEOUT_MS`), which [env:native]
 * and [env:native_nodevtools] deliberately exclude from build_src_filter
 * (firestarter.cpp is the dispatcher/loop() TU, not a proms/ handler). v1.22
 * Phase 119 D-06/D-07 (119-07 Task 1, RESEARCH Open Question 1 option a)
 * widened both envs' build_src_filter to include operation_utils.cpp so the
 * NULL-main refusal is natively provable; operation_utils.cpp calls
 * op_reset_timeout() unconditionally from _execute_operation_house_keeping
 * and _check_response, which is a new link-time requirement this stub
 * resolves. No native suite asserts on real-time timeout behaviour (that is
 * an AVR-only concern outside every suite's scope), so a no-op is
 * behaviourally adequate — same contract as every other stub in this file. */
extern "C" void op_reset_timeout() {}

/* Communication API — Phase 6 widened the [env:native] src_filter to pull
 * src/boards/rurp_serial_utils.cpp into the test binary (so test_messages
 * can exercise the real frame emitter end-to-end). That TU provides the
 * production rurp_communication_* implementations, so the dispatch suite no
 * longer needs to stub them. Intentionally NOT defined here. */

/* ArduinoFake declares Serial_::operator bool() in its USB-CDC header but
 * does not define it in SerialFake.cpp. rurp_serial_utils.cpp's
 * rurp_serial_begin loop `while (!SERIAL_PORT) ...` references the operator,
 * and the test binary pulls rurp_serial_utils.cpp into the link, so we
 * provide a host-side definition that returns true (matches production
 * HardwareSerial::operator bool() behaviour). The dispatch tests never call
 * rurp_serial_begin — this is link-only. */
#include <Arduino.h>
Serial_::operator bool() {
    return true;
}
