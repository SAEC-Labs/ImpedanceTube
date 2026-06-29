//
// Created by torq on 6/20/26.
//

#include "audio.h"
#include "config.h"
#include <portaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//static ( private to this file )

#define MAX_DEVICES 32

static PaStream *stream = NULL; //portaudio stream handle
static RingBuffer *global_rb = NULL; //ring buffer passed from audio_init
static AudioDeviceInfo device_list[MAX_DEVICES];
static int device_count = 0;
static int current_device_index = -1; //index into device_list
static int is_running = 0;
static char device_name[256] = "Unknown";

/**
* PortAudio callback function. Audio callback runs in high-priority portaudio thread
*
* Called automatically by PortAudio when audio data is available.
*
* @param input       Pointer to input buffer (microphone samples)
* @param output      Pointer to output buffer (not used – we only capture)
* @param frameCount  Number of frames in this callback
* @param timeInfo    Timing info (not used)
* @param statusFlags PortAudio status flags (not used)
* @param userData    Pointer to our ring buffer (passed during stream open)
*
* @return paContinue to keep the stream running
*/
static int audio_callback(const void *input, void *output, unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {

    //ignore unused params
    (void) output;
    (void) timeInfo;
    (void) statusFlags;

    //cast data passed through stream to our struct
    RingBuffer *rb = (RingBuffer *) userData;

    //if input is NULL (maybe some audio device issue) do nothing
    if (input == NULL) {
        return paContinue;
    }

    //cast to float. we use paFloat32 type
    const float *samples = (const float *) input;

    //write samples to ring buffer
    ring_buffer_write(rb, samples, (int) frameCount);

    return paContinue;

}

/**
 * Enumerate all input devices and fill device_list.
 * Returns number of devices found.
 */
static int enumerate_devices(void) {
    int count = Pa_GetDeviceCount();
    if (count < 0) return 0;

    device_count = 0;

    for (int i = 0; i < count && device_count < MAX_DEVICES; i++) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (info == NULL) continue;
        if (info->maxInputChannels <= 0) continue; //input only

        device_list[device_count].index = i;
        strncpy(device_list[device_count].name, info->name, sizeof(device_list[device_count].name) - 1);
        device_list[device_count].name[sizeof(device_list[device_count].name) -1] = '\0';
        device_list[device_count].maxInputChannels = info->maxInputChannels;
        device_count++;
    }
    return device_count;
}

/**
 * Find the default input device index in our device_list.
 * Returns -1 if not found.
 */
static int find_default_device_index(void) {
    PaDeviceIndex default_idx = Pa_GetDefaultInputDevice();
    if (default_idx == paNoDevice) {
        return -1;
    }

    for (int i = 0; i < device_count; i++) {
        if (device_list[i].index == default_idx) {
            return i;
        }
    }
    return -1;
}

/**
 *function to open the stream with the current device.
 */
static int open_stream(void) {
    PaError err;
    PaStreamParameters inputParams;

    if (current_device_index < 0 || current_device_index >=device_count) {
        fprintf(stderr, "audio stream: Invalid device index.\n");
        return -1;
    }

    int pa_device = device_list[current_device_index].index;
    const PaDeviceInfo *info = Pa_GetDeviceInfo(pa_device);
    if (info == NULL) {
        fprintf(stderr, "audio: Failed to get device info for index %d.\n", pa_device);
        return -1;
    }

    inputParams.device = pa_device;
    inputParams.channelCount = 1; //mono for now
    inputParams.sampleFormat = paFloat32;
    inputParams.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
        &stream,
        &inputParams,
        NULL,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        audio_callback,
        global_rb);

    if (err != paNoError) {
        fprintf(stderr, "audio: Pa_OpenStream error: %s\n", Pa_GetErrorText(err));
        stream = NULL;
        return -1;
    }

    printf("audio: Stream opened on device: %s\n", info->name);
    return 0;
}

int audio_init(RingBuffer *rb) {
    PaError err;
    PaDeviceIndex device_index;
    const PaDeviceInfo *device_info;

    //validate input
    if (rb == NULL) {
        fprintf(stderr, "audio_init: ring buffer is NULL\n");
        return -1;
    }

    //store ring buffer for use in start/stop if needed
    global_rb = rb;

    //ininialize portaudio
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "audio_init: Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        return -1;
    }

    //enumerate devices
    int count = enumerate_devices();
    if (count == 0) {
        fprintf(stderr, "audio_init: No input devices found.\n");
        Pa_Terminate();
        return -1;
    }

    //find default device
    int default_idx = find_default_device_index();
    if (default_idx < 0) {
        //fallback to first device
        default_idx = 0;
    }
    current_device_index = default_idx;

    //open the stream with default device
    if (open_stream() != 0) {
        Pa_Terminate();
        return -1;
    }

    printf("audio_init: Initialized with device: %s\n", device_list[current_device_index].name);
    return 0;
}

int audio_get_device_count(void) {
    return device_count;
}

const AudioDeviceInfo* audio_get_device_info(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &device_list[index];
}

int audio_select_device(int device_index) {
    //find the index in our list
    int new_list_index = -1;
    for (int i = 0; i < device_count; i++) {
        if (device_list[i].index == device_index) {
            new_list_index = i;
            break;
        }
    }
    if (new_list_index < 0) {
        fprintf(stderr, "audio_select_device: Device index %d not found.\n", device_index);
        return -1;
    }

    //if already selected do nothing
    if (new_list_index == current_device_index) {
        return 0;
    }

    //stop stream if running
    int was_running = is_running;
    if (was_running) {
        audio_stop();
    }

    //close old stream
    if (stream) {
        Pa_CloseStream(stream);
        stream == NULL;
    }

    //update current device
    current_device_index == new_list_index;

    //reopen stream
    if (open_stream() != 0) {
        fprintf(stderr, "audio_select_device: Failed to open stream with new device.\n");
        return -1;
    }

    //restart if it was running
    if (was_running) {
        if (audio_start() != 0) {
            fprintf(stderr, "audio_select_device: Failed to restart stream.\n");
            return -1;
        }
    }

    return 0;
}

int audio_start(void) {
    PaError err;

    if (stream == NULL) {
        fprintf(stderr, "audio_start: stream not initialized. Call audio_init() first.\n");
        return -1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "audio_start: Pa_StartStream failed: %s\n", Pa_GetErrorText(err));
        return -1;
    }

    is_running = 1;
    printf("audio: Stream started.\n");
    return 0;
}

int audio_stop(void) {
    if (stream == NULL) {
        fprintf(stderr, "audio_stop: stream not initialized. Use audio_init() first.\n");
        return -1;
    }

    if (!is_running) {
        return 0; //already stopped
    }

    PaError err = Pa_StopStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "audio_stop: Pa_StopStream failed: %s\n", Pa_GetErrorText(err));
        return -1;
    }

    is_running = 0;
    printf("audio: Stream stopped.\n");
    return 0;
}

int audio_is_running(void) {
    return is_running;
}

const char * audio_get_device_name(void) {
    if (current_device_index < 0 || current_device_index >= device_count) {
        return "No device";
    }
    return device_list[current_device_index].name;
}

void audio_terminate(void) {
    if (stream) {
        if (is_running) {
            //stop if running
            Pa_StopStream(stream);
            is_running = 0;
        }
        Pa_CloseStream(stream);
        stream = NULL;
        printf("audio: Stream closed.\n");
    }

    Pa_Terminate();
    global_rb = NULL;
    printf("audio: PortAudio terminated.\n");
}

