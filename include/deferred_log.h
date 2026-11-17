/*
 * Project Name: Firestarter
 * Copyright (c) 2024 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

#ifndef __DEFERRED_LOG_H__
#define __DEFERRED_LOG_H__

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Frames held while the wire is the data bus (the Uno-class boards in
 * programmer mode) and sent when communication mode returns.
 *
 * Sizing: DEBUG frames compile out in production and an operation emits at
 * most one or two frames per programmer-mode window, so 4 slots has headroom.
 * Narrow frames only: the wide MSG_DATA_CHUNK path runs in communication
 * mode. A frame longer than DEFERRED_PARAM_MAX is truncated; no narrow frame
 * sent in programmer mode is longer.
 */
#define DEFERRED_LOG_MAX 4
#define DEFERRED_PARAM_MAX 8  // widest narrow frame (U32_U32 / U16x4 / progress) = 8 bytes

typedef struct {
    uint8_t id;
    uint8_t len;
    uint8_t params[DEFERRED_PARAM_MAX];
} deferred_frame_t;

typedef struct {
    uint8_t count;
    deferred_frame_t frames[DEFERRED_LOG_MAX];
} deferred_log_t;

/*
 * INFO (0x40-0x7F) and WARN (0x80-0x9F) frames. messages.h carries ids only,
 * so the catalog's id bands are the severity information available here.
 * The host never acks or waits for these frames, so dropping one loses a
 * diagnostic but never stalls the protocol. Control, error and data frames
 * do not have that property.
 */
static inline bool deferred_log_is_droppable(uint8_t id) {
    return id >= 0x40 && id < 0xA0;
}

/*
 * Queue one frame. When the queue is full, a frame that is not droppable
 * (an error, for example) replaces the newest droppable frame, and the
 * frames after it move up one slot so the send order stays the same.
 * Returns false when the frame itself is dropped: the queue is full and it
 * is droppable, or the queue holds no droppable frame.
 */
static inline bool deferred_log_push(deferred_log_t* log, uint8_t id, const uint8_t* params,
                                     uint8_t param_count) {
    if (log->count >= DEFERRED_LOG_MAX) {
        if (deferred_log_is_droppable(id)) {
            return false;
        }
        uint8_t victim = DEFERRED_LOG_MAX;
        do {
            if (victim == 0) {
                return false;
            }
            victim--;
        } while (!deferred_log_is_droppable(log->frames[victim].id));
        memmove(&log->frames[victim], &log->frames[victim + 1],
                (DEFERRED_LOG_MAX - 1 - victim) * sizeof(deferred_frame_t));
        log->count--;
    }
    if (param_count > DEFERRED_PARAM_MAX) {
        param_count = DEFERRED_PARAM_MAX;
    }
    deferred_frame_t* frame = &log->frames[log->count++];
    frame->id = id;
    frame->len = param_count;
    for (uint8_t i = 0; i < param_count; i++) {
        frame->params[i] = params[i];
    }
    return true;
}

#endif  // __DEFERRED_LOG_H__
