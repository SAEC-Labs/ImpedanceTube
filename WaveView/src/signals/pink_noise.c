//
// Created by torq on 9/26/26.
//

/**
 * Pink noise (1/f) noise generator
 *
 * Uses the Paul Kellet filter bank: 7 first-order IIR sections in
 * parallel whose combined response approximates -3 dB/octave (±0.05 dB
 * from 20 Hz to 20 kHz)
 *
 * Input:  zero-mean white noise
 * Output: pink noise with near-zero DC and bounded RMS
 *
 * Normalization: multiplied by ~0.11 to keep output roughly in
 * [-amplitude, +amplitude] for typical white noise input.
 *
 * REFERENCE if your're curious: https://www.firstpr.com.au/dsp/pink-noise/  : Paul Kellet method
 */

#include "pink_noise.h"

#include <stddef.h>

#include "signals.h"
#include <stdint.h>

static struct {
    uint64_t rng_state;
    float b0, b1, b2, b3, b4, b5, b6; //Kellet filter state
    float amplitude;
    int initialized;
} pn_state = {0};

#define PINK_NORM 0.11f //normalization constant from Kellet original notes

void pink_noise_init(const SignalParams *params) {
    if (params == NULL || !params->is_active) {
        pn_state.initialized = 0;
        return;
    }

    //fresh seed for each activation
    pn_state.rng_state = 0x2545F4914F6CDD1DULL;

    //reset Kellet filter state
    pn_state.b0 = pn_state.b1 = pn_state.b2 = 0.0f;
    pn_state.b3 = pn_state.b4 = pn_state.b5 = pn_state.b6 = 0.0f;

    pn_state.amplitude = params->amplitude;
    pn_state.initialized = 1;
}

/**
 *Getting into it using Kellet's refined method
 *
   b0 = 0.99886 * b0 + white * 0.0555179;
   b1 = 0.99332 * b1 + white * 0.0750759;
   b2 = 0.96900 * b2 + white * 0.1538520;
   b3 = 0.86650 * b3 + white * 0.3104856;
   b4 = 0.55000 * b4 + white * 0.5329522;
   b5 = -0.7616 * b5 - white * 0.0168980;
   pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
   b6 = white * 0.115926;
 */
float pink_noise_generate_sample(uint64_t sample_index) {
    (void) sample_index;
    if (!pn_state.initialized) return 0.0f;

    const float white = uniform_signed(&pn_state.rng_state);

    pn_state.b0 = 0.99886f * pn_state.b0 + white * 0.0555179f;
    pn_state.b1 = 0.99332f * pn_state.b1 + white * 0.0750759f;
    pn_state.b2 = 0.96900f * pn_state.b2 + white * 0.1538520f;
    pn_state.b3 = 0.86650f * pn_state.b3 + white * 0.3104856f;
    pn_state.b4 = 0.55000f * pn_state.b4 + white * 0.5329522f;
    pn_state.b5 = -0.7616f * pn_state.b5 - white * 0.0168980f;

    const float pink =  pn_state.b0 + pn_state.b1 + pn_state.b2 +
                 pn_state.b3 + pn_state.b4 + pn_state.b5 +
                 pn_state.b6 + white * 0.5362f;

    pn_state.b6 = white * 0.115926f;

    return pn_state.amplitude * PINK_NORM * pink;
}

void pink_noise_reset(void) {
    pn_state.rng_state = 0;
    pn_state.b0 = pn_state.b1 = pn_state.b2 = 0.0f;
    pn_state.b3 = pn_state.b4 = pn_state.b5 = pn_state.b6 = 0.0f;
    pn_state.amplitude = 0.0f;
    pn_state.initialized = 0;
}