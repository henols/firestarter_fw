/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#include "march.h"

#include "firestarter.h"
#include "logging_id.h"
#include "messages.h"
#include "operation_utils.h"

// One March operation is two bits: bit 0 selects the all-1 background (0xFF)
// over all-0 (0x00), bit 1 selects a write over a read.
#define R0 0x0
#define R1 0x1
#define W0 0x2
#define W1 0x3

// One element is one byte: the first operation in bits 0-1, the second in
// bits 2-3, the operation count in bits 4-5 and the direction in bit 7.
#define UP(op) (uint8_t)(0x10 | (op))
#define UP2(a, b) (uint8_t)(0x20 | ((b) << 2) | (a))
#define DOWN2(a, b) (uint8_t)(0x80 | UP2(a, b))

// March C-: {up(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); up(r0)}.
// Each element runs over the full address range before the next one starts,
// so coupling faults between any two addresses are found.
static const uint8_t march_c_minus[] = {
    UP(W0), UP2(R0, W1), UP2(R1, W0), DOWN2(R0, W1), DOWN2(R1, W0), UP(R0),
};
#define MARCH_C_MINUS_ELEMENTS (sizeof(march_c_minus) / sizeof(march_c_minus[0]))

static uint8_t march_element;
static uint32_t march_index;

void march_begin(firestarter_handle_t* handle) {
    (void)handle;
    march_element = 0;
    march_index = 0;
}

void march_execute(firestarter_handle_t* handle) {
    const uint32_t size = handle->mem_size;
    const uint8_t element = march_c_minus[march_element];
    uint32_t index = march_index;

    for (uint16_t batch = MARCH_BATCH_SIZE; batch && index < size; batch--, index++) {
        // Descending order counts down from size - 1, so it never underflows.
        const uint32_t address = (element & 0x80) ? size - 1 - index : index;
        uint8_t ops = element;
        for (uint8_t n = (element >> 4) & 0x3; n; n--, ops >>= 2) {
            const uint8_t value = (ops & R1) ? 0xFF : 0x00;
            if (ops & W0) {
                handle->firestarter_set_data(handle, address, value);
                continue;
            }
            const uint8_t actual = handle->firestarter_get_data(handle, address);
            if (actual != value) {
                uint8_t _b[6] = {
                    march_element,
                    (uint8_t)((address >> 16) & 0xFF),
                    (uint8_t)((address >> 8) & 0xFF),
                    (uint8_t)(address & 0xFF),
                    value,
                    actual,
                };
                LOG_ERROR_ID_BYTES(MSG_ERR_RAM_TEST, _b, 6);
                handle->response_code = RESPONSE_CODE_ERROR;
                return;
            }
        }
    }

    if (index >= size) {
        march_element++;
        index = 0;
    }
    march_index = index;
    if (march_element < MARCH_C_MINUS_ELEMENTS) {
        set_operation_in_progress(handle);
        // One progress frame per batch, counted in address steps. On
        // SERIAL_ON_IO boards the frame waits in the deferred log until the
        // callback returns; one frame per callback cannot fill it. The host
        // does not ack it: a simple operation reads no input in MAIN, so an
        // ack would wait and answer the END phase's ACK wait too early.
        LOG_DATA_ID_U32_U32(MSG_DATA_PROGRESS, march_element * size + index, MARCH_C_MINUS_ELEMENTS * size);
    } else {
        clear_operation_in_progress(handle);
    }
}
