//
// Created by torq on 6/20/26.
//

#ifndef WAVEVIEW_RING_BUFFER_H
#define WAVEVIEW_RING_BUFFER_H

#include <stdatomic.h>

/**
 * lock-free ring buffer for interleaved audio samples
 *
 * buffer stores floats in interleaved order
 *      [ch0, ch1, ch0, ch1, ...]
 * channels field tells callers how the floats are grouped but dows not change r/w
 *
 * Thread safety:
 *  - write from audio callback (producer)
 *  - read from gui thread (consumer)
 *  - use C11 atomics for index tracing - never use mutexes!!
 */
typedef struct {
    float *buffer;
    int size; //total floats the buffer can hold
    int channels; //number of interleaved channels
    atomic_int write_idx;
    atomic_int read_idx;
} RingBuffer;

/**
 * Create a ring buffer.
 * @param size      Total floats (frames × channels)
 * @param channels  Number of interleaved channels (informational)
 */
RingBuffer* ring_buffer_create(int size, int channels);

void ring_buffer_destroy(RingBuffer *rb);

/**
 * Write `samples` floats (not frames) into the ring buffer.
 * @param rb       Ring buffer
 * @param data     Interleaved floats
 * @param samples  Number of floats (frames × channels)
 * @return         Number of floats actually written
 */
int ring_buffer_write(RingBuffer *rb, const float *data, int samples);

/**
 * Read up to `samples` floats from the ring buffer.
 * @param rb       Ring buffer
 * @param out      Destination buffer
 * @param samples  Maximum floats to read
 * @return         Number of floats actually read
 */
int ring_buffer_read(RingBuffer *rb, float *out, int samples);

#endif //WAVEVIEW_RING_BUFFER_H
