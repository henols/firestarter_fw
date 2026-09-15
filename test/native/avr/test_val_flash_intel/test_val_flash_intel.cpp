/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Proves configure_flash_intel behavior BY SIDE-EFFECT via the recording bus stub:
 *
 *   POSITIVE test (CMD_WRITE + init): configure_memory() + firestarter_operation_init()
 *     → flash_intel_write_init → sets CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE
 *     via firestarter_set_control_register → memory_set_control_register
 *     → rurp_write_to_register(CONTROL_REGISTER, value | CTRL_VPP_P1_ENABLE)
 *     The recording must contain at least one CONTROL_REGISTER write with
 *     CTRL_VPP_P1_ENABLE set.
 *
 * Protocol covered: 0x10 (FLASH_INTEL).
 *
 * VPP mechanism (from flash_intel.cpp source — not guessed):
 *   flash_intel_write_init line 107: CTRL_VPP_REGULATOR_ENABLE | CTRL_VPP_P1_ENABLE
 *
 * NOTE: delay() must be mocked; flash_intel_write_init calls delay(500).
 * VPP voltage stub returns 12000 mV (h.vpp_mv = 12000 setpoint matches → no error).
 * Hardware revision stub returns 1 (non-REVISION_0) so VPP path is reachable.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"

using namespace fakeit;

/* Recording API — symbols compiled because host_stubs.cpp defines HOST_STUBS_RECORD_BUS. */
extern "C" void clear_bus_recording();
extern "C" int  bus_recording_count();
extern "C" uint8_t recorded_reg(int i);
extern "C" uint8_t recorded_data(int i);

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by flash_intel_write_init (delay(500) for regulator settle). */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

/* Build a handle pre-wired for the Intel-flash write path.
 * chip_id=0 skips the chip-id branch. FLAG_SKIP_BLANK_CHECK + FLAG_SKIP_ERASE
 * prevent blank-check and erase from running against the mock. */
static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x10;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv     = 12000; /* matches mock voltage 12V → no VPP warn/error */
    h.chip_id    = 0;     /* skip chip-id branch */
    h.mem_size   = 131072; /* 128 KB */
    h.ctrl_flags = FLAG_SKIP_BLANK_CHECK | FLAG_SKIP_ERASE;
    return h;
}

/* ─── POSITIVE test: CMD_WRITE + init → CTRL_VPP_P1_ENABLE must fire ──────── */

void test_flash_intel_write_enables_vpp_p1(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    bool p1_seen = false;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & CTRL_VPP_P1_ENABLE)) {
            p1_seen = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(p1_seen,
        "configure_flash_intel write must record CTRL_VPP_P1_ENABLE in CTL register");
}

/* Also assert CTRL_VPP_REGULATOR_ENABLE is set (flash_intel_write_init sets both). */
void test_flash_intel_write_enables_vpp_regulator(void) {
    firestarter_handle_t h = make_handle(CMD_WRITE);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_WRITE");
    if (h.firestarter_operation_init) {
        h.firestarter_operation_init(&h);
    }
    bool reg_seen = false;
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER &&
            (recorded_data(i) & CTRL_VPP_REGULATOR_ENABLE)) {
            reg_seen = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(reg_seen,
        "configure_flash_intel write must record CTRL_VPP_REGULATOR_ENABLE in CTL register");
}

/* ─── NEGATIVE CONTROL: CMD_READ, configure-only — VPP must NOT fire ─────── */

void test_flash_intel_read_configure_only_does_not_enable_vpp(void) {
    firestarter_handle_t h = make_handle(CMD_READ);
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "configure_memory must not error on 0x10 CMD_READ");
    /* No init call — configure-phase only */
    for (int i = 0; i < bus_recording_count(); i++) {
        if (recorded_reg(i) == CONTROL_REGISTER) {
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_P1_ENABLE,
                recorded_data(i),
                "configure_flash_intel CMD_READ configure-phase must NOT set CTRL_VPP_P1_ENABLE");
            TEST_ASSERT_BITS_LOW_MESSAGE(
                (uint8_t)CTRL_VPP_REGULATOR_ENABLE,
                recorded_data(i),
                "configure_flash_intel CMD_READ configure-phase must NOT set CTRL_VPP_REGULATOR_ENABLE");
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* POSITIVE: write + init path enables VPP_P1 and VPP_REGULATOR */
    RUN_TEST(test_flash_intel_write_enables_vpp_p1);
    RUN_TEST(test_flash_intel_write_enables_vpp_regulator);

    /* NEGATIVE CONTROL: configure-only (CMD_READ, no init) — no VPP bits set */
    RUN_TEST(test_flash_intel_read_configure_only_does_not_enable_vpp);

    return UNITY_END();
}
