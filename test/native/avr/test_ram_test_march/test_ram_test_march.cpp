/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * CMD_RAM_TEST: the March C- engine (march.cpp) and its dispatch (sram.cpp).
 *
 * Engine cases install a fault-injecting SRAM model directly on the handle's
 * get/set data pointers, then drive march_execute() the way the simple
 * operation loop does: once per call, until the operation is no longer in
 * progress or the response is an error. Each fault case asserts the exact
 * first failure (element, address, expected, actual) that March C- must
 * report for it, read back from the emitted MSG_ERR_RAM_TEST frame. The
 * element index counts over all data backgrounds: background * 6 + element.
 *
 * Bus cases go through configure_memory() and the real memory_get_data /
 * memory_set_data, against the bus-level model in host_stubs.cpp.
 *
 * Validation ceiling: software only. No SRAM part was on the bench.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>
#include <vector>

extern "C" {
#include "march.h"
#include "memory.h"
#include "sram.h"
#include "messages.h"
#include "operation_utils.h"
}
#include "firestarter.h"
#include "proto_constants.h"
#include "rurp_pinout.h"

using namespace fakeit;

extern "C" void clear_bus_recording();
extern "C" int bus_recording_count();
extern "C" bool bus_recording_saturated();
extern "C" uint8_t recorded_reg(int i);
extern "C" rurp_register_t recorded_data(int i);
extern "C" void sram_model_reset(uint8_t fill);
extern "C" void sram_model_stuck_at_one(uint32_t address, uint8_t mask);
extern "C" uint8_t sram_model_peek(uint32_t address);

/* ─── Frame capture ─────────────────────────────────────────────────────── */

static std::vector<uint8_t> captured;

struct frame_t {
    uint8_t id;
    std::vector<uint8_t> params;
};

static std::vector<frame_t> captured_frames() {
    std::vector<frame_t> frames;
    size_t offset = 0;
    while (offset + 7 <= captured.size()) {
        uint16_t len = (uint16_t)(((uint16_t)captured[offset + 4] << 8) | captured[offset + 5]);
        size_t frame_size = 4 + 2 + (size_t)len + 1;
        if (offset + frame_size > captured.size()) {
            break;
        }
        frame_t frame;
        frame.id = captured[offset + 6];
        frame.params.assign(captured.begin() + offset + 7, captured.begin() + offset + 6 + len - 1);
        frames.push_back(frame);
        offset += frame_size;
    }
    return frames;
}

static uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return ((uint32_t)b[at] << 24) | ((uint32_t)b[at + 1] << 16) | ((uint32_t)b[at + 2] << 8) | b[at + 3];
}

/* ─── Engine-level SRAM model with fault injection ─────────────────────── */

enum fault_kind_t {
    FAULT_NONE,
    FAULT_STUCK_AT_0,
    FAULT_STUCK_AT_1,
    FAULT_TRANSITION_UP,      // the cell cannot go 0 -> 1
    FAULT_TRANSITION_DOWN,    // the cell cannot go 1 -> 0
    FAULT_ADDRESS_ALIAS,      // `other` decodes to the cell at `address`
    FAULT_COUPLING_INVERSION, // a rising write at `other` inverts `address`
    FAULT_COUPLING_IDEMPOTENT, // a rising write at `other` forces `address` to 0
    FAULT_DATA_BRIDGE         // the data lines in `mask` are shorted (wired-AND), at every address
};

struct fault_t {
    fault_kind_t kind;
    uint32_t address;
    uint32_t other;
    uint8_t mask;
};

// The last data background: a good RAM ends with every cell holding it.
#define MARCH_LAST_BACKGROUND 0x0F

static std::vector<uint8_t> model;
static fault_t fault;
static std::vector<uint32_t> touches;
static uint32_t ops_this_call;

static uint32_t model_decode(uint32_t address) {
    if (fault.kind == FAULT_ADDRESS_ALIAS && address == fault.other) {
        return fault.address;
    }
    return address;
}

// Shorted data lines read and drive the AND of their levels.
static uint8_t model_bridge(uint8_t value) {
    if (fault.kind == FAULT_DATA_BRIDGE && (value & fault.mask) != fault.mask) {
        value &= (uint8_t)~fault.mask;
    }
    return value;
}

static uint8_t model_cell(uint32_t cell) {
    uint8_t value = model[cell];
    if (cell == fault.address) {
        if (fault.kind == FAULT_STUCK_AT_0) value &= (uint8_t)~fault.mask;
        if (fault.kind == FAULT_STUCK_AT_1) value |= fault.mask;
    }
    return value;
}

static void model_set(firestarter_handle_t* handle, uint32_t address, uint8_t data) {
    (void)handle;
    touches[address]++;
    ops_this_call++;
    const uint32_t cell = model_decode(address);
    const uint8_t old = model[cell];
    uint8_t value = model_bridge(data);
    if (cell == fault.address && fault.kind == FAULT_TRANSITION_UP) {
        value = (uint8_t)((value & ~fault.mask) | (old & value & fault.mask));
    }
    if (cell == fault.address && fault.kind == FAULT_TRANSITION_DOWN) {
        value = (uint8_t)(value | (old & fault.mask));
    }
    model[cell] = value;
    const bool rising = ((old ^ value) & value & fault.mask) != 0;
    if (cell == fault.other && rising) {
        if (fault.kind == FAULT_COUPLING_INVERSION) model[fault.address] ^= fault.mask;
        if (fault.kind == FAULT_COUPLING_IDEMPOTENT) model[fault.address] &= (uint8_t)~fault.mask;
    }
}

static uint8_t model_get(firestarter_handle_t* handle, uint32_t address) {
    (void)handle;
    touches[address]++;
    ops_this_call++;
    return model_bridge(model_cell(model_decode(address)));
}

static firestarter_handle_t handle;

static void engine_setup(uint32_t size, fault_t f) {
    memset(&handle, 0, sizeof(handle));
    handle.cmd = CMD_RAM_TEST;
    handle.protocol = PROTO_SRAM_28PIN;
    handle.mem_size = size;
    handle.response_code = RESPONSE_CODE_OK;
    handle.firestarter_set_data = model_set;
    handle.firestarter_get_data = model_get;
    model.assign(size, 0x5A);  // random-looking power-up contents
    touches.assign(size, 0);
    fault = f;
    march_begin(&handle);
}

/* Drive march_execute() like _single_step_operation_callback: call, check
 * the response, stop when no longer in progress. Returns the call count. */
static int engine_run() {
    int calls = 0;
    do {
        ops_this_call = 0;
        clear_operation_in_progress(&handle);
        march_execute(&handle);
        calls++;
        TEST_ASSERT_TRUE_MESSAGE(ops_this_call <= 2 * MARCH_BATCH_SIZE,
            "one main callback must stay within one batch");
        if (handle.response_code == RESPONSE_CODE_ERROR) {
            break;
        }
    } while (is_operation_in_progress(&handle));
    return calls;
}

static void assert_first_failure(uint8_t element, uint32_t address, uint8_t expected, uint8_t actual) {
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, handle.response_code, "the fault must be detected");
    std::vector<frame_t> frames = captured_frames();
    int found = 0;
    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i].id != MSG_ERR_RAM_TEST) {
            continue;
        }
        found++;
        const std::vector<uint8_t>& p = frames[i].params;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, p.size(), "MSG_ERR_RAM_TEST carries 6 param bytes");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(element, p[0], "March element");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(address, ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3], "address");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected, p[4], "expected value");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(actual, p[5], "value read");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, found, "exactly one MSG_ERR_RAM_TEST frame: the test stops at the first failure");
}

/* ─── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void) {
    ArduinoFakeReset();
    captured.clear();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured.push_back(b);
            return (size_t)1;
        });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    When(Method(ArduinoFake(), micros)).AlwaysReturn(0);
    clear_bus_recording();
}

void tearDown(void) {}

/* ─── Engine: a good RAM passes ─────────────────────────────────────────── */

static void check_good_ram_passes(uint32_t size) {
    engine_setup(size, fault_t{FAULT_NONE, 0xFFFFFFFF, 0xFFFFFFFF, 0});
    engine_run();
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, handle.response_code, "a good RAM must pass");
    TEST_ASSERT_FALSE_MESSAGE(is_operation_in_progress(&handle), "the test must end");
    for (uint32_t a = 0; a < size; a++) {
        if (touches[a] != MARCH_C_MINUS_OPS_PER_ADDRESS * MARCH_BACKGROUNDS) {
            char msg[64];
            snprintf(msg, sizeof(msg), "address 0x%06x touched %u times", (unsigned)a, (unsigned)touches[a]);
            TEST_FAIL_MESSAGE(msg);
        }
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(MARCH_LAST_BACKGROUND, model[a], "the test leaves every cell at the last background");
    }
}

void test_good_ram_passes_2k(void) { check_good_ram_passes(2048); }
void test_good_ram_passes_8k(void) { check_good_ram_passes(8192); }
void test_good_ram_passes_32k(void) { check_good_ram_passes(32768); }

/* A size that is not a whole number of batches, and the one-cell edge. */
void test_good_ram_passes_partial_batch(void) { check_good_ram_passes(1000); }
void test_good_ram_passes_one_cell(void) { check_good_ram_passes(1); }

void test_progress_is_monotonic_and_bounded(void) {
    const uint32_t size = 2048;
    engine_setup(size, fault_t{FAULT_NONE, 0xFFFFFFFF, 0xFFFFFFFF, 0});
    const int calls = engine_run();
    std::vector<frame_t> frames = captured_frames();
    uint32_t last = 0;
    int progress = 0;
    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i].id != MSG_DATA_PROGRESS) {
            continue;
        }
        progress++;
        const uint32_t done = be32(frames[i].params, 0);
        const uint32_t total = be32(frames[i].params, 4);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(6 * MARCH_BACKGROUNDS * size, total, "total is elements x backgrounds x size");
        TEST_ASSERT_TRUE_MESSAGE(done > last, "progress must advance on every frame");
        TEST_ASSERT_TRUE_MESSAGE(done <= total, "progress must not pass the total");
        last = done;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(calls, progress, "one progress frame per batch, the last batch included");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6 * MARCH_BACKGROUNDS * size, last, "the last frame reports the total");
}

/* ─── Engine: each fault class is found, at the expected first failure ── */

void test_stuck_at_1_found_in_element_1(void) {
    engine_setup(2048, fault_t{FAULT_STUCK_AT_1, 0x123, 0xFFFFFFFF, 0x04});
    engine_run();
    assert_first_failure(1, 0x123, 0x00, 0x04);
}

void test_stuck_at_0_found_in_element_2(void) {
    engine_setup(2048, fault_t{FAULT_STUCK_AT_0, 0x7FF, 0xFFFFFFFF, 0x80});
    engine_run();
    assert_first_failure(2, 0x7FF, 0xFF, 0x7F);
}

void test_transition_up_fault_found_in_element_2(void) {
    engine_setup(2048, fault_t{FAULT_TRANSITION_UP, 0x000, 0xFFFFFFFF, 0x01});
    engine_run();
    assert_first_failure(2, 0x000, 0xFF, 0xFE);
}

void test_transition_down_fault_found_in_element_3(void) {
    // Bit 0 of the 0x5A power-up fill is 0, so the cell first reaches 1 in
    // element 1 and first fails to return to 0 in element 2's write.
    engine_setup(2048, fault_t{FAULT_TRANSITION_DOWN, 0x400, 0xFFFFFFFF, 0x01});
    engine_run();
    assert_first_failure(3, 0x400, 0x00, 0x01);
}

/* Address line short: a read of 0x200 returns the cell at 0x000. */
void test_address_alias_found(void) {
    engine_setup(2048, fault_t{FAULT_ADDRESS_ALIAS, 0x000, 0x200, 0});
    engine_run();
    assert_first_failure(1, 0x200, 0x00, 0xFF);
}

/* Aggressor below the victim: the ascending element 1 finds it. */
void test_inversion_coupling_aggressor_below_victim(void) {
    engine_setup(2048, fault_t{FAULT_COUPLING_INVERSION, 0x040, 0x030, 0x01});
    engine_run();
    assert_first_failure(1, 0x040, 0x00, 0x01);
}

/* Aggressor above the victim: the victim already holds 1 when it flips. */
void test_inversion_coupling_aggressor_above_victim(void) {
    engine_setup(2048, fault_t{FAULT_COUPLING_INVERSION, 0x030, 0x040, 0x01});
    engine_run();
    assert_first_failure(2, 0x030, 0xFF, 0xFE);
}

/* The case that needs the descending elements: an aggressor below the victim
 * forces 0 on a rising write, which only the down(r0,w1) element produces
 * after the victim already holds 1. */
void test_idempotent_coupling_needs_descending_element(void) {
    engine_setup(2048, fault_t{FAULT_COUPLING_IDEMPOTENT, 0x060, 0x050, 0x02});
    engine_run();
    assert_first_failure(4, 0x060, 0xFF, 0xFD);
}

/* Two shorted data lines: the solid backgrounds 0x00/0xFF drive them to the
 * same level, so only a data background where they differ finds the short.
 * D0/D1 differ in 0x55: the first read of background 1 (element 7) fails. */
void test_data_bridge_d0_d1_found_in_background_0x55(void) {
    engine_setup(2048, fault_t{FAULT_DATA_BRIDGE, 0xFFFFFFFF, 0xFFFFFFFF, 0x03});
    engine_run();
    assert_first_failure(7, 0x000, 0x55, 0x54);
}

/* D3/D7 are equal in 0x00, 0x55 and 0x33, and differ only in 0x0F. */
void test_data_bridge_d3_d7_found_in_background_0x0f(void) {
    engine_setup(2048, fault_t{FAULT_DATA_BRIDGE, 0xFFFFFFFF, 0xFFFFFFFF, 0x88});
    engine_run();
    assert_first_failure(19, 0x000, 0x0F, 0x07);
}

/* ─── Dispatch: every SRAM protocol installs the engine ────────────────── */

static bus_config_t sram_model_bus_config() {
    bus_config_t config;
    memset(&config, 0, sizeof(config));
    config.address_lines[0] = 0xFF;  // no remap
    config.address_mask = 0x7FF;
    config.rw_line = 14;
    config.vpp_line = 0xFF;
    return config;
}

static void dispatch(uint8_t protocol) {
    memset(&handle, 0, sizeof(handle));
    handle.cmd = CMD_RAM_TEST;
    handle.protocol = protocol;
    handle.mem_size = 2048;
    handle.pins = 24;
    handle.response_code = RESPONSE_CODE_OK;
    handle.bus_config = sram_model_bus_config();
    configure_memory(&handle);
}

/* The firmware cannot tell a volatile row from a non-volatile one, so 0x0E
 * installs the engine too. The host refuses non-volatile rows. */
void test_dispatch_installs_engine_for_sram_protocols(void) {
    const uint8_t protocols[] = {PROTO_SRAM_24PIN, PROTO_SRAM_28PIN, PROTO_SRAM_32PIN_NVRAM, PROTO_SRAM_32PIN};
    for (size_t i = 0; i < sizeof(protocols); i++) {
        dispatch(protocols[i]);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, handle.response_code, "configure must succeed");
        TEST_ASSERT_TRUE_MESSAGE(handle.firestarter_operation_main == march_execute,
            "a RAM-test protocol must install march_execute");
        TEST_ASSERT_NULL_MESSAGE(handle.firestarter_operation_init, "the RAM test has no INIT callback");
    }
}

/* 0x07 and 0x0D are not RAM. A NULL main is refused with
 * MSG_ERR_NOT_SUPPORTED before any INIT callback runs. */
void test_dispatch_refuses_other_protocols(void) {
    const uint8_t protocols[] = {PROTO_EPROM_28PIN, PROTO_EEPROM_PARALLEL};
    for (size_t i = 0; i < sizeof(protocols); i++) {
        dispatch(protocols[i]);
        TEST_ASSERT_NULL_MESSAGE(handle.firestarter_operation_main,
            "a protocol that is not an SRAM protocol must leave main NULL");
    }
}

/* ─── Bus level: real memory_get_data / memory_set_data ─────────────────── */

static void bus_run() {
    TEST_ASSERT_TRUE(handle.firestarter_operation_main == march_execute);
    do {
        clear_operation_in_progress(&handle);
        handle.firestarter_operation_main(&handle);
        if (handle.response_code == RESPONSE_CODE_ERROR) {
            break;
        }
    } while (is_operation_in_progress(&handle));
}

void test_bus_good_ram_passes_with_no_high_voltage(void) {
    sram_model_reset(0x5A);
    dispatch(PROTO_SRAM_24PIN);
    bus_run();
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_OK, handle.response_code, "a good RAM must pass on the real bus path");
    TEST_ASSERT_FALSE_MESSAGE(bus_recording_saturated(), "the recording must hold the whole run");
    for (uint32_t a = 0; a < 2048; a++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(MARCH_LAST_BACKGROUND, sram_model_peek(a), "the test leaves every cell at the last background");
    }
    const rurp_register_t high_voltage =
        CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE | CTRL_VPE_ENABLE | CTRL_VPP_A9_ENABLE;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_EQUAL_MESSAGE(0, recorded_data(i) & high_voltage,
                "the RAM test must never set a high-voltage control bit");
        }
    }
}

void test_bus_reports_the_decoded_address(void) {
    sram_model_reset(0x00);
    sram_model_stuck_at_one(0x5A5, 0x20);
    dispatch(PROTO_SRAM_24PIN);
    bus_run();
    assert_first_failure(1, 0x5A5, 0x00, 0x20);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_good_ram_passes_2k);
    RUN_TEST(test_good_ram_passes_8k);
    RUN_TEST(test_good_ram_passes_32k);
    RUN_TEST(test_good_ram_passes_partial_batch);
    RUN_TEST(test_good_ram_passes_one_cell);
    RUN_TEST(test_progress_is_monotonic_and_bounded);
    RUN_TEST(test_stuck_at_1_found_in_element_1);
    RUN_TEST(test_stuck_at_0_found_in_element_2);
    RUN_TEST(test_transition_up_fault_found_in_element_2);
    RUN_TEST(test_transition_down_fault_found_in_element_3);
    RUN_TEST(test_address_alias_found);
    RUN_TEST(test_inversion_coupling_aggressor_below_victim);
    RUN_TEST(test_inversion_coupling_aggressor_above_victim);
    RUN_TEST(test_idempotent_coupling_needs_descending_element);
    RUN_TEST(test_data_bridge_d0_d1_found_in_background_0x55);
    RUN_TEST(test_data_bridge_d3_d7_found_in_background_0x0f);
    RUN_TEST(test_dispatch_installs_engine_for_sram_protocols);
    RUN_TEST(test_dispatch_refuses_other_protocols);
    RUN_TEST(test_bus_good_ram_passes_with_no_high_voltage);
    RUN_TEST(test_bus_reports_the_decoded_address);

    return UNITY_END();
}
