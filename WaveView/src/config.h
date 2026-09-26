
// Created by torq on 6/20/26.
//configuration file

#ifndef WAVEVIEW_CONFIG_H
#define WAVEVIEW_CONFIG_H

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 256

/* Stereo - set to 4 for future 4-mic array */
#define NUM_CHANNELS 2

/* ring buffer holds N seconds for the interleaved audio */
#define RING_BUFFER_SECONDS 2

/* total number of floats (frames # channels) */
#define RING_BUFFER_SIZE (SAMPLE_RATE * RING_BUFFER_SECONDS * NUM_CHANNELS)

#define FFT_SIZE 1024
#define PLOT_POINTS 512

#endif //WAVEVIEW_CONFIG_H
