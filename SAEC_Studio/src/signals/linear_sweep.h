//
// Created by torq on 7/11/26.
//

#ifndef SAEC_STUDIO_LINEAR_SWEEP_H
#define SAEC_STUDIO_LINEAR_SWEEP_H

#include "signals.h"

void linear_sweep_init(const SignalParams *params);
float linear_sweep_generate_sample(uint64_t sample_index);
void linear_sweep_reset(void);

#endif //SAEC_STUDIO_LINEAR_SWEEP_H
