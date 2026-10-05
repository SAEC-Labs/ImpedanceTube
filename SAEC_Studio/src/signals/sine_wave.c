//
// Created by torq on 7/9/26.
//

/**
 * optimized sine wave generator
 * uses a phase accumulator with precomputed step to avoid sinf() per sample
 * This is fast enough for the real time audio callback
 */
#include "sine_wave.h"
#include "signals/signals.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

//static state for sine generator
static struct {
    float phase; //current phase in radians
    float phase_step; //phase increment per sample
    float amplitude;
    float frequency;
    uint32_t sample_rate;
    int initialized; //1 if ready
} sine_state = {0};

#define TWO_PI 6.283185307179586f

/**
 * init sine wave generator
 * called when params change or stream starts
 */
void sine_init(const SignalParams *params) {
    if (params == NULL || !params->is_active) {
        sine_state.initialized = 0;
        return;
    }

    sine_state.frequency = params->frequency;
    sine_state.amplitude = params->amplitude;
    sine_state.sample_rate = params->sample_rate;

    //precompute phase step, only once per freq change
    sine_state.phase_step = TWO_PI * sine_state.frequency / sine_state.sample_rate;

    //start at phase 0
    sine_state.phase = 0;

    sine_state.initialized = 1;
}

float sine_wave_generate_sample(uint64_t sample_index) {
    (void) sample_index; //not needed with phase accumulator

    if (!sine_state.initialized) {
        return 0.0f;
    }

    //generate sample from current phase
    float sample = sine_state.amplitude * sinf(sine_state.phase);

    //phase for next sample
    sine_state.phase += sine_state.phase_step;


    //wrap phase to keep it in range [0, TWO_PI], prevents floating-point drift over long runs
    if (sine_state.phase >= TWO_PI) {
        sine_state.phase -= TWO_PI;
    }

    //handle -ve phase
    if (sine_state.phase < 0.0f) {
        sine_state.phase += TWO_PI;
    }

    return sample;
}

void sine_reset(void) {
    sine_state.phase = 0.0f;
    sine_state.initialized = 0;
}
























