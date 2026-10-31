/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * The level of a VPP-on-P1 bus line while the chip is read (PR #101).
 *
 * A 2764/27128 (28-pin, VPP on pin 1 = bus line 15) and a 2716 (24-pin, VPP
 * on pin 21 = bus line 11) need VPP at VCC in read mode. Both lines are
 * shift-register logic outputs, so mem_util_remap_address_bus() can drive
 * them high for a read.
 *
 * A 32-pin VPP-on-P1 part uses bus line 21, which lands in the control
 * register as CTRL_ADDRESS_LINE_18. On Rev 0/1 that is the A18 latch bit, a
 * logic level, so a read drives it high. On Rev 2.x
 * rurp_map_ctrl_reg_for_hardware_revision() maps it onto the pin-1 VPP
 * switch, so driving it high switches VPE onto pin 1 (#142). With the
 * regulator off VPE idles at about 4.8 V, so a plain read turns the switch
 * on, after VPE has had RURP_VPE_DISCHARGE_MS to discharge. Every read with
 * the regulator on (the chip-ID read, the write path) keeps it off.
 *
 * test_val_eprom checks the remap output. This suite checks the PHYSICAL
 * control byte that the board latches while the chip is read.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "memory.h"
#include "eprom.h"
#include "memory_utils.h"
}
#include "firestarter.h"
#include "rurp_pinout.h"
#include "rurp_shield.h"

using namespace fakeit;

extern "C" void reset_register_cache(uint8_t lsb, uint8_t msb, rurp_register_t ctrl);
extern "C" void clear_read_snapshots();
extern "C" int read_snapshot_count();
extern "C" uint8_t read_snapshot_msb(int i);
extern "C" rurp_register_t read_snapshot_ctrl_logical(int i);
extern "C" uint8_t read_snapshot_ctrl_physical(int i);
extern "C" rurp_configuration_t* rurp_get_config();
extern "C" void set_regulator_off_ms(uint32_t ms);

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), delayMicroseconds)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(0);
    rurp_get_config()->hardware_revision = REVISION_2_0;
    reset_register_cache(0, 0, 0);
    set_regulator_off_ms(0);
    clear_read_snapshots();
}

void tearDown(void) {
    rurp_get_config()->hardware_revision = 0;
}

/* A bus config with no per-line remap: address_lines[0] == 0xFF keeps the
 * address bits where they are, so only the VPP line moves. */
static firestarter_handle_t make_handle(uint32_t protocol, uint8_t pins, uint8_t vpp_line,
                                        uint32_t mem_size, uint8_t cmd) {
    firestarter_handle_t h = {};
    h.protocol = protocol;
    h.pins = pins;
    h.mem_size = mem_size;
    h.cmd = cmd;
    h.response_code = RESPONSE_CODE_OK;
    h.ctrl_flags = FLAG_SKIP_ERASE;
    memset(h.bus_config.address_lines, 0xFF, sizeof(h.bus_config.address_lines));
    h.bus_config.address_mask = mem_size - 1;
    h.bus_config.matching_lines = 0;
    h.bus_config.rw_line = 0xFF;
    h.bus_config.vpp_line = vpp_line;
    h.bus_config.static_high_mask = 0;
    configure_memory(&h);
    reset_register_cache(0, 0, 0);
    clear_read_snapshots();
    return h;
}

/* ─── Rev 2 board: the physical bus while the chip is read ───────────────── */

static void assert_p1_switch_off_on_every_read(const char* msg) {
    TEST_ASSERT_TRUE_MESSAGE(read_snapshot_count() > 0, "no data read was observed");
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, read_snapshot_ctrl_physical(i) & CTRL_VPP_P1_ENABLE_REV2, msg);
    }
}

/* eprom_get_chip_id() turns the regulator on (without the drop resistor) and
 * then reads. The pin-1 VPP switch must stay off for those reads. */
void test_rev2_32pin_chip_id_read_keeps_p1_vpp_switch_off(void) {
    firestarter_handle_t h = make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_CHECK_CHIP_ID);
    h.firestarter_operation_main(&h);
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_TRUE_MESSAGE(read_snapshot_ctrl_logical(i) & CTRL_VPP_REGULATOR_ENABLE,
                                 "precondition: the regulator is on during the chip-ID read");
    }
    assert_p1_switch_off_on_every_read("the regulated rail is switched onto pin 1 during the chip-ID read");
}

/* A plain read switches the idle VPE rail onto pin 1, so the pin is at VCC
 * level, and the regulator stays off for every read. */
void test_rev2_32pin_plain_read_holds_pin1_at_idle_vpe(void) {
    firestarter_handle_t h = make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_READ);
    h.address = 0;
    h.data_size = 16;
    h.mem_size = 16;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_TRUE_MESSAGE(read_snapshot_count() > 0, "no data read was observed");
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(CTRL_VPP_P1_ENABLE_REV2, read_snapshot_ctrl_physical(i) & CTRL_VPP_P1_ENABLE_REV2,
                                       "pin 1 is low during a plain read");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, read_snapshot_ctrl_physical(i) & CTRL_VPP_REGULATOR_ENABLE_REV2,
                                       "the regulator is on while VPE is switched onto pin 1");
    }
}

/* The verify reads of a write run with the regulator on. The pin-1 switch
 * must be off for each of them, or the programming voltage reaches pin 1
 * while the chip drives its outputs. */
void test_rev2_32pin_write_reads_keep_p1_vpp_switch_off(void) {
    firestarter_handle_t h = make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_WRITE);
    h.address = 0;
    h.data_size = 4;
    memset(h.data_buffer, 0x00, 4);
    h.firestarter_operation_main(&h);
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_TRUE_MESSAGE(read_snapshot_ctrl_logical(i) & CTRL_VPP_REGULATOR_ENABLE,
                                 "precondition: the regulator is on during the write-path read");
    }
    assert_p1_switch_off_on_every_read("the regulated rail is switched onto pin 1 during a write-path read");
}

/* ─── Rev 2 board: VPE discharges before a read switches it onto pin 1 ───── */

void test_regulator_switch_off_records_the_time(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1234);
    rurp_write_to_register(CONTROL_REGISTER, CTRL_VPP_REGULATOR_ENABLE);
    rurp_write_to_register(CONTROL_REGISTER, 0);
    TEST_ASSERT_EQUAL_UINT32(1234, rurp_get_regulator_off_ms());
}

void test_rev2_32pin_read_soon_after_regulator_off_waits_for_discharge(void) {
    set_regulator_off_ms(1000);
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1300);
    make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_READ);
    Verify(Method(ArduinoFake(), delay).Using(RURP_VPE_DISCHARGE_MS - 300)).Once();
}

void test_rev2_32pin_read_after_discharge_does_not_wait(void) {
    set_regulator_off_ms(1000);
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000 + RURP_VPE_DISCHARGE_MS);
    make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_READ);
    Verify(Method(ArduinoFake(), delay)).Never();
}

/* Only a read switches VPE onto pin 1, so only a read waits. */
void test_rev2_32pin_chip_id_does_not_wait_for_discharge(void) {
    set_regulator_off_ms(1000);
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_CHECK_CHIP_ID);
    Verify(Method(ArduinoFake(), delay)).Never();
}

/* Rev 1 holds pin 1 high through its own A18 bit, so a read never waits. */
void test_rev1_32pin_read_does_not_wait_for_discharge(void) {
    rurp_get_config()->hardware_revision = REVISION_1;
    set_regulator_off_ms(1000);
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_READ);
    Verify(Method(ArduinoFake(), delay)).Never();
}

/* 28-pin: pin 1 is at VCC (MSB bit 7 = bus line 15) for every read, and the
 * VPP switch stays off. */
void test_rev2_28pin_chip_id_read_holds_pin1_high_with_switch_off(void) {
    firestarter_handle_t h = make_handle(0x07, 28, VPP_P1_28_DIP, 0x2000, CMD_CHECK_CHIP_ID);
    h.firestarter_operation_main(&h);
    assert_p1_switch_off_on_every_read("the VPP switch is on during a 28-pin chip-ID read");
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x80, read_snapshot_msb(i) & 0x80, "pin 1 is low during the read");
    }
}

/* ─── Rev 1 board: A18 is its own latch bit ──────────────────────────────── */

/* On Rev 0/1, pin 1 gets the A18 latch bit through a diode, separate from the
 * VPP switch, so a read holds pin 1 high without switching VPP. */
void test_rev1_32pin_plain_read_holds_pin1_high_with_switch_off(void) {
    rurp_get_config()->hardware_revision = REVISION_1;
    firestarter_handle_t h = make_handle(0x08, 32, VPP_P1_32_DIP, 0x20000, CMD_READ);
    h.address = 0;
    h.data_size = 16;
    h.mem_size = 16;
    h.firestarter_operation_main(&h);
    TEST_ASSERT_TRUE_MESSAGE(read_snapshot_count() > 0, "no data read was observed");
    for (int i = 0; i < read_snapshot_count(); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(CTRL_ADDRESS_LINE_18_REV1,
                                       read_snapshot_ctrl_physical(i) & CTRL_ADDRESS_LINE_18_REV1,
                                       "pin 1 (A18) is low during the read");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, read_snapshot_ctrl_physical(i) & CTRL_VPP_P1_ENABLE_REV1,
                                       "the VPP switch is on during the read");
    }
}

/* ─── dev addr: the handle never went through configure_memory() ──────────── */

/* dt_set_address() remaps an address on a handle that the JSON parser filled
 * but configure_memory() never saw, so every handler pointer is NULL. */
static firestarter_handle_t make_unconfigured_handle(uint8_t pins, uint8_t vpp_line) {
    firestarter_handle_t h = {};
    h.pins = pins;
    memset(h.bus_config.address_lines, 0xFF, sizeof(h.bus_config.address_lines));
    h.bus_config.address_mask = 0x1FFF;
    h.bus_config.rw_line = 0xFF;
    h.bus_config.vpp_line = vpp_line;
    return h;
}

void test_unconfigured_read_remap_does_not_call_a_null_handler(void) {
    firestarter_handle_t h = make_unconfigured_handle(28, VPP_P1_28_DIP);
    TEST_ASSERT_NULL(h.firestarter_get_control_register);
    mem_util_remap_address_bus(&h, 0, READ_FLAG);
}

/* dt_set_address() sets the mask itself, so dev addr with OE holds pin 1 high
 * for the read until the P1 route is on. */
void test_unconfigured_read_holds_pin1_high_once_the_mask_is_set(void) {
    firestarter_handle_t h = make_unconfigured_handle(28, VPP_P1_28_DIP);
    mem_util_set_read_high_mask(&h);
    uint32_t pin1 = (uint32_t)1 << VPP_P1_28_DIP;
    TEST_ASSERT_EQUAL_HEX32(pin1, mem_util_remap_address_bus(&h, 0, READ_FLAG) & pin1);
    TEST_ASSERT_EQUAL_HEX32(0, mem_util_remap_address_bus(&h, 0, WRITE_FLAG) & pin1);
    reset_register_cache(0, 0, CTRL_VPP_P1_ENABLE);
    TEST_ASSERT_EQUAL_HEX32(0, mem_util_remap_address_bus(&h, 0, READ_FLAG) & pin1);
}

/* ─── The read-high mask, set once per command ───────────────────────────── */

static bus_config_t read_high_config_for(uint8_t pins, uint8_t vpp_line) {
    firestarter_handle_t h = make_unconfigured_handle(pins, vpp_line);
    mem_util_set_read_high_mask(&h);
    return h.bus_config;
}

void test_read_high_mask_holds_logic_level_p1_lines(void) {
    bus_config_t c = read_high_config_for(28, VPP_P1_28_DIP);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)1 << VPP_P1_28_DIP, c.read_high_mask);
    TEST_ASSERT_FALSE(c.read_high_needs_reg_off);
    c = read_high_config_for(24, VPP_P21_24_DIP);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)1 << VPP_P21_24_DIP, c.read_high_mask);
    TEST_ASSERT_FALSE(c.read_high_needs_reg_off);
    rurp_get_config()->hardware_revision = REVISION_1;
    c = read_high_config_for(32, VPP_P1_32_DIP);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)1 << VPP_P1_32_DIP, c.read_high_mask);
    TEST_ASSERT_FALSE(c.read_high_needs_reg_off);
}

void test_read_high_mask_on_the_rev2_p1_switch_needs_the_regulator_off(void) {
    bus_config_t c = read_high_config_for(32, VPP_P1_32_DIP);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)1 << VPP_P1_32_DIP, c.read_high_mask);
    TEST_ASSERT_TRUE(c.read_high_needs_reg_off);
}

void test_read_high_mask_is_empty_where_the_line_is_not_p1(void) {
    TEST_ASSERT_EQUAL_HEX32(0, read_high_config_for(28, 0xFF).read_high_mask);
    TEST_ASSERT_EQUAL_HEX32(0, read_high_config_for(32, VPP_P1_28_DIP).read_high_mask);
}

/* dev addr: on Rev 2 the remap drives line 21 only while the regulator is off. */
void test_rev2_remap_holds_line21_low_while_the_regulator_is_on(void) {
    firestarter_handle_t h = make_unconfigured_handle(32, VPP_P1_32_DIP);
    mem_util_set_read_high_mask(&h);
    uint32_t line21 = (uint32_t)1 << VPP_P1_32_DIP;
    TEST_ASSERT_EQUAL_HEX32(line21, mem_util_remap_address_bus(&h, 0, READ_FLAG) & line21);
    reset_register_cache(0, 0, CTRL_VPP_REGULATOR_ENABLE);
    TEST_ASSERT_EQUAL_HEX32(0, mem_util_remap_address_bus(&h, 0, READ_FLAG) & line21);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_rev2_32pin_chip_id_read_keeps_p1_vpp_switch_off);
    RUN_TEST(test_rev2_32pin_plain_read_holds_pin1_at_idle_vpe);
    RUN_TEST(test_rev2_32pin_write_reads_keep_p1_vpp_switch_off);
    RUN_TEST(test_rev2_28pin_chip_id_read_holds_pin1_high_with_switch_off);
    RUN_TEST(test_rev1_32pin_plain_read_holds_pin1_high_with_switch_off);

    RUN_TEST(test_regulator_switch_off_records_the_time);
    RUN_TEST(test_rev2_32pin_read_soon_after_regulator_off_waits_for_discharge);
    RUN_TEST(test_rev2_32pin_read_after_discharge_does_not_wait);
    RUN_TEST(test_rev2_32pin_chip_id_does_not_wait_for_discharge);
    RUN_TEST(test_rev1_32pin_read_does_not_wait_for_discharge);

    RUN_TEST(test_unconfigured_read_remap_does_not_call_a_null_handler);
    RUN_TEST(test_unconfigured_read_holds_pin1_high_once_the_mask_is_set);
    RUN_TEST(test_read_high_mask_holds_logic_level_p1_lines);
    RUN_TEST(test_read_high_mask_on_the_rev2_p1_switch_needs_the_regulator_off);
    RUN_TEST(test_read_high_mask_is_empty_where_the_line_is_not_p1);
    RUN_TEST(test_rev2_remap_holds_line21_low_while_the_regulator_is_on);

    return UNITY_END();
}
