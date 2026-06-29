//
// Created by torq on 6/20/26.
//

/*
 * Audio capture interface, using the built-in mic, using PortAudio. samples are delivered via a callback and
 * written to the thread safe ring buffer
 */
#ifndef WAVEVIEW_AUDIO_H
#define WAVEVIEW_AUDIO_H

#include "ring_buffer.h"

/**
 * Device information structure
 */
typedef struct {
    int index;
    char name[256];
    int maxInputChannels;
} AudioDeviceInfo;

/**
 * Initialize the audio capture subsystem.
 *
 * @param rb  Pointer to a ring buffer where audio samples will be written.
 *            Must be created and valid before calling this function.
 * @return    0 on success, -1 on failure.
 */
int audio_init(RingBuffer *rb);

/**
 * Get number of available input devices
 * @return number of devices, or -1 if not initialized
 */
int audio_get_device_count(void);

/**
 * Get device information for a specific device index.
 * @param index  Device index (0 to audio_get_device_count()-1)
 * @return       Pointer to AudioDeviceInfo, or NULL if invalid
 */
const AudioDeviceInfo* audio_get_device_info(int index);

/**
 * Select an input device by its PortAudio device index.
 * The stream will be stopped, the device changed, and the stream restarted
 * if it was running.
 * @param device_index  PortAudio device index (from AudioDeviceInfo.index)
 * @return              0 on success, -1 on failure
 */
int audio_select_device(int device_index);

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
 * Check if the audio stream is currently running.
 * @return 1 if running, 0 if stopped, -1 on error
 */
int audio_is_running(void);

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

#endif //WAVEVIEW_AUDIO_H
