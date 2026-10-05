//
// Created by torq on 9/23/26.
//

/*
 * freq grows geometrically:
 *      f(t) = f1 * (f2 / f1) ^ (t/T)
 * usees mutiplifiered phase step, only one multipy and one addition per sample
 */

#include "log_sweep.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#define TWO_PI  6.283185307179586f

static struct {
    float phase;
    float phase_step;
    float ratio;
    float amplitude;
    float start_freq;
    float end_freq;
    float duration;
    uint32_t sample_rate;
    uint64_t start_sample;
    int initialized;
    int started;
    int sweep_active;
} sweep_state = {0};

void logsweep_init(const SignalParams *params) {
    //DEBUG PRINT 1, show incoming params
    if (params != NULL) {
        fprintf(stderr, "[LOG_INIT] ENTER: f1=%.2f f2=%.2f dur=%.2f fs=%u amp=%.2f active=%d type=%d\n",
                params->frequency, params->frequency_end,
                params->sweep_duration, params->sample_rate,
                params->amplitude, params->is_active, params->type);
    } else {
        fprintf(stderr, "[LOG_INIT] params is NULL!\n");
        return;
    }

    if (params == NULL || !params->is_active) {
        fprintf(stderr, "[LOG_INIT] EXIT early: is_active=0\n");
        sweep_state.initialized = 0;
        return;
    }

    float f1  = params->frequency;
    float f2  = params->frequency_end;
    float dur = params->sweep_duration;
    const uint32_t fs = params->sample_rate;

    if (f1  <= 0.0f) f1  = 20.0f;
    if (f2  <= 0.0f) f2  = 20000.0f;
    if (dur <= 0.0f) dur = 1.0f;

    sweep_state.start_freq  = f1;
    sweep_state.end_freq    = f2;
    sweep_state.duration    = dur;
    sweep_state.amplitude   = params->amplitude;
    sweep_state.sample_rate = fs;

    sweep_state.phase_step = TWO_PI * f1 / fs;
    sweep_state.ratio      = powf(f2 / f1, 1.0f / (dur * fs));

    //DEBUG PRINT 2, show computed values
    fprintf(stderr, "[LOG_INIT] COMPUTED: phase_step=%.8f ratio=%.10f\n",
            sweep_state.phase_step, sweep_state.ratio);

    sweep_state.phase        = 0.0f;
    sweep_state.start_sample = 0;
    sweep_state.started      = 0;   /* from the started-flag fix */
    sweep_state.sweep_active = 1;
    sweep_state.initialized  = 1;

    fprintf(stderr, "[LOG_INIT] READY: initialized=1\n");
}

float logsweep_generate_sample(uint64_t sample_index) {
    if (!sweep_state.initialized) return 0.0f;

    if (!sweep_state.started) {
        sweep_state.start_sample = sample_index;
        sweep_state.started = 1;
    }

    const uint64_t elapsed = sample_index - sweep_state.start_sample;
    const float elapsed_time = elapsed / sweep_state.sample_rate;

    if (elapsed_time >= sweep_state.duration) {
        sweep_state.sweep_active = 0;
        return 0.0f;
    }

    const float sample = sweep_state.amplitude * sinf(sweep_state.phase);

    // DEBUG PRINT 3, first 10 calls
    static int dbg = 0;
    if (dbg < 10) {
        fprintf(stderr, "[LOG_GEN] idx=%llu phase=%.6f step=%.8f ratio=%.8f out=%.6f\n",
                (unsigned long long)sample_index,
                sweep_state.phase,
                sweep_state.phase_step,
                sweep_state.ratio,
                sample);
        dbg++;
    }

    sweep_state.phase += sweep_state.phase_step;
    sweep_state.phase_step *= sweep_state.ratio;

    if (sweep_state.phase >= TWO_PI) sweep_state.phase -= TWO_PI;
    if (sweep_state.phase <  0.0f)   sweep_state.phase += TWO_PI;

    return sample;
}

int logsweep_is_active(void) {
    return sweep_state.initialized && sweep_state.sweep_active;
}

void logarithmic_sweep_reset(void) {
    sweep_state.phase = 0.0f;
    sweep_state.phase_step = 0.0f;
    sweep_state.ratio = 1.0f;
    sweep_state.start_sample = 0;
    sweep_state.sweep_active = 0;
    sweep_state.initialized = 0;
    sweep_state.started = 1;
}
