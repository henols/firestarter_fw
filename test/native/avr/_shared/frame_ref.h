/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 *
 * Test-side reference implementations of the wire framing: CRC8 and COBS.
 *
 * These are independent of the production code (no CRC8_TABLE, no
 * production encoder), so a suite that compares production output against
 * them fails if the production side drifts off-spec.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/* Reference CRC8 (poly 0x07, seed 0x00, no reflection, no final XOR),
 * table-free. */
static inline uint8_t ref_crc8(const uint8_t* data, size_t n) {
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

/* COBS-encodes `src[0..len-1]` into `dst` and returns the encoded byte count,
 * without the trailing 0x00 delimiter (the caller appends it).
 *
 * Standard COBS: each run of up to 254 non-zero bytes is emitted as a
 * run-code byte (run_len + 1) followed by the bytes. A 0x00 payload byte
 * ends a run. A 254-byte run has code 0xFF and no implicit zero follows it. */
static inline size_t test_cobs_encode(const uint8_t* src, size_t len, uint8_t* dst) {
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

/* Appends one frame to `out_vec`: COBS(payload + CRC8(payload)) + 0x00.
 * This is what rurp_communication_read_data() receives. A data-block frame
 * also has a leading '#', which the caller adds when it needs one. */
static inline void build_cobs_frame_bytes(
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
