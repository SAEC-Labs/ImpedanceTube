//
// Created by torq on 9/23/26.
//

#ifndef WAVEVIEW_LOG_SWEEP_H
#define WAVEVIEW_LOG_SWEEP_H

#include "signals.h"

/**
 * Initialize logarithmic sweep generator.
 * Sweeps from params->frequency to params->frequency_end over
 * params->sweep_duration seconds. Frequency grows geometrically.
 */
void logsweep_init(const SignalParams *params);

/**
 * Generate next sample. Returns 0.0f when duration is exceeded.
 */
float logsweep_generate_sample(uint64_t sample_index);


/**
 * Check if sweep is still running.
 */
int logsweep_is_active(void);

/**
 * Reset state.
 */
void logsweep_reset(void);

#endif //WAVEVIEW_LOG_SWEEP_H
