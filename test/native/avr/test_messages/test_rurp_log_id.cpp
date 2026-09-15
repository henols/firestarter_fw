/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * native Unity tests for rurp_log_id() wire frame.
 *
 *     AA 55 AA 55 | len=1+param_count+1 | id | params | crc8 | 0A
 *
 * The CRC8 algorithm (poly 0x07, seed 0x00, no reflection, no final XOR)
 * is pinned by `test_crc_polynomial_smoke` — any silent refactor that
 * changes the polynomial fails this suite.
 *
 * Test cases:
 *  - test_zero_param_frame:  id=0x01, no params. Asserts CRC(0x01) == 0x07.
 *  - test_u32_param_frame:   id=0x84, params={0x00,0x01,0x00,0x00}.
 *    Generic u32-shape fixture using MSG_WARN_MEM_SIZE_TOO_SMALL.
 *  - test_multi_param_frame: id=0xB1, params={0x01,0xF4,0xA2,0x05,0x00,0x03}.
 *  - test_crc_polynomial_smoke: independent ref CRC vs frame CRC for an
 *    arbitrary payload — pins poly 0x07 seed 0x00.
 *
 * Build context: [env:native] src_filter is widened in platformio.ini to
 * include `boards/rurp_serial_utils.cpp` + `messages.c`. The production
 * _firestarter_emit_frame + CRC8 table + MSG_PARAM_BYTES_TABLE are linked
 * directly into the test binary (no test-side reimplementation).
 *
 * Serial bytes are captured via ArduinoFake's Method(Serial, write) mock —
 * one .AlwaysDo handler accumulates every write into a host std::vector.
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <vector>
#include <cstdint>

extern "C" {
#include "rurp_shield.h"
#include "messages.h"
}

using namespace fakeit;

// Capture buffer shared by all tests; cleared in setUp.
static std::vector<uint8_t> captured;

void setUp(void) {
    ArduinoFakeReset();
    captured.clear();

    // Mock every SERIAL_PORT.write(uint8_t) — accumulate into `captured`.
    // SerialFake has two write overloads (single-byte and buffer-form); use
    // OverloadedMethod to pick the single-byte one explicitly.
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysDo([](uint8_t b) -> size_t {
            captured.push_back(b);
            return (size_t)1;
        });

    // .flush() is no-op-safe; stub explicitly so ArduinoFake doesn't warn.
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();
}

void tearDown(void) {
}

// --- Reference CRC8 (poly 0x07, seed 0x00, no refl, no XOR). Table-free
// recomputation so the test is fully independent of CRC8_TABLE in
// rurp_serial_utils.cpp. If the production table ever drifts off-spec, this
// reference disagrees with the frame's CRC byte and the test fails. ---
static uint8_t ref_crc8(const uint8_t* data, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void test_zero_param_frame(void) {
    rurp_log_id(0x01, NULL, 0);

    // 4 magic + 2 len (u16 W-04) + 1 id + 1 crc + 1 anchor = 9 bytes.
    TEST_ASSERT_EQUAL_size_t(9, captured.size());

    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[2]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[4]);    // len MSB (= 0x00 since len <= 0xFF)
    TEST_ASSERT_EQUAL_HEX8(0x02, captured[5]);    // len LSB = 1 (id) + 0 (params) + 1 (crc)
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[6]);    // id

    TEST_ASSERT_EQUAL_HEX8(0x07, captured[7]);

    // Also verify against the table-free reference (defence-in-depth).
    uint8_t body[1] = { 0x01 };
    TEST_ASSERT_EQUAL_HEX8(ref_crc8(body, 1), captured[7]);

    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[8]);    // re-sync anchor
}

void test_u32_param_frame(void) {
    // MSG_WARN_MEM_SIZE_TOO_SMALL (id=0x84) with u32=0x00010000 encoded MSB-first.
    uint8_t params[4] = { 0x00, 0x01, 0x00, 0x00 };
    rurp_log_id(MSG_WARN_MEM_SIZE_TOO_SMALL, params, 4);

    // 4 magic + 2 len (u16 W-04) + 1 id + 4 params + 1 crc + 1 anchor = 13 bytes.
    TEST_ASSERT_EQUAL_size_t(13, captured.size());

    // Magic preamble.
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[2]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[3]);

    // len = 1 (id) + 4 (params) + 1 (crc) = 0x06, encoded as u16 big-endian.
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[4]);    // len MSB
    TEST_ASSERT_EQUAL_HEX8(0x06, captured[5]);    // len LSB

    // id.
    TEST_ASSERT_EQUAL_HEX8(0x84, captured[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)MSG_WARN_MEM_SIZE_TOO_SMALL, captured[6]);

    // Params.
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[7]);
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[10]);

    // CRC over [0x84, 0x00, 0x01, 0x00, 0x00] — table-free reference.
    uint8_t body[5] = { 0x84, 0x00, 0x01, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_HEX8(ref_crc8(body, 5), captured[11]);

    // Anchor.
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[12]);
}

void test_multi_param_frame(void) {
    // id=0xB1 (MSG_ERR_WRITE_FAILED) with u24=0x01F4A2 + u8=0x05 + u16=0x0003,
    // each encoded MSB-first by the caller. 6 param bytes total.
    uint8_t params[6] = { 0x01, 0xF4, 0xA2, 0x05, 0x00, 0x03 };
    rurp_log_id(0xB1, params, 6);

    // 4 + 2 (len u16 W-04) + 1 + 6 + 1 + 1 = 15 bytes.
    TEST_ASSERT_EQUAL_size_t(15, captured.size());

    // Magic.
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[2]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[3]);

    // len = 1 + 6 + 1 = 0x08, encoded as u16 big-endian.
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[4]);    // len MSB
    TEST_ASSERT_EQUAL_HEX8(0x08, captured[5]);    // len LSB

    // id.
    TEST_ASSERT_EQUAL_HEX8(0xB1, captured[6]);

    // Params.
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[7]);
    TEST_ASSERT_EQUAL_HEX8(0xF4, captured[8]);
    TEST_ASSERT_EQUAL_HEX8(0xA2, captured[9]);
    TEST_ASSERT_EQUAL_HEX8(0x05, captured[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[11]);
    TEST_ASSERT_EQUAL_HEX8(0x03, captured[12]);

    // CRC over [0xB1, 0x01, 0xF4, 0xA2, 0x05, 0x00, 0x03].
    uint8_t body[7] = { 0xB1, 0x01, 0xF4, 0xA2, 0x05, 0x00, 0x03 };
    TEST_ASSERT_EQUAL_HEX8(ref_crc8(body, 7), captured[13]);

    // Anchor.
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[14]);
}

void test_crc_polynomial_smoke(void) {
    // Independent CRC computation vs the frame's embedded CRC byte. If the
    // production CRC8_TABLE is ever regenerated with the wrong polynomial,
    // seed, reflection, or final XOR, the two values disagree.
    uint8_t params[4] = { 0x12, 0x34, 0x56, 0x78 };
    rurp_log_id(0x84, params, 4);

    // 4 magic + 2 len (u16 W-04) + 1 id + 4 params + 1 crc + 1 anchor = 13 bytes.
    TEST_ASSERT_EQUAL_size_t(13, captured.size());

    uint8_t body[5] = { 0x84, 0x12, 0x34, 0x56, 0x78 };
    uint8_t expected_crc = ref_crc8(body, 5);

    // CRC is the second-to-last byte (anchor 0x0A is the last).
    TEST_ASSERT_EQUAL_HEX8(expected_crc, captured[11]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[12]);
}

void test_ok_ready_u16_param_frame(void) {
    // SC2 contract: MSG_OK_READY emits a 2-byte big-endian param carrying
    // DATA_BUFFER_SIZE (512 on Uno). rurp_log_id_u16 packs MSB-first.
    // Frame: 4 magic + 2 len (u16) + 1 id + 2 params + 1 crc + 1 anchor = 11 bytes.
    // len = 1 (id) + 2 (params) + 1 (crc) = 4 = 0x0004.
    rurp_log_id_u16(MSG_OK_READY, 512);

    TEST_ASSERT_EQUAL_size_t(11, captured.size());

    // Magic preamble.
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, captured[2]);
    TEST_ASSERT_EQUAL_HEX8(0x55, captured[3]);

    // len = 4, encoded as u16 big-endian.
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[4]);    // len MSB
    TEST_ASSERT_EQUAL_HEX8(0x04, captured[5]);    // len LSB

    // id = MSG_OK_READY = 0x01.
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)MSG_OK_READY, captured[6]);

    // 2-byte big-endian param: 512 = 0x0200, MSB first.
    TEST_ASSERT_EQUAL_HEX8(0x02, captured[7]);    // 512 >> 8
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[8]);    // 512 & 0xFF

    // CRC over [0x01, 0x02, 0x00] (body = id + params).
    uint8_t body[3] = { 0x01, 0x02, 0x00 };
    TEST_ASSERT_EQUAL_HEX8(ref_crc8(body, 3), captured[9]);

    // Re-sync anchor.
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[10]);
}

void test_oversize_param_count_rejected(void) {
    // W-04: _firestarter_emit_frame guard raised from 253 to 65533.
    // `len = 1 (id) + param_count + 1 (crc)` is now a u16, so no uint8_t
    // param_count value (max 255) can exceed the new guard. The guard matters
    // for future callers that pass > 255 via an internal wide path.
    //
    // Under the u16 len, param_count = 253 (previously the largest accepted)
    // must still emit a well-formed frame.
    // param_count = 254, 255 also EMIT now (they were rejected under u8 len
    // because 254+2=256 wrapped; under u16 they are valid small values).
    //
    // Dummy params buffer — sized at 255 to cover the uint8_t maximum.
    uint8_t params[255];
    memset(params, 0xCC, sizeof(params));

    // --- 253 params: must emit (was the u8-era max; still accepted). ---
    // Frame: 4 magic + 2 len + 1 id + 253 params + 1 crc + 1 anchor = 262 bytes.
    // len_u16 = 1 + 253 + 1 = 255 (0x00FF).
    rurp_log_id(0x01, params, 253);
    TEST_ASSERT_EQUAL_size_t(262, captured.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[4]);   // len MSB = 0x00
    TEST_ASSERT_EQUAL_HEX8(0xFF, captured[5]);   // len LSB = 0xFF (255)
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[6]);   // id
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[261]); // anchor

    captured.clear();

    // --- 254 params: must emit (previously rejected; now valid under u16). ---
    // Frame: 4 + 2 + 1 + 254 + 1 + 1 = 263 bytes. len_u16 = 256 (0x0100).
    rurp_log_id(0x01, params, 254);
    TEST_ASSERT_EQUAL_size_t(263, captured.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[4]);   // len MSB = 0x01
    TEST_ASSERT_EQUAL_HEX8(0x00, captured[5]);   // len LSB = 0x00
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[6]);   // id
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[262]); // anchor

    captured.clear();

    // --- 255 params: must emit (previously rejected; now valid under u16). ---
    // Frame: 4 + 2 + 1 + 255 + 1 + 1 = 264 bytes. len_u16 = 257 (0x0101).
    rurp_log_id(0x01, params, 255);
    TEST_ASSERT_EQUAL_size_t(264, captured.size());
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[4]);   // len MSB = 0x01
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[5]);   // len LSB = 0x01
    TEST_ASSERT_EQUAL_HEX8(0x01, captured[6]);   // id
    TEST_ASSERT_EQUAL_HEX8(0x0A, captured[263]); // anchor
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_zero_param_frame);
    RUN_TEST(test_u32_param_frame);
    RUN_TEST(test_multi_param_frame);
    RUN_TEST(test_crc_polynomial_smoke);
    RUN_TEST(test_oversize_param_count_rejected);
    RUN_TEST(test_ok_ready_u16_param_frame);

    return UNITY_END();
}
