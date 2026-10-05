//
// Created by torq on 9/26/26.
//

#ifndef SAEC_STUDIO_PINK_NOISE_H
#define SAEC_STUDIO_PINK_NOISE_H

#include "signals.h"

void pink_noise_init(const SignalParams *params);
float pink_noise_generate_sample(uint64_t sample_index);
void pink_noise_reset(void);


#endif //SAEC_STUDIO_PINK_NOISE_H
