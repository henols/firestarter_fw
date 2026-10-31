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

#include <vector>

extern "C" {
#include "memory.h"
}
#include "firestarter.h"
#include "messages.h"
#include "rurp_pinout.h"

using namespace fakeit;

#include "../_shared/bus_recording.h"

static std::vector<uint8_t> s_wire_bytes;
static uint32_t s_millis = 0;

void setUp(void) {
    ArduinoFakeReset();
    s_wire_bytes.clear();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            s_wire_bytes.push_back(b);
            return (size_t)1;
        });
    s_millis = 0;
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return s_millis++; });
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    /* delay() is called by flash_intel_write_init (delay(500) for regulator settle). */
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    clear_bus_recording();
}

void tearDown(void) {}

/* Build a handle pre-wired for the Intel-flash write path.
 * chip_id=0 skips the chip-id branch. FLAG_SKIP_ERASE prevents erase from
 * running against the mock; write-init performs no blank check at all any
 * more regardless of ctrl_flags (FWBLANK-01). */
static firestarter_handle_t make_handle(uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol   = 0x10;
    h.cmd        = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.vpp_mv     = 12000; /* matches mock voltage 12V → no VPP warn/error */
    h.chip_id    = 0;     /* skip chip-id branch */
    h.mem_size   = 131072; /* 128 KB */
    h.ctrl_flags = FLAG_SKIP_ERASE;
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

/* ─── Issue #116: the status-register decode ─────────────────────────────── */

static uint8_t s_status = 0x80;
static std::vector<uint8_t> s_commands;
static uint8_t stub_status(firestarter_handle_t*, uint32_t) { return s_status; }
static void stub_command(firestarter_handle_t*, uint32_t, uint8_t data) { s_commands.push_back(data); }

/* Runs one operation main against a part that answers every read with sr. */
static firestarter_handle_t run_with_status(uint8_t cmd, uint8_t sr) {
    firestarter_handle_t h = make_handle(cmd);
    h.data_size = 1;
    configure_memory(&h);
    h.firestarter_get_data = stub_status;
    h.firestarter_set_data = stub_command;
    s_status = sr;
    s_commands.clear();
    h.firestarter_operation_main(&h);
    return h;
}

static uint8_t first_frame_id(void) {
    return s_wire_bytes.size() > 6 ? s_wire_bytes[6] : 0;
}

void test_intel_sr_ready_without_error_succeeds(void) {
    firestarter_handle_t h = run_with_status(CMD_ERASE, 0x80);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_OK, h.response_code);
    TEST_ASSERT_EQUAL_size_t(0, s_wire_bytes.size());
}

void test_intel_sr3_is_vpp_low(void) {
    firestarter_handle_t h = run_with_status(CMD_WRITE, 0x88);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_INTEL_VPP, first_frame_id());
}

void test_intel_sr4_is_a_program_error(void) {
    firestarter_handle_t h = run_with_status(CMD_WRITE, 0x90);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_INTEL_PROGRAM, first_frame_id());
}

void test_intel_sr5_fails_the_erase(void) {
    firestarter_handle_t h = run_with_status(CMD_ERASE, 0xA0);
    TEST_ASSERT_EQUAL_MESSAGE(RESPONSE_CODE_ERROR, h.response_code,
        "SR.5 is the erase error: the erase must not report success");
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_INTEL_PROGRAM, first_frame_id());
}

void test_intel_sr4_and_sr5_is_a_command_sequence_error(void) {
    firestarter_handle_t h = run_with_status(CMD_ERASE, 0xB0);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_CMD, first_frame_id());
}

void test_intel_vpp_low_wins_over_the_other_bits(void) {
    firestarter_handle_t h = run_with_status(CMD_ERASE, 0xB8);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_INTEL_VPP, first_frame_id());
}

void test_intel_error_clears_the_status_then_reads_array(void) {
    run_with_status(CMD_ERASE, 0xA0);
    TEST_ASSERT_TRUE(s_commands.size() >= 4);
    TEST_ASSERT_EQUAL_HEX8(0x20, s_commands[0]);
    TEST_ASSERT_EQUAL_HEX8(0xD0, s_commands[1]);
    TEST_ASSERT_EQUAL_HEX8(0x50, s_commands[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, s_commands[3]);
}

void test_intel_busy_part_times_out(void) {
    firestarter_handle_t h = run_with_status(CMD_WRITE, 0x00);
    TEST_ASSERT_EQUAL(RESPONSE_CODE_ERROR, h.response_code);
    TEST_ASSERT_EQUAL_HEX8(MSG_ERR_INTEL_SR_TIMEOUT, first_frame_id());
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    /* POSITIVE: write + init path enables VPP_P1 and VPP_REGULATOR */
    RUN_TEST(test_flash_intel_write_enables_vpp_p1);
    RUN_TEST(test_flash_intel_write_enables_vpp_regulator);

    /* NEGATIVE CONTROL: configure-only (CMD_READ, no init) — no VPP bits set */
    RUN_TEST(test_flash_intel_read_configure_only_does_not_enable_vpp);

    RUN_TEST(test_intel_sr_ready_without_error_succeeds);
    RUN_TEST(test_intel_sr3_is_vpp_low);
    RUN_TEST(test_intel_sr4_is_a_program_error);
    RUN_TEST(test_intel_sr5_fails_the_erase);
    RUN_TEST(test_intel_sr4_and_sr5_is_a_command_sequence_error);
    RUN_TEST(test_intel_vpp_low_wins_over_the_other_bits);
    RUN_TEST(test_intel_error_clears_the_status_then_reads_array);
    RUN_TEST(test_intel_busy_part_times_out);

    return UNITY_END();
}
