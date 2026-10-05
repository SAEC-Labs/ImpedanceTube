//
// Created by torq on 6/20/26.
//

/**
 * uses KissFFT for efficient real-optimised FFT.
 * applies Hann window before FFT to reduce spectral leakage.
 * Normalises magnitudes by FFT size for consistent scaling
 *
 * use a persistent FFT context, allocated once and reused across calls
 */

#include "dsp.h"
#include "external/kissfft/kiss_fft.h"
#include "external/kissfft/kiss_fftr.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI2
#define M_PI2 3.14159265358979323846
#endif

//persistent state, alloc once, reused for every FFT

static kiss_fftr_cfg  g_cfg        = NULL;
static float         *g_windowed   = NULL;
static kiss_fft_cpx  *g_freq_data  = NULL;
static float         *g_hann       = NULL;
static int            g_fft_size   = 0;

//ensure the FFT context matches the requested size

static void dsp_ensure_initialized(const int fft_size) {
    if (g_cfg != NULL && g_fft_size == fft_size) {
        return; //aready good
    }
    //free previous state if size changed
    if (g_cfg) {
        kiss_fftr_free(g_cfg);
        g_cfg = NULL;
    }

    free(g_windowed);   g_windowed = NULL;
    free(g_freq_data);  g_freq_data = NULL;
    free(g_hann);       g_hann = NULL;

    //alloc fresh
    g_cfg = kiss_fftr_alloc(fft_size, 0, NULL, NULL);
    g_windowed = (float*) malloc(fft_size * sizeof(float));
    g_freq_data = (kiss_fft_cpx*) malloc((fft_size /2 + 1) * sizeof(kiss_fft_cpx));
    g_hann = (float*) malloc(fft_size * sizeof(float));

    if (!g_cfg || !g_windowed || !g_freq_data || !g_hann) {
        return; //alloc failure, compute spectrum will no op
    }

    //precompute Hann window (once)
    for (int i = 0; i < fft_size; i++) {
        g_hann[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI2 * i / (fft_size - 1)));
    }

    g_fft_size = fft_size;

}



/**
 * Apply Hann window in-place.
 * w[n] = 0.5 * (1 - cos(2π * n / (N - 1)))
 *
 * <NOT INTERNALLY USED ANYMORE>
 */
void apply_hann_window(float *data, const int n) {
    if ( n < 2) return; //avoid division by zero

    for (int i = 0; i < n; i++) {
        const float w = 0.5f * (1.0f - cosf(2.0f * M_PI2 * i / (n - 1)));
        data[i] *= w;
    }
}

void compute_spectrum(const float *time_data, float *magnitude, const int fft_size) {
    if (!time_data || !magnitude || fft_size < 2) return;

    dsp_ensure_initialized(fft_size);

    if (!g_cfg || !g_windowed || !g_freq_data || !g_hann) return;

    //apply precomputed Hann window
    for (int i = 0; i < fft_size; i++) {
        g_windowed[i] = time_data[i] * g_hann[i];
    }

    //Execute FFT
    kiss_fftr(g_cfg, g_windowed, g_freq_data);

    //Compute magnitude spectrum; single sided, normalized
    const float norm = 1.0f / (float) fft_size;
    for (int i = 0; i < fft_size / 2; i++) {
        const float re = g_freq_data[i].r;
        const float im = g_freq_data[i].i;
        magnitude[i] = sqrtf(re * re + im * im) * norm;
    }
}

/**
void compute_spectrum(const float *time_data, float *magnitude, int fft_size) {
    //validate inputs
    if (time_data == NULL || magnitude == NULL || fft_size < 2){
        return;
    }

    //allocate FFT configuration (forward transform)
    kiss_fftr_cfg cfg = kiss_fftr_alloc(fft_size, 0, NULL, NULL);
    if (cfg == NULL) {
        //allocation failed, use a fallback (zero out the magnitudes)
        memset(magnitude, 0, (fft_size/2) * sizeof(float));
        return;
    }

    //copy input data as we need mutable buffer for windowing
    float *windowed_data = (float*) malloc(fft_size * sizeof(float));
    if (!windowed_data) {
        //memory allocation failed
        kiss_fftr_free(cfg);
        memset(magnitude, 0, (fft_size/2) * sizeof(float));
        return;
    }

    memcpy(windowed_data, time_data, fft_size * sizeof(float));

    //apply hann window
    apply_hann_window(windowed_data, fft_size);

    //output buffer for FFT (complex values)
    kiss_fft_cpx * freq_data = (kiss_fft_cpx*) malloc((fft_size / 2 + 1) * sizeof(kiss_fft_cpx));

    if (freq_data == NULL) {
        //memory alloc failed
        free(windowed_data);
        kiss_fftr_free(cfg);
        memset(magnitude, 0, (fft_size / 2) * sizeof(float));
        return;
    }

    //execute the FFT , real to complex
    kiss_fftr(cfg, windowed_data, freq_data);

    //compute magnitude spectrum (single-sided)
    int num_bins = fft_size / 2;  //we only need the positive frequencies
    float normalisation = 1.0f / (float) fft_size;  //normalise by FFT size

    for (int i = 0; i < num_bins; i++) {
        float re = freq_data[i].r;
        float im = freq_data[i].i;
        magnitude[i] = sqrtf(re * re + im * im) * normalisation;
    }

    //clean up
    free(freq_data);
    free(windowed_data);
    kiss_fftr_free(cfg);
}
 */