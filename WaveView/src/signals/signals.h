//
// Created by torq on 7/11/26.
//

#ifndef WAVEVIEW_SIGNALS_H
#define WAVEVIEW_SIGNALS_H

#include <stdint.h>

/**
 * signal types enumeration
 */
typedef enum {
    SIGNAL_SINE = 0,
    SIGNAL_LINEAR_SWEEP,
    //SIGNAL_LOG_SWEEP,
    //SIGNAL_WHITE_NOISE,
    //SIGNAL_PINK_NOISE,
    //SIGNAL_BROWNIAN_NOISE
} SignalType;

/**
 * signal params, matches audio.h SignalParams. keep a separate copy here for clarity.
 */
typedef struct {
    SignalType type;          //Which signal to generate
    float frequency;          //For sine, or start freq for sweep
    float frequency_end;      //For sweep only
    float amplitude;          //Output volume (0.0 to 1.0)
    float sweep_duration;     //Duration in seconds (for sweep)
    int is_active;            // 1=output signal, 0=silence
    uint32_t sample_rate;     // Sample rate (from config)
} SignalParams;

/**
 * Initialize a signal generator state.
 * Called once when parameters change.
 * @param params  Signal parameters
 */
void signal_init(const SignalParams* params );

/**
 * Generate the next sample.
 * This is called from the audio callback for each output sample.
 * @param params  Signal parameters (read-only)
 * @param sample_index  Absolute sample index (for phase calculation)
 * @return  Next sample value (float in range [-amplitude, amplitude])
 */
float signal_generate_sample(const SignalParams* params, uint64_t sample_index);

/**
 * Reset all signal generator states.
 * Useful when parameters change mid-stream.
 */
void signal_reset(void);

/**
 * Get the current sample index for a given signal type.
 * Used internally for phase tracking.
 */
uint64_t signal_get_sample_index(void);

#endif //WAVEVIEW_SIGNALS_H
