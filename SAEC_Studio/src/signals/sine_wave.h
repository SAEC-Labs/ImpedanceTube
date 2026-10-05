//
// Created by torq on 7/9/26.
//

#ifndef SAEC_STUDIO_SINE_WAVE_H
#define SAEC_STUDIO_SINE_WAVE_H

#include "signals.h"

void sine_init(const SignalParams *params);
float sine_wave_generate_sample(uint64_t sample_index);
void sine_reset(void);

#endif //SAEC_STUDIO_SINE_WAVE_H
