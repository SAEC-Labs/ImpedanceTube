//
// Created by torq on 7/11/26.
//

/**
 * Optimized linear frequency sweep generator
 * Uses phase accumulator with linearly increasing phase step.
 * This produces a clean sweep with continuous phase.
 *
 * Performance: ~3 additions + 1 sinf() per sample.
 */

#include "linear_sweep.h"
#include <math.h>
#include  <stddef.h>


static struct {
    float phase;              //Current phase (radians)
    float phase_step;         //Current phase increment per sample (radians/sample)
    float phase_step_increment; //How much phase_step increases per sample
    float amplitude;          //Output amplitude
    float start_freq;         //Start frequency (for reference)
    float end_freq;           //End frequency (for reference)
    float duration;           //Sweep duration (seconds)
    uint32_t sample_rate;     //Sample rate (Hz)
    uint64_t start_sample;    //Sample index when sweep started
    int initialized;          //1 if ready to generate
    int sweep_active;         //1 if sweep is still running
} sweep_state = {0};


#define TWO_PI 6.283185307179586f

void linear_sweep_init(const SignalParams *params)
{
    if (params == NULL || !params->is_active) {
        sweep_state.initialized = 0;
        return;
    }

    float start_freq = params->frequency;
    float end_freq = params->frequency_end;
    float duration = params->sweep_duration;
    float amp = params->amplitude;
    uint32_t fs = params->sample_rate;

    sweep_state.start_freq = start_freq;
    sweep_state.end_freq = end_freq;
    sweep_state.duration = duration;
    sweep_state.amplitude = amp;
    sweep_state.sample_rate = fs;

    //Calculate the frequency change per sample
    float freq_step_per_sample = (end_freq - start_freq) / (duration * fs);

    //Initial phase step (at start frequency)
    sweep_state.phase_step = TWO_PI * start_freq / fs;

    //How much the phase step changes per sample
    //d(phase_step)/dt = 2π * (f_end - f_start) / (T * Fs)
    //Per sample: divide by Fs again
    sweep_state.phase_step_increment = TWO_PI * freq_step_per_sample / fs;

    //Reset phase to 0
    sweep_state.phase = 0.0f;

    //Reset start sample
    sweep_state.start_sample = 0;

    //Sweep is active initially
    sweep_state.sweep_active = 1;
    sweep_state.initialized = 1;
}

float linear_sweep_generate_sample(uint64_t sample_index)
{
    if (!sweep_state.initialized) {
        return 0.0f;
    }

    //Store start sample on first call
    if (sweep_state.start_sample == 0) {
        sweep_state.start_sample = sample_index;
    }

    //Calculate elapsed samples
    uint64_t elapsed_samples = sample_index - sweep_state.start_sample;
    float elapsed_time = elapsed_samples / sweep_state.sample_rate;

    //Stop sweep if duration exceeded
    if (elapsed_time >= sweep_state.duration) {
        sweep_state.sweep_active = 0;
        return 0.0f;
    }

    //Generate sample from current phase
    float sample = sweep_state.amplitude * sinf(sweep_state.phase);

    //Advance phase for next sample
    sweep_state.phase += sweep_state.phase_step;

    //Increase phase step linearly (this makes the frequency sweep)
    sweep_state.phase_step += sweep_state.phase_step_increment;

    //Wrap phase to keep it in range
    if (sweep_state.phase >= TWO_PI) {
        sweep_state.phase -= TWO_PI;
    }
    if (sweep_state.phase < 0.0f) {
        sweep_state.phase += TWO_PI;
    }

    return sample;
}

int linear_sweep_is_active(void)
{
    return sweep_state.initialized && sweep_state.sweep_active;
}

//reset all
void linear_sweep_reset(void)
{
    sweep_state.phase = 0.0f;
    sweep_state.phase_step = 0.0f;
    sweep_state.phase_step_increment = 0.0f;
    sweep_state.start_sample = 0;
    sweep_state.sweep_active = 0;
    sweep_state.initialized = 0;
}
