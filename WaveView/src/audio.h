//
// Created by torq on 6/20/26.
//

/*
 * Audio capture interface, using the built-in mic, using PortAudio. samples are delivered via a callback and
 * written to the thread safe ring buffer
 */
#ifndef SAECTUBE_AUDIO_H
#define SAECTUBE_AUDIO_H

#include "ring_buffer.h"

/**
 * Initialize the audio capture subsystem.
 *
 * @param rb  Pointer to a ring buffer where audio samples will be written.
 *            Must be created and valid before calling this function.
 * @return    0 on success, -1 on failure.
 */
int audio_init(RingBuffer *rb);

/**
 * Start audio capture.
 *
 * The callback begins running and samples are written to the ring buffer.
 * @return 0 on success, -1 on failure.
 */
int audio_start(void);

/**
 * Stop audio capture.
 *
 * The callback stops running. Already captured samples remain in the
 * ring buffer and can still be read.
 * @return 0 on success, -1 on failure.
 */
int audio_stop(void);

/**
 * Get the name of the audio device being used.
 *
 * @return Pointer to a static string containing the device name,
 *         or "Unknown" if not initialized.
 */
const char* audio_get_device_name(void);


/**
 * Shut down the audio subsystem and free all resources.
 *
 * Stops the stream if running, closes it, and terminates PortAudio.
 * Safe to call multiple times.
 */
void audio_terminate(void);

#endif //SAECTUBE_AUDIO_H
