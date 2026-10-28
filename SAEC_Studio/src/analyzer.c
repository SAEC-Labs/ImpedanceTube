//
// Created by torq on 10/13/26.
//

/**
 * H12 & Coherence computation
 *
 * Guard against division by zero (denominator < EPS -> output = 0)
 * Magnitudes in dB is clamped to a floor of -120dB to avoid -inf (^_^)
 */

#include "analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI2
#define M_PI2 3.14159265358979323846
#endif

#define EPS 1e-20f
#define DB_FLOOR -120.0f

typedef struct {
    int num_bins;
    float *h12_real;
    float *h12_imag;
    float *h12_mag_db;
    float *h12_phase_rad;
    float *coherence;
    int initialized;
} AnalysisContext;

static AnalysisContext a_ctx = {0};

int analysis_init(const int num_bins) {
    if (a_ctx.initialized) return 0;
    if (num_bins <= 0) return -1;

    a_ctx.num_bins = num_bins;
    a_ctx.h12_real = (float*)calloc(num_bins, sizeof(float));
    a_ctx.h12_imag = (float*)calloc(num_bins, sizeof(float));
    a_ctx.h12_mag_db = (float*)calloc(num_bins, sizeof(float));
    a_ctx.h12_phase_rad = (float*)calloc(num_bins, sizeof(float));
    a_ctx.coherence = (float*)calloc(num_bins, sizeof(float));

    if (!a_ctx.h12_real || !a_ctx.h12_imag || !a_ctx.h12_mag_db || !a_ctx.h12_phase_rad || !a_ctx.coherence) {
        analysis_free();
        return -1;
    }

    a_ctx.initialized = 1;
    return 0;
}

void analysis_free(void) {
    free(a_ctx.h12_real);
    free(a_ctx.h12_imag);
    free(a_ctx.h12_mag_db);
    free(a_ctx.h12_phase_rad);
    free(a_ctx.coherence);
    memset(&a_ctx, 0, sizeof(a_ctx));
}

int analysis_compute(const float *G11, const float *G22, const float *G12_real, const float *G12_imag) {
    if (!a_ctx.initialized) return -1;
    if (!G11 || !G22 || !G12_real || !G12_imag) return -1;

    for (int i = 0; i < a_ctx.num_bins; i++) {
        const float g11 = G11[i];
        const float g22 = G22[i];
        const float s12r = G12_real[i];
        const float s12i = G12_imag[i];

        //we all know H12 = S12 / S11
        if (g11 > EPS) {
            const float inv_g11 = 1.0f / g11;
            a_ctx.h12_real[i] = s12r * inv_g11;
            a_ctx.h12_imag[i] = s12i * inv_g11;
        } else {
            a_ctx.h12_real[i] = 0.0f;
            a_ctx.h12_imag[i] = 0.0f;
        }

        //H12 in dB
        const float mag = sqrtf(a_ctx.h12_real[i] * a_ctx.h12_real[i] + a_ctx.h12_imag[i] * a_ctx.h12_imag[i]);
        const float mag_db = (mag > 1e-8f) ? 20.0f * log10f(mag) : DB_FLOOR;
        a_ctx.h12_mag_db[i] = mag_db;

        //phase
        a_ctx.h12_phase_rad[i] = atan2f(a_ctx.h12_imag[i], a_ctx.h12_real[i]);

        //coherence Y^2 = |S12|^2 / (S11.S22)
        const float denom = g11 * g22;
        if (denom > EPS) {
            float coh = (s12r * s12r + s12i * s12i) /  denom;
            if (coh > 1.0f) coh = 1.0f;
            if (coh < 0.0f) coh = 0.0f;
            a_ctx.coherence[i] = coh;
        } else {
            a_ctx.coherence[i] = 0.0f;
        }
    }
    return 0;
}

const float *analysis_get_h12_mag_db(void) { return a_ctx.h12_mag_db; }
const float *analysis_get_h12_phase_rad(void) { return a_ctx.h12_phase_rad; }
const float *analysis_get_h12_real(void) { return a_ctx.h12_real; }
const float *analysis_get_h12_imag(void) { return a_ctx.h12_imag; }
const float *analysis_get_coherence(void) { return a_ctx.coherence; }
