//
// Created by torq on 6/20/26.
//

/**
 * uses KissFFT for efficient real-optimised FFT.
 * applies Hann window before FFT to reduce spectral leakage.
 * Normalises magnitudes by FFT size for consistent scaling
 */

#include "dsp.h"
#include "external/kissfft/kiss_fft.h"
#include "external/kissfft/kiss_fftr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * Apply Hann window in-place.
 * w[n] = 0.5 * (1 - cos(2π * n / (N - 1)))
 */
void apply_hann_window(float *data, int n) {
    if ( n < 2) return; //avoid division by zero

    for (int i = 0; i < n; i++) {
        float window = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (n - 1)));
        data[i] *= window;
    }
}

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
