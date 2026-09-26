//
// Created by torq on 9/26/26.
//

#ifndef WAVEVIEW_PINK_NOISE_H
#define WAVEVIEW_PINK_NOISE_H

#include "signals.h"

void pink_noise_init(const SignalParams *params);
float pink_noise_generate_sample(uint64_t sample_index);
void pink_noise_reset(void);


#endif //WAVEVIEW_PINK_NOISE_H
