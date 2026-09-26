/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Firmware COBS decode-in-place + bounded-resync
 * Unity suite (RED scaffold).
 *
 * Tests `rurp_communication_read_data()` against the Phase-49-frozen COBS
 * frame contract:
 *
 *     [COBS(payload + CRC8)][0x00 delimiter]
 *
 * Mock wiring: uses `serial_read_mock.h` to drive a queued std::vector<uint8_t>
 * into `Serial.read` / `Serial.available` / `Serial.peek` —
 * mirroring the write-mock cadence in test_rurp_log_id.cpp.
 *
 * Reference CRC: table-free ref_crc8 (copied from test_rurp_log_id.cpp) to
 * build expected CRC8 bytes independently of the production PROGMEM table.
 *
 * Scope guard: this file does NOT call the frozen log/telemetry functions.
 * The magic-preamble frame path is UNCHANGED in v1.10 (ADR §4.2).
 */

#include <Arduino.h>
#include <ArduinoFake.h>
#include <unity.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

/* Shared mock helper — wires Serial.read/available/peek to a queued vector. */
#include "serial_read_mock.h"

extern "C" {
#include "rurp_shield.h"
#include "firestarter.h"
}

using namespace fakeit;

/* Shared test state                                                         */

static char data_buffer[DATA_BUFFER_SIZE];
static std::vector<uint8_t> rx_queue;
static size_t rx_pos;

/* Reference CRC8 (poly 0x07, seed 0x00, no refl, no XOR) — table-free.    */
/* Copied from test_rurp_log_id.cpp:76-85 so this suite is fully independent */
/* of the production CRC8_TABLE in rurp_serial_utils.cpp.                   */
static uint8_t ref_crc8(const uint8_t* data, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc & 0x80)
                      ? (uint8_t)((crc << 1) ^ 0x07)
                      : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* COBS encode helper — test-side only, used to build expected byte streams. */
/*                                                                           */
/* Encodes `src[0..len-1]` into `dst`, returns encoded byte count (without  */
/* the trailing 0x00 delimiter — caller appends it).                        */
/*                                                                           */
/* Standard COBS: scan for 0x00 or end of run (≤254 non-zero bytes), emit  */
/* run-code byte (run_len+1), then the non-zero bytes.  A 0x00 payload byte */
/* emits a run-code of 1 with no data bytes.  A 254-run code byte is 0xFF   */
/* (no implicit zero follows it per RFC COBS).                               */
static size_t test_cobs_encode(const uint8_t* src, size_t len, uint8_t* dst) {
    size_t out = 0;
    size_t code_pos = out++;        /* placeholder for first run-code */
    uint8_t code = 1;               /* run_len + 1 */

    for (size_t i = 0; i < len; i++) {
        if (src[i] == 0x00) {
            dst[code_pos] = code;
            code_pos = out++;
            code = 1;
        } else {
            dst[out++] = src[i];
            code++;
            if (code == 0xFF) {
                /* 254-run boundary: emit now, start new run without zero */
                dst[code_pos] = code;
                code_pos = out++;
                code = 1;
            }
        }
    }
    dst[code_pos] = code;
    return out;
}

/* Build the COBS frame byte sequence (without leading '#') into `out_vec`:
 *   COBS(payload + CRC8(payload)) + 0x00
 * This is what rurp_communication_read_data() receives after '#' is consumed. */
static void build_cobs_frame_bytes(
    const uint8_t* payload, size_t payload_len,
    std::vector<uint8_t>& out_vec
) {
    uint8_t crc = ref_crc8(payload, payload_len);

    /* Logical stream = payload + crc byte */
    std::vector<uint8_t> src(payload, payload + payload_len);
    src.push_back(crc);

    /* COBS-encode: max encoded size = len + len/254 + 1 */
    size_t max_enc = src.size() + src.size() / 254 + 2;
    std::vector<uint8_t> encoded(max_enc);
    size_t enc_len = test_cobs_encode(src.data(), src.size(), encoded.data());

    out_vec.insert(out_vec.end(), encoded.data(), encoded.data() + enc_len);
    out_vec.push_back(0x00);  /* frame delimiter */
}

/* setUp / tearDown                                                          */

/* Monotonically-increasing millis counter used by the millis() mock.
 * The current (pre-COBS) decoder has a 2 s timeout loop that calls millis().
 * Incrementing by 100 each call ensures that at most ~20 calls exhaust the
 * 2000 ms window, preventing an infinite spin when the mock queue is empty. */
static unsigned long millis_counter;

void setUp(void) {
    ArduinoFakeReset();
    rx_queue.clear();
    rx_pos = 0;
    millis_counter = 0;
    memset(data_buffer, 0, sizeof(data_buffer));

    /* Stub flush() so ArduinoFake doesn't warn. */
    When(Method(ArduinoFake(Serial), flush)).AlwaysReturn();

    /* Stub write so ArduinoFake doesn't complain if the decode path ever
     * triggers an error log that touches Serial.write (shouldn't, but safe). */
    When(OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)))
        .AlwaysReturn((size_t)1);

    /* millis() — monotonically increasing so the 2 s timeout in the current
     * len_u16 decoder fires and returns -3 instead of spinning forever.
     * Each invocation advances by 100 ms. */
    When(Method(ArduinoFake(Function), millis))
        .AlwaysDo([&]() -> unsigned long {
            millis_counter += 100;
            return millis_counter;
        });
}

void tearDown(void) {
}

/* Feed COBS(payload+CRC8) + 0x00 for a known small payload.                */
/* Assert rurp_communication_read_data returns the decoded length and the    */
/* buffer matches the payload.                                               */
/*                                                                           */
/* RED against the current len_u16+XOR decoder: the current code reads 2    */
/* bytes as a length prefix; with COBS bytes those will be wildly wrong,    */
/* causing either a timeout (-3), oversize (-2), or checksum mismatch (-4). */
void test_cobs_decode_valid_frame(void) {
    /* Small known payload: {0x10, 0x20, 0x30} */
    uint8_t payload[] = { 0x10, 0x20, 0x30 };
    size_t payload_len = sizeof(payload);

    build_cobs_frame_bytes(payload, payload_len, rx_queue);
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    /* GREEN condition (Wave 2): res == payload_len AND buffer == payload */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res);
    TEST_ASSERT_EQUAL_size_t(payload_len, (size_t)res);
    TEST_ASSERT_EQUAL_MEMORY(payload, data_buffer, payload_len);
}

/* Feed [garbled frame][0x00][valid frame][0x00].                            */
/* Assert:                                                                   */
/*   (a) First rurp_communication_read_data call returns res < 0.            */
/*   (b) A second call returns the correct decoded length with data_buffer   */
/*       matching the expected payload (bounded recovery, not mere detection */
/*       — VALIDATION.md SC2 shape).                                         */
void test_cobs_resync_bounded(void) {
    /* Valid payload for the SECOND frame. */
    uint8_t good_payload[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    size_t good_len = sizeof(good_payload);

    /* Garbled frame: a sequence of non-zero junk bytes ending in 0x00.
     * COBS body of an all-0xFF "payload" of length 4 — will fail CRC. */
    uint8_t junk[] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t bad_crc = ref_crc8(junk, sizeof(junk)) ^ 0xAA;  /* deliberate flip */
    uint8_t bad_src[] = { 0xFF, 0xFF, 0xFF, 0xFF, bad_crc };
    /* COBS-encode the junk: all non-zero → single run */
    uint8_t bad_encoded[16];
    size_t bad_enc_len = test_cobs_encode(bad_src, sizeof(bad_src), bad_encoded);
    for (size_t i = 0; i < bad_enc_len; i++) {
        rx_queue.push_back(bad_encoded[i]);
    }
    rx_queue.push_back(0x00);  /* garbled frame delimiter */

    /* Append valid frame. */
    build_cobs_frame_bytes(good_payload, good_len, rx_queue);

    setup_serial_read_mock(rx_queue, rx_pos);

    /* First call: must return error (res < 0). */
    int res1 = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);
    TEST_ASSERT_LESS_THAN_INT(0, res1);

    /* After draining to 0x00, the read cursor is re-anchored at the start
     * of the valid frame.  Second call must decode it correctly. */
    int res2 = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res2);
    TEST_ASSERT_EQUAL_size_t(good_len, (size_t)res2);
    TEST_ASSERT_EQUAL_MEMORY(good_payload, data_buffer, good_len);
}

/* This test is updated to use DATA_BUFFER_SIZE-1 (511) bytes of all-zero   */
/* payload — the maximum accepted size.  The COBS encoding property          */
/* (no 0x00 in the body before the delimiter) is still verified.             */
/*                                                                           */
/* Rationale: rurp_communication_read_data() is the HOST→FW command decoder. */
/* The fw→host data-block path uses MSG_DATA_CHUNK magic-preamble frames    */
/* (ADR §4.2, UNCHANGED in v1.10) and never passes a 512-byte payload       */
/* through this function in production.  The largest legitimate JSON command */
/* is ~422 B (CONTEXT.md), well under the 511-byte cap.                     */
void test_cobs_all_zero_payload(void) {
    const size_t max_accepted = DATA_BUFFER_SIZE - 1;
    static uint8_t zero_payload[DATA_BUFFER_SIZE - 1];
    memset(zero_payload, 0, max_accepted);

    build_cobs_frame_bytes(zero_payload, max_accepted, rx_queue);

    /* Verify the encoded body contains no 0x00 before the delimiter. */
    /* (The delimiter is the last byte.) */
    for (size_t i = 0; i + 1 < rx_queue.size(); i++) {
        TEST_ASSERT_NOT_EQUAL(0x00, (int)rx_queue[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x00, rx_queue.back());  /* delimiter IS 0x00 */

    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res);
    TEST_ASSERT_EQUAL_size_t(max_accepted, (size_t)res);
    for (int i = 0; i < res; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)data_buffer[i]);
    }
}

/* test_cobs_254_run_then_zero                                               */
void test_cobs_254_run_then_zero(void) {
    /* 254 nonzero bytes + one zero + three more bytes = 258 bytes total. */
    uint8_t payload[258];
    memset(payload, 0x01, 254);
    payload[254] = 0x00;
    payload[255] = 0x02;
    payload[256] = 0x03;
    payload[257] = 0x04;

    build_cobs_frame_bytes(payload, sizeof(payload), rx_queue);
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), (size_t)res);
    TEST_ASSERT_EQUAL_MEMORY(payload, data_buffer, sizeof(payload));
}

/* main                                                                      */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_cobs_decode_valid_frame);
    RUN_TEST(test_cobs_resync_bounded);
    RUN_TEST(test_cobs_all_zero_payload);
    RUN_TEST(test_cobs_254_run_then_zero);

    return UNITY_END();
}
