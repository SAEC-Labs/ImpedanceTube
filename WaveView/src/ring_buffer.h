//
// Created by torq on 6/20/26.
//

#ifndef SAECTUBE_RING_BUFFER_H
#define SAECTUBE_RING_BUFFER_H

#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    float *buffer;
    int size;
    atomic_int write_idx;
    atomic_int read_idx;
} RingBuffer;

RingBuffer* ring_buffer_create(int size);
void ring_buffer_destroy(RingBuffer *rb);
int ring_buffer_write(RingBuffer *rb, const float *data, int frames);
int ring_buffer_read(RingBuffer *rb, float *out, int max_frames);

#endif //SAECTUBE_RING_BUFFER_H
