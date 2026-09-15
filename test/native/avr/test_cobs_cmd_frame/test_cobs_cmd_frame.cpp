/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Firmware COBS command-frame decode + CRC8-reject +
 * bounded-recovery Unity suite.
 *
 * Tests `rurp_communication_read_data()` against the Phase-49/50-frozen COBS
 * frame contract for the HOST→FW command channel:
 *
 *     [COBS(payload + CRC8)][0x00 delimiter]
 *
 * Command frames have NO '#' preamble byte (unlike the data-block path).
 * The rx_queue is COBS-body + 0x00 only.
 *
 * These tests exercise the decode primitive directly.  The headline
 * RED→GREEN for the CMD_IDLE wiring surgery is in Task 2 (firestarter.cpp);
 * the cases here establish the behavioral contract that Task-2 surgery must
 * satisfy.
 *
 * Mock wiring: uses `serial_read_mock.h` to drive a queued std::vector<uint8_t>
 * into `Serial.read` / `Serial.available` / `Serial.peek` —
 * mirroring the data-frame suite in test_cobs_data_frame.cpp.
 *
 * Reference CRC: table-free ref_crc8 (copied from test_rurp_log_id.cpp) to
 * build expected CRC8 bytes independently of the production PROGMEM table.
 *
 * Scope guard: this file does NOT call the frozen log/telemetry functions.
 * The magic-preamble data-block frame path is UNCHANGED in v1.10 (ADR §4.2).
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

/* Build the COBS frame byte sequence (no '#' preamble for command frames) into
 * `out_vec`:
 *   COBS(payload + CRC8(payload)) + 0x00
 * This is what rurp_communication_read_data() receives on the command channel. */
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

    /* millis() — monotonically increasing counter.  The COBS decoder does
     * NOT have a wall-clock timeout loop (the 2 s cascade was deleted in
     * SC1 win).  This stub is retained for defensive
     * completeness in case any helper path ever calls millis(). */
    When(Method(ArduinoFake(Function), millis))
        .AlwaysDo([&]() -> unsigned long {
            millis_counter += 100;
            return millis_counter;
        });
}

void tearDown(void) {
}

/* test_cobs_decode_valid_json_command                                       */
/*                                                                           */
/* Feed COBS(payload+CRC8) + 0x00 for a small valid JSON payload.           */
/* Assert rurp_communication_read_data returns the payload length and the    */
/* buffer matches the JSON bytes.                                            */
/*                                                                           */
/* This is the GREEN contract: a well-formed command frame decodes into      */
/* data_buffer and reaches the JSON parser with the exact payload.           */
void test_cobs_decode_valid_json_command(void) {
    /* Small known JSON payload: {"state":13} */
    const uint8_t payload[] = {
        '{', '"', 's', 't', 'a', 't', 'e', '"', ':', '1', '3', '}'
    };
    size_t payload_len = sizeof(payload);

    build_cobs_frame_bytes(payload, payload_len, rx_queue);
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res);
    TEST_ASSERT_EQUAL_size_t(payload_len, (size_t)res);
    TEST_ASSERT_EQUAL_MEMORY(payload, data_buffer, payload_len);
}

/* test_cobs_crc_reject_does_not_reach_parser (V5 / §4.4 headline)          */
/*                                                                           */
/* COBS-encode a payload but deliberately flip the CRC byte.                */
/* Assert rurp_communication_read_data returns < 0.                         */
/*                                                                           */
/* This is the proof that a CRC-failing frame is REJECTED by the decode     */
/* primitive before any JSON parse path.  T-51-01 mitigation assurance.     */
void test_cobs_crc_reject_does_not_reach_parser(void) {
    const uint8_t payload[] = {
        '{', '"', 'c', 'm', 'd', '"', ':', '1', '}'
    };
    size_t payload_len = sizeof(payload);

    /* Compute real CRC, then flip one bit to corrupt it. */
    uint8_t real_crc = ref_crc8(payload, payload_len);
    uint8_t bad_crc  = real_crc ^ 0x55;  /* deliberate flip */

    /* Build the logical stream: payload + corrupted CRC */
    std::vector<uint8_t> src(payload, payload + payload_len);
    src.push_back(bad_crc);

    /* COBS-encode the corrupted stream. */
    size_t max_enc = src.size() + src.size() / 254 + 2;
    std::vector<uint8_t> encoded(max_enc);
    size_t enc_len = test_cobs_encode(src.data(), src.size(), encoded.data());
    rx_queue.insert(rx_queue.end(), encoded.data(), encoded.data() + enc_len);
    rx_queue.push_back(0x00);  /* frame delimiter */

    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    /* The decoder MUST return negative — CRC mismatch rejects before parse. */
    TEST_ASSERT_LESS_THAN_INT(0, res);
}

/* test_cobs_resync_bounded                                                  */
/*                                                                           */
/* Feed [garbled frame][0x00][valid frame][0x00].                            */
/* Assert:                                                                   */
/*   (a) First rurp_communication_read_data call returns res < 0.            */
/*   (b) A second call returns the correct decoded length with data_buffer   */
/*       matching the expected payload (bounded recovery, not mere detection).*/
void test_cobs_resync_bounded(void) {
    /* Valid payload for the SECOND frame. */
    uint8_t good_payload[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    size_t good_len = sizeof(good_payload);

    /* Garbled frame: junk bytes with a deliberately-wrong CRC ending in 0x00. */
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

/* test_cobs_oversized_frame_bounded_recovery                                */
/*                                                                           */
/* Feed a frame whose decoded payload would exceed DATA_BUFFER_SIZE (no      */
/* 0x00 within the cap), followed by a 0x00 and then a valid frame.         */
/* Assert:                                                                   */
/*   (a) First call returns < 0 (overflow drain, code -2).                  */
/*   (b) Second call recovers and decodes the valid frame — no hang.         */
/*                                                                           */
/* CMD_FRAME_MAX / DATA_BUFFER_SIZE overflow guard — T-51-02 mitigation.     */
void test_cobs_oversized_frame_bounded_recovery(void) {
    /* Build an oversized payload: DATA_BUFFER_SIZE + 4 bytes of non-zero data.
     * All non-zero so the COBS encoding produces a simple long run (no zeros
     * in payload means COBS encoded body is payload_len + 1 code byte). */
    const size_t oversized_len = DATA_BUFFER_SIZE + 4;
    std::vector<uint8_t> big_payload(oversized_len, 0x42);

    build_cobs_frame_bytes(big_payload.data(), big_payload.size(), rx_queue);

    /* Append a valid short frame to verify recovery. */
    uint8_t good_payload[] = { 0x01, 0x02, 0x03 };
    build_cobs_frame_bytes(good_payload, sizeof(good_payload), rx_queue);

    setup_serial_read_mock(rx_queue, rx_pos);

    /* First call: oversized frame → overflow drain → return < 0 (code -2). */
    int res1 = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);
    TEST_ASSERT_LESS_THAN_INT(0, res1);

    /* Second call: must recover and decode the valid frame cleanly. */
    int res2 = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, res2);
    TEST_ASSERT_EQUAL_size_t(sizeof(good_payload), (size_t)res2);
    TEST_ASSERT_EQUAL_MEMORY(good_payload, data_buffer, sizeof(good_payload));
}

/* Feed a payload of EXACTLY DATA_BUFFER_SIZE (512) bytes — all 0x42 so     */
/* the COBS body is a clean non-zero run, no internal zeros.                 */
void test_cobs_exact_buffer_size_payload(void) {
    /* All-0x42 payload of exactly DATA_BUFFER_SIZE bytes — clean COBS run. */
    std::vector<uint8_t> payload(DATA_BUFFER_SIZE, 0x42);

    build_cobs_frame_bytes(payload.data(), payload.size(), rx_queue);
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    TEST_ASSERT_LESS_THAN_INT(0, res);
}

/* This pins that the largest legitimate command still decodes correctly AND  */
/* that n never exceeds DATA_BUFFER_SIZE-1 — so the caller's NUL write at    */
/* data_buffer[n] is provably in-bounds for every legal payload.             */
void test_cobs_max_accepted_payload(void) {
    const size_t max_len = DATA_BUFFER_SIZE - 1;  /* 511 bytes */
    std::vector<uint8_t> payload(max_len, 0x42);

    build_cobs_frame_bytes(payload.data(), payload.size(), rx_queue);
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    /* The 511-byte payload must decode successfully (n == DATA_BUFFER_SIZE-1). */
    TEST_ASSERT_EQUAL_INT((int)max_len, res);
    TEST_ASSERT_EQUAL_MEMORY(payload.data(), data_buffer, max_len);
}

/* Feed the COBS body of a small payload but WITHOUT the trailing 0x00       */
/* delimiter, then leave the queue exhausted (finite-stream mock: available()*/
/* returns 0, read() returns -1 after exhaustion).                           */
/*                                                                           */
/* The millis() stub in setUp increments +100 ms per call, so the           */
/* TIMEOUT_MS (1000 ms) inter-byte deadline in the Task-2 fix fires after    */
/* 10 stub calls — deterministically, with no real wall-clock time.          */
/*                                                                           */
/* Assert: rurp_communication_read_data() RETURNS a negative value.         */
/* The fact that the call returns at all (the test binary terminates) is the  */
/* proof that no infinite spin occurs.                                        */
/*                                                                           */
/* Before the fix (RED): the current `while (rurp_communication_available()  */
/* <= 0) {}` spin at lines 92 and 125 of rurp_serial_utils.cpp spins        */
/* forever once the queue is exhausted — hanging the test harness. This case */
/* therefore CANNOT be run cleanly before Task 2 applies the bounded-wait   */
/* fix. The RED defect is demonstrated by the hang itself; the GREEN outcome */
/* is a clean return with a negative result code.                            */
void test_cobs_truncated_frame_no_hang(void) {
    const uint8_t partial_payload[] = { 0x01, 0x02, 0x03, 0x04 };
    size_t payload_len = sizeof(partial_payload);

    uint8_t crc = ref_crc8(partial_payload, payload_len);

    /* Logical stream: payload + crc */
    std::vector<uint8_t> src(partial_payload, partial_payload + payload_len);
    src.push_back(crc);

    /* COBS-encode the logical stream — but do NOT append 0x00 (truncated). */
    size_t max_enc = src.size() + src.size() / 254 + 2;
    std::vector<uint8_t> encoded(max_enc);
    size_t enc_len = test_cobs_encode(src.data(), src.size(), encoded.data());
    rx_queue.insert(rx_queue.end(), encoded.data(), encoded.data() + enc_len);
    /* Deliberately NO rx_queue.push_back(0x00) — frame is truncated. */

    /* The finite-stream mock exhausts after enc_len bytes: available()
     * returns 0 and read() returns -1 from that point forward. */
    setup_serial_read_mock(rx_queue, rx_pos);

    int res = rurp_communication_read_data(data_buffer, DATA_BUFFER_SIZE - 1);

    /* Must return (no hang) with a negative error code. */
    TEST_ASSERT_LESS_THAN_INT(0, res);
}

/* main                                                                      */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_cobs_decode_valid_json_command);
    RUN_TEST(test_cobs_crc_reject_does_not_reach_parser);
    RUN_TEST(test_cobs_resync_bounded);
    RUN_TEST(test_cobs_oversized_frame_bounded_recovery);
    RUN_TEST(test_cobs_exact_buffer_size_payload);
    RUN_TEST(test_cobs_max_accepted_payload);
    RUN_TEST(test_cobs_truncated_frame_no_hang);

    return UNITY_END();
}
