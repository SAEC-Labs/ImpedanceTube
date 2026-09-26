//
// Created by torq on 9/26/26.
//

#ifndef WAVEVIEW_WHITE_NOISE_H_H
#define WAVEVIEW_WHITE_NOISE_H_H

#include "signals.h"


/**
 * Initialize white noise generator.
 * Seed is fixed so output is reproducible...?
 */
void white_noise_init(const SignalParams *params);

/**
 * Generate next sample. Uniform in [-amplitude, +amplitude].
 * sample_index is unused (white noise is stationary).
 */
float white_noise_generate_sample(uint64_t sample_index);

/**
 * Reset state and free nothing (no dynamic memory).
 */
void white_noise_reset(void);


#endif //WAVEVIEW_WHITE_NOISE_H_H
