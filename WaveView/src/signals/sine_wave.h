//
// Created by torq on 7/9/26.
//

#ifndef WAVEVIEW_SINE_WAVE_H
#define WAVEVIEW_SINE_WAVE_H

#include "signals.h"

void sine_init(const SignalParams *params);
float sine_generate_sample(uint64_t sample_index);
void sine_reset(void);

#endif //WAVEVIEW_SINE_WAVE_H
