//
// Created by torq on 7/11/26.
//

#ifndef WAVEVIEW_LINEAR_SWEEP_H
#define WAVEVIEW_LINEAR_SWEEP_H

#include "signals.h"
#include "signals/signals.h"

void linear_sweep_init(const SignalParams *params);
float linear_sweep_generate_sample(uint64_t sample_index);
void linear_sweep_reset(void);

#endif //WAVEVIEW_LINEAR_SWEEP_H
