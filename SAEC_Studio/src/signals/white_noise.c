//
// Created by torq on 9/26/26.
//

/**
 * White noise generator
 *
 * Flat Power Spectral Density (PSD). uncorrelated samples from a zero mean
 *
 * PRNG is xorshift64: fast, statistically strong, 8-byte state!
 */

#include <stddef.h>
#include <stdint.h>

#include "white_noise.h"



static  struct {
    uint64_t state; //PRNG state
    float amplitude; //peak amp
    int initialized;

} wn_state = {0};



void white_noise_init(const SignalParams *params) {
    if (params == NULL || !params->is_active) {
        wn_state.initialized = 0;
        return;
    }

    //fixed seed, reproducible runs. change to get different noise
    wn_state.state = 0x9E3779B97F4A7C15ULL;
    wn_state.amplitude = params->amplitude;
    wn_state.initialized = 1;
}

float white_noise_generate_sample(uint64_t sample_index) {
    (void) sample_index; //stationary, inex not used

    if (!wn_state.initialized) return 0.0f;
    return wn_state.amplitude * uniform_signed(&wn_state.state);
}

void white_noise_reset(void) {
    wn_state.state = 0;
    wn_state.amplitude = 0.0f;
    wn_state.initialized = 0;
}
















