//
// Created by torq on 6/20/26.
//

/*
 * For real-time audio application, we need a ring buffer because we have two threads running at completely different speeds.
 * 1. PortAudio callback thread: very fast to capture mic samples and must return quickly
 * 2. GTK GUI thread: very slow, updates the display and draws waveforms
 *The PA callback is the producer and the GUI the consumer. The PA callback will write data to the buffer continuously, and
 *the GUI consumer reads samples from the data periodically.
 *If the GUI tries to read directly from the callback, they will interfere.
 *If the callback waits for the GUI, audio will glitch.
 *
 *so this ring buffer is important.
*/

#include "ring_buffer.h"
#include <stdlib.h>
//#include <string.h>
//#include <stdatomic.h>

RingBuffer* ring_buffer_create(const int size) {
    RingBuffer *rb = (RingBuffer*) calloc(1, sizeof(RingBuffer));
    rb->buffer = (float*) calloc(size, sizeof(float));
    rb->size = size;

    rb->write_idx = 0;
    rb->read_idx = 0;

    return rb;
}

void ring_buffer_destroy(RingBuffer *rb) {
    if (!rb) return;
    free(rb->buffer);
    free(rb);
}

int ring_buffer_write(RingBuffer *rb, const float *data, int frames) {
    //int write_idx = atomic_load(&rb->write_idx);
    int write_idx = rb->write_idx;
    int read_idx = rb->read_idx;

    int available = rb->size - 1 - (write_idx - read_idx + rb->size) % rb->size;
    int to_write = (frames < available) ? frames : available;

    for (int i = 0; i < to_write; i++) {
        int idx = (write_idx + i) % rb->size;
        rb->buffer[idx] = data[i];
    }

    rb->write_idx =  (write_idx + to_write) % rb->size;
    return to_write;
}

int ring_buffer_read(RingBuffer *rb, float *out, int max_frames) {
    int read_idx = rb->read_idx;
    int write_idx = rb->write_idx;

    int available = (write_idx - read_idx + rb->size) % rb->size;
    int to_read = (available < max_frames) ? available : max_frames;

    for (int i = 0; i < to_read; i++) {
        int idx = (read_idx + i) % rb->size;
        out[i] = rb->buffer[idx];
    }

    rb->read_idx = (read_idx + to_read) % rb->size;
    return to_read;
}









