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

static PaStream *stream = NULL; //portaudio stream handle
static RingBuffer *global_rb = NULL; //ring buffer passed from audio_init
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

    //get default input device
    device_index = Pa_GetDefaultInputDevice();
    if (device_index == paNoDevice) {
        fprintf(stderr, "audio_init: No default input device found.\n");
        Pa_Terminate();
        return -1;
    }

    //get device info for name lookup
    device_info = Pa_GetDeviceInfo(device_index);
    if (device_info != NULL) {
        strncpy(device_name, device_info->name, sizeof(device_name) - 1);
        device_name[sizeof(device_name) - 1] = '\0';
    }

    printf("audio: Using input device: %s\n", device_name);

    //configure input stream params
    PaStreamParameters input_params;
    input_params.device = device_index;
    input_params.channelCount = 1; //Mono for Phase 1
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = device_info->defaultLowInputLatency;
    input_params.hostApiSpecificStreamInfo = NULL;

    //open the stream for input only
    err = Pa_OpenStream(
        &stream,
        &input_params,
        NULL,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff, //don't clip
        audio_callback,     //callback function
        rb                  //userData passed to callback
        );

    if (err != paNoError) {
        fprintf(stderr, "audio_init: Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        stream = NULL;
        return -1;
    }

    printf("audio: Stream opened successfully. Rate=%d, FrameBuf=%d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);

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

    printf("audio: Stream started.\n");
    return 0;
}

int audio_stop(void) {
    PaError err;

    if (stream == NULL) {
        fprintf(stderr, "audio_stop: stream not initialized. Use audio_init() first.\n");
        return -1;
    }

    err = Pa_StopStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "audio_stop: Pa_StopStream failed: %s\n", Pa_GetErrorText(err));
        return -1;
    }

    printf("audio: Stream stopped.\n");
    return 0;
}

const char * audio_get_device_name(void) {
    return device_name;
}

void audio_terminate(void) {
    if (stream != NULL) {
        //stop if running
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = NULL;
        printf("audio: Stream closed.\n");
    }

    Pa_Terminate();
    global_rb = NULL;
    printf("audio: PortAudio terminated.\n");
}

