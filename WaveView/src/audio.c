//
// Created by torq on 6/20/26.
//

#include "audio.h"
#include "config.h"
#include <portaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h> //for usleep

//signal generator headers
#include "signals/sine_wave.h"
#include "signals/linear_sweep.h"



//static ( private to this file )
#define MAX_DEVICES 32
static PaStream *stream = NULL; //portaudio stream handle
static RingBuffer *global_rb = NULL; //ring buffer passed from audio_init
static AudioDeviceInfo device_list[MAX_DEVICES];
static int device_count = 0;
static int current_device_index = -1; //index into device_list
static int is_running = 0;
static int current_input_channels = NUM_CHANNELS;

/* signal params and protection mutex */
static SignalParams current_params = {
    .type = SIGNAL_SINE,
    .frequency = 432.0f,
    .frequency_end = 1000.0f,
    .amplitude = 0.5f,
    .sweep_duration = 5.0f,
    .is_active = 0, //change to 1 for testing
    .sample_rate = SAMPLE_RATE
};

static pthread_mutex_t params_mutex = PTHREAD_MUTEX_INITIALIZER;

//check if device name contains "pulse" (for linux)
static int is_pulse_device(const char *name) {
    if (name == NULL) return 0;
    return (strstr(name, "pulse") != NULL || strstr(name, "pulse") != NULL);
}

/* signal generator */
static float generate_signal_sample(const SignalParams *params, uint64_t sample_index) {
    if (!params->is_active) {
        return 0.0f;
    }

    switch (params->type) {
        case SIGNAL_SINE:
            return sine_wave_generate_sample(sample_index);
        case SIGNAL_LINEAR_SWEEP:
            return linear_sweep_generate_sample(sample_index);
        default:
            return 0.0f; //other types not yet implemented
    }
}

/**
* PortAudio callback function. Audio callback runs in high-priority portaudio thread. FULL-DUPLEX
*
* Called automatically by PortAudio when audio data is available.
*
* @param input       Pointer to input buffer (microphone samples)
* @param output      Pointer to output buffer (used – we only capture and output too)
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
    //(void) output;
    (void) timeInfo;
    (void) statusFlags;

    //cast data passed through stream to our struct
    RingBuffer *rb = userData;

    float *out = output;

    /* 1. Write interleaved mic samples to ring buffer */
    if (input != NULL) {
        const int samples = (int)(frameCount * current_input_channels);
        ring_buffer_write(rb, input, samples);
    }

    /* 2. generate output */
    if (out != NULL) {
        //copy current signal params under mutex - quick lock

        SignalParams local_params;
        pthread_mutex_lock(&params_mutex);
        local_params = current_params;
        pthread_mutex_unlock(&params_mutex);

        //get current sample index for sweeps timing
        static uint64_t sample_counter = 0;
        uint64_t sample_index = sample_counter;
        sample_counter += frameCount;

        if (local_params.is_active) {
            //generate samples using the signal generator
            for (unsigned long i = 0; i < frameCount; i++) {
                out[i] = generate_signal_sample(&local_params, sample_index + i);
            }
        } else {
            //output silence!
            for (unsigned long i = 0; i < frameCount; i++) {
                out[i] = 0.0f;
            }
        }
    }
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

        //skip PulseAudio devices as may cause instability (on linux)
        if (is_pulse_device(info->name)) {
            continue;
        }

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
 * Attempt to open the full-duplex stream with given device/channel configuration.
 * @return 0 on success, -1 on failure (stream set to NULL).
 */
static int try_open_stream(int in_dev, int out_dev, int in_channels) {
    PaStreamParameters in_params, out_params;
    const PaDeviceInfo *in_info = Pa_GetDeviceInfo(in_dev);
    const PaDeviceInfo *out_info = Pa_GetDeviceInfo(out_dev);

    if (!in_info || !out_info) return -1;

    in_params.device = in_dev;
    in_params.channelCount = in_channels;
    in_params.sampleFormat = paFloat32;
    in_params.suggestedLatency = in_info->defaultLowInputLatency;
    in_params.hostApiSpecificStreamInfo = NULL;

    out_params.device = out_dev;
    out_params.channelCount = 1;     //Mono speaker
    out_params.sampleFormat = paFloat32;
    out_params.suggestedLatency = out_info->defaultLowOutputLatency;
    out_params.hostApiSpecificStreamInfo = NULL;

    PaError err = Pa_OpenStream(&stream,
        &in_params,
        &out_params,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        audio_callback,
        global_rb
        );
    if (err == paNoError) {
        current_input_channels = in_channels;
        return 0;
    }

    stream = NULL;
    return -1;
}


/**
 *Stream opening, FULL-DUPLEX
 *function to open the stream with the current device.
 */
static int open_stream(void)
{
    if (current_device_index < 0 || current_device_index >= device_count) {
        fprintf(stderr, "audio: No valid input device selected.\n");
        return -1;
    }

    int in_dev = device_list[current_device_index].index;
    const PaDeviceInfo *in_info = Pa_GetDeviceInfo(in_dev);
    if (in_info == NULL) {
        fprintf(stderr, "audio: Failed to get input device info.\n");
        return -1;
    }

    PaDeviceIndex out_dev = Pa_GetDefaultOutputDevice();
    if (out_dev == paNoDevice) {
        fprintf(stderr, "audio: No default output device found.\n");
        return -1;
    }

    /* Determine safe channel count (device may not support full stereo) */
    int safe_channels = NUM_CHANNELS;
    if (in_info->maxInputChannels < safe_channels) {
        printf("audio: Device '%s' supports only %d input channel(s).\n",
               in_info->name, in_info->maxInputChannels);
        safe_channels = in_info->maxInputChannels;
    }

    /* Attempt 1: open with safe channel count */
    if (try_open_stream(in_dev, out_dev, safe_channels) == 0) {
        printf("audio: Full-duplex stream opened: input=%s (%d ch), output=default (1 ch)\n",
               in_info->name, current_input_channels);
        return 0;
    }

    /* Attempt 2: if stereo failed, retry with mono */
    if (safe_channels > 1) {
        printf("audio: Multi-channel open failed. Retrying with mono...\n");
        if (try_open_stream(in_dev, out_dev, 1) == 0) {
            printf("audio: Full-duplex stream opened: input=%s (1 ch), output=default (1 ch)\n",
                   in_info->name);
            return 0;
        }
    }

    fprintf(stderr, "audio: Failed to open stream with device '%s'.\n", in_info->name);
    return -1;
}

int audio_init(RingBuffer *rb) {
    PaError err;

    //validate input
    if (rb == NULL) {
        fprintf(stderr, "audio_init: ring buffer is NULL\n");
        return -1;
    }

    //store ring buffer for use in start/stop if needed
    global_rb = rb;

    //initialize portaudio
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

    printf("audio_init: Initialized with input device: %s\n", device_list[current_device_index].name);
    return 0;
}

int audio_update_signal_params(const SignalParams *params) {
    if (params == NULL) return -1;

    pthread_mutex_lock(&params_mutex);

    //copy params
    current_params = *params;
    current_params.sample_rate = SAMPLE_RATE;

    //init the selected signal generator
    if (current_params.is_active) {
        switch (current_params.type) {
            case SIGNAL_SINE:
                sine_init(&current_params);
                break;
            case SIGNAL_LINEAR_SWEEP:
                linear_sweep_init(&current_params);
                break;
            default:
                //other types: do nothing yet
                break;
        }
    }

    pthread_mutex_unlock(&params_mutex);
    return 0;
}

int audio_get_signal_params(SignalParams *params) {
    if (params == NULL) return -1;

    pthread_mutex_lock(&params_mutex);
    *params = current_params;
    pthread_mutex_unlock(&params_mutex);

    return 0;
}

int audio_get_device_count(void) {
    return device_count;
}

int audio_get_input_channels(void) {
    return current_input_channels;
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

    //always stop the stream first
    if (is_running) {
        audio_stop();
    }

    //close old stream
    if (stream) {
        Pa_CloseStream(stream);
        stream = NULL;

        usleep(100000); //100ms delay to let ALSA (linux) or ASIO (windows) release the device
    }

    //update current device
    current_device_index = new_list_index;

    //reopen stream
    if (open_stream() != 0) {
        fprintf(stderr, "audio_select_device: Failed to open stream with new device.\n");
        return -1;
    }

    //restart only  if it was running before
    if (is_running) {
        if (audio_start() != 0) {
            fprintf(stderr, "audio_select_device: Failed to restart stream.\n");
            return -1;
        }
    }

    return 0;
}

int audio_start(void) {
    if (stream == NULL) {
        fprintf(stderr, "audio_start: stream not initialized. Call audio_init() first.\n");
        return -1;
    }

    PaError err = Pa_StartStream(stream);
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

