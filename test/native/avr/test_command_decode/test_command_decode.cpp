/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Native tests for the binary command decoder (src/command_decoder.c).
 *
 * The golden frames in command_vectors.h are generated in the meta repo from
 * tools/protocol/command_frame.toml by a reference encoder that is neither
 * the host's nor the firmware's. The host's test suite encodes the same
 * vectors and must produce the same bytes.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

extern "C" {
#include "command_decoder.h"
#include "memory.h"
}
#include "command_vectors.h"
#include "firestarter.h"
#include "rurp_shield.h"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t))).AlwaysReturn(1);
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))).AlwaysReturn(1);
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {}

static uint8_t load(firestarter_handle_t* h, const uint8_t* frame, uint16_t len) {
    memcpy(h->data_buffer, frame, len);
    h->data_size = len;
    return cmd_decode(h);
}

static const cf_accept_vector_t* accept_vector(const char* name) {
    for (size_t i = 0; i < CF_ACCEPT_VECTOR_COUNT; i++) {
        if (strcmp(CF_ACCEPT_VECTORS[i].name, name) == 0) {
            return &CF_ACCEPT_VECTORS[i];
        }
    }
    TEST_FAIL_MESSAGE(name);
    return NULL;
}

static uint8_t load_vector(firestarter_handle_t* h, const char* name) {
    const cf_accept_vector_t* v = accept_vector(name);
    return load(h, v->frame, v->frame_len);
}

/* Fill every command parameter with a value no vector uses. */
static void poison(firestarter_handle_t* h) {
    h->mem_size = 0xDEADBEEF;
    h->address = 0xDEADBEEF;
    h->region_end = 0xDEADBEEF;
    h->protocol = 0xEE;
    h->pins = 0xEE;
    h->vpp_mv = 0xEEEE;
    h->pulse_delay = 0xDEADBEEF;
    h->chip_id = 0xEEEE;
    h->page_size = 0xEEEE;
    h->read_settling_us = 0xDEADBEEF;
    h->read_strobe_us = 0xDEADBEEF;
    memset(h->bus_config.address_lines, 0xEE, sizeof(h->bus_config.address_lines));
    h->bus_config.rw_line = 0xEE;
    h->bus_config.vpp_line = 0xEE;
    h->bus_config.static_high_mask = 0xDEADBEEF;
    h->bus_config.address_mask = 0xDEADBEEF;
    h->bus_config.matching_lines = 0xEE;
}

/* Every field poison() writes still holds its poison value. */
static void assert_poisoned(const firestarter_handle_t* h, const char* name) {
    uint8_t poisoned_lines[ADDRESS_LINES_SIZE];
    memset(poisoned_lines, 0xEE, sizeof(poisoned_lines));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->mem_size, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->address, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->region_end, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, h->protocol, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, h->pins, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xEEEE, h->vpp_mv, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->pulse_delay, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xEEEE, h->chip_id, name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xEEEE, h->page_size, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->read_settling_us, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->read_strobe_us, name);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(poisoned_lines, h->bus_config.address_lines,
                                          ADDRESS_LINES_SIZE, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, h->bus_config.rw_line, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, h->bus_config.vpp_line, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->bus_config.static_high_mask, name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEF, h->bus_config.address_mask, name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, h->bus_config.matching_lines, name);
}

/* cmd_apply_config writes exactly the fields the frame marks present. */
static void assert_config_applied(const firestarter_handle_t* h, const cf_accept_vector_t* v) {
    rurp_configuration_t config;
    memset(&config, 0xEE, sizeof(config));
    const rurp_configuration_t before = config;
    uint8_t res = cmd_apply_config(h, &config);
    bool changes = v->cfg_present & (CF_CONFIG_PRESENT_R1 | CF_CONFIG_PRESENT_R2 |
                                     CF_CONFIG_PRESENT_REV | CF_CONFIG_PRESENT_BG);
    TEST_ASSERT_EQUAL_MESSAGE(changes, (res & CMD_CFG_FIELD_CHANGED) != 0, v->name);
    TEST_ASSERT_EQUAL_MESSAGE((v->cfg_present & CF_CONFIG_PRESENT_VCC) != 0,
                              (res & CMD_CFG_CALIBRATE) != 0, v->name);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        v->cfg_present & CF_CONFIG_PRESENT_R1 ? (long)v->cfg_r1 : before.r1, config.r1, v->name);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        v->cfg_present & CF_CONFIG_PRESENT_R2 ? (long)v->cfg_r2 : before.r2, config.r2, v->name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        v->cfg_present & CF_CONFIG_PRESENT_REV ? v->cfg_rev : before.hardware_revision,
        config.hardware_revision, v->name);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        v->cfg_present & CF_CONFIG_PRESENT_BG ? v->cfg_bg : before.bandgap_mv, config.bandgap_mv,
        v->name);
    if (v->cfg_present & CF_CONFIG_PRESENT_VCC) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->cfg_vcc, cmd_config_measured_vcc_mv(h), v->name);
    }
}

void test_every_accept_vector_decodes_to_its_fields(void) {
    for (size_t i = 0; i < CF_ACCEPT_VECTOR_COUNT; i++) {
        const cf_accept_vector_t* v = &CF_ACCEPT_VECTORS[i];
        firestarter_handle_t h = {};
        poison(&h);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(CMD_DECODE_OK, load(&h, v->frame, v->frame_len), v->name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->cmd, h.cmd, v->name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->flags, h.ctrl_flags, v->name);
        if (v->kind == CF_KIND_CONFIG) {
            assert_config_applied(&h, v);
        }
        if (v->kind != CF_KIND_MEMORY) {
            continue;
        }
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->mem_size, h.mem_size, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->address, h.address, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->region_end, h.region_end, v->name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->protocol, h.protocol, v->name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->pins, h.pins, v->name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->vpp_mv, h.vpp_mv, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->pulse_delay, h.pulse_delay, v->name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->chip_id, h.chip_id, v->name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->page_size, h.page_size, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->read_settling_us, h.read_settling_us, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->read_strobe_us, h.read_strobe_us, v->name);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(v->address_lines, h.bus_config.address_lines,
                                              ADDRESS_LINES_SIZE, v->name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->rw_line, h.bus_config.rw_line, v->name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->vpp_line, h.bus_config.vpp_line, v->name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->static_high_mask, h.bus_config.static_high_mask, v->name);
    }
}

void test_every_refuse_vector_is_refused_with_its_code(void) {
    for (size_t i = 0; i < CF_REFUSE_VECTOR_COUNT; i++) {
        const cf_refuse_vector_t* v = &CF_REFUSE_VECTORS[i];
        firestarter_handle_t h = {};
        uint8_t expected = v->refusal == 0 ? CMD_DECODE_BAD_FRAME : v->refusal;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, load(&h, v->frame, v->frame_len), v->name);
    }
}

/* A refused memory frame writes no command parameter: nothing half-applied
 * can reach configure_memory. */
void test_refused_memory_frame_leaves_every_parameter_untouched(void) {
    for (size_t i = 0; i < CF_REFUSE_VECTOR_COUNT; i++) {
        const cf_refuse_vector_t* v = &CF_REFUSE_VECTORS[i];
        firestarter_handle_t h = {};
        poison(&h);
        load(&h, v->frame, v->frame_len);
        assert_poisoned(&h, v->name);
    }
}

void test_one_byte_short_and_long_frames_are_refused(void) {
    const cf_accept_vector_t* v = accept_vector("write_w27c512");
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CF_REFUSE_LENGTH, load(&h, v->frame, v->frame_len - 1));
    uint8_t longer[CF_HEADER_SIZE + CF_MEMORY_SIZE + 1] = {};
    memcpy(longer, v->frame, v->frame_len);
    TEST_ASSERT_EQUAL_UINT8(CF_REFUSE_LENGTH, load(&h, longer, sizeof(longer)));
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load(&h, v->frame, v->frame_len));
}

/* The JSON parser derived these; configure_memory and the address remap
 * depend on them. */
void test_bus_derived_values(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_w27c512"));
    TEST_ASSERT_EQUAL_UINT8(16, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0xFFFF, h.bus_config.address_mask);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "read_27c801_with_timing"));
    TEST_ASSERT_EQUAL_UINT8(17, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0x0071FFFF, h.bus_config.address_mask);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_at28c256_sdp"));
    TEST_ASSERT_EQUAL_UINT8(14, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0xBFFF, h.bus_config.address_mask);

    /* No address lines: the low 16 bits pass straight through. */
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "dev_address"));
    TEST_ASSERT_EQUAL_UINT8(0, h.bus_config.matching_lines);
    TEST_ASSERT_EQUAL_UINT32(0xFFFF, h.bus_config.address_mask);
}

/* Issue #118: the firmware reuses one handle for every command. */
void test_second_command_does_not_inherit_chip_or_bus_parameters(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "read_27c801_with_timing"));
    TEST_ASSERT_EQUAL_UINT32(5, h.read_settling_us);
    TEST_ASSERT_EQUAL_UINT32(0x1000, h.address);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "read_2716_vpp_line"));
    TEST_ASSERT_EQUAL_UINT32(0, h.address);
    TEST_ASSERT_EQUAL_UINT32(0, h.read_settling_us);
    TEST_ASSERT_EQUAL_UINT32(0, h.read_strobe_us);
    TEST_ASSERT_EQUAL_UINT16(0, h.chip_id);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.address_lines[11]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, h.bus_config.address_lines[19]);
    TEST_ASSERT_EQUAL_UINT8(11, h.bus_config.vpp_line);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)1 << 13, h.bus_config.static_high_mask);
}

/* A command without a memory payload must not keep the chip fields of the
 * command before it. */
void test_command_without_memory_payload_clears_the_chip_fields(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_w27c512"));
    configure_memory(&h);
    TEST_ASSERT_NOT_EQUAL(0, h.protocol);
    TEST_ASSERT_NOT_EQUAL(0, h.pulse_delay);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "fw_version"));
    TEST_ASSERT_EQUAL_UINT8(0, h.protocol);
    TEST_ASSERT_EQUAL_UINT32(0, h.pulse_delay);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_w27c512"));
    configure_memory(&h);
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_read"));
    TEST_ASSERT_EQUAL_UINT8(0, h.protocol);
    TEST_ASSERT_EQUAL_UINT32(0, h.pulse_delay);
}

void test_decoder_writes_no_state_outside_the_command_parameters(void) {
    firestarter_handle_t h = {};
    h.operation_state = 7;
    h.response_code = RESPONSE_CODE_WARNING;
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_w27c512"));
    TEST_ASSERT_EQUAL_UINT8(7, h.operation_state);
    TEST_ASSERT_EQUAL_UINT8(RESPONSE_CODE_WARNING, h.response_code);
    TEST_ASSERT_NULL(h.firestarter_operation_main);
}

/* pulse_delay 0 on the wire means "use the protocol default", resolved by
 * configure_memory for the chip in THIS command. */
void test_eprom_default_pulse_is_for_the_current_chip(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "write_w27c512"));
    configure_memory(&h);
    TEST_ASSERT_EQUAL_UINT32(100, h.pulse_delay);

    h.response_code = RESPONSE_CODE_OK;
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "read_2716_vpp_line"));
    TEST_ASSERT_EQUAL_UINT32(0, h.pulse_delay);
    configure_memory(&h);
    TEST_ASSERT_EQUAL_UINT32(500, h.pulse_delay);
}

/* Dev commands share the memory payload but not the required-field rule. */
void test_dev_address_needs_no_chip_fields(void) {
    firestarter_handle_t h = {};
    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "dev_address"));
    TEST_ASSERT_EQUAL_UINT32(0x1234, h.address);
    TEST_ASSERT_EQUAL_UINT32(0, h.mem_size);
}

void test_config_apply_writes_only_present_values(void) {
    rurp_configuration_t config = {};
    config.r1 = 1;
    config.r2 = 2;
    config.hardware_revision = 3;
    config.bandgap_mv = 4;
    firestarter_handle_t h = {};

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_read"));
    TEST_ASSERT_EQUAL_UINT8(0, cmd_apply_config(&h, &config));
    TEST_ASSERT_EQUAL_INT32(1, config.r1);
    TEST_ASSERT_EQUAL_UINT16(4, config.bandgap_mv);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_set_divider"));
    TEST_ASSERT_EQUAL_UINT8(CMD_CFG_FIELD_CHANGED, cmd_apply_config(&h, &config));
    TEST_ASSERT_EQUAL_INT32(270000, config.r1);
    TEST_ASSERT_EQUAL_INT32(44000, config.r2);
    TEST_ASSERT_EQUAL_UINT8(3, config.hardware_revision);
    TEST_ASSERT_EQUAL_UINT16(4, config.bandgap_mv);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_clear_rev"));
    TEST_ASSERT_EQUAL_UINT8(CMD_CFG_FIELD_CHANGED, cmd_apply_config(&h, &config));
    TEST_ASSERT_EQUAL_UINT8(0xFF, config.hardware_revision);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_bandgap"));
    TEST_ASSERT_EQUAL_UINT8(CMD_CFG_FIELD_CHANGED, cmd_apply_config(&h, &config));
    TEST_ASSERT_EQUAL_UINT16(1100, config.bandgap_mv);

    TEST_ASSERT_EQUAL_UINT8(CMD_DECODE_OK, load_vector(&h, "config_calibrate"));
    TEST_ASSERT_EQUAL_UINT8(CMD_CFG_CALIBRATE, cmd_apply_config(&h, &config));
    TEST_ASSERT_EQUAL_UINT16(5012, cmd_config_measured_vcc_mv(&h));
    TEST_ASSERT_EQUAL_UINT16(1100, config.bandgap_mv);
}

/* The frame's revision limit is a copy of the highest shield revision. A
 * new revision must raise both, or the decoder refuses its override. */
void test_revision_limit_is_the_highest_shield_revision(void) {
    TEST_ASSERT_EQUAL_UINT8(REVISION_2_3, CF_REVISION_MAX);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_every_accept_vector_decodes_to_its_fields);
    RUN_TEST(test_every_refuse_vector_is_refused_with_its_code);
    RUN_TEST(test_refused_memory_frame_leaves_every_parameter_untouched);
    RUN_TEST(test_one_byte_short_and_long_frames_are_refused);
    RUN_TEST(test_bus_derived_values);
    RUN_TEST(test_second_command_does_not_inherit_chip_or_bus_parameters);
    RUN_TEST(test_command_without_memory_payload_clears_the_chip_fields);
    RUN_TEST(test_decoder_writes_no_state_outside_the_command_parameters);
    RUN_TEST(test_eprom_default_pulse_is_for_the_current_chip);
    RUN_TEST(test_dev_address_needs_no_chip_fields);
    RUN_TEST(test_config_apply_writes_only_present_values);
    RUN_TEST(test_revision_limit_is_the_highest_shield_revision);
    return UNITY_END();
}
