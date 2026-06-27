//
// Created by torq on 6/20/26.
//

/**
 * dsp.h – Digital signal processing for audio analysis
 *
 * Provides functions for windowing, FFT, and magnitude spectrum computation
 * using the KissFFT library. Designed for real-time audio analysis.
 */

#ifndef SAECTUBE_DSP_H
#define SAECTUBE_DSP_H

#include <stdint.h>

/**
 * Compute the magnitude spectrum of a time-domain signal.
 *
 * This function applies a Hann window, computes the FFT, and returns
 * the single-sided magnitude spectrum (normalised by FFT size).
 *
 * @param time_data   Input: array of float samples (length = fft_size)
 * @param magnitude   Output: array of float magnitudes (length = fft_size/2)
 * @param fft_size    Number of samples in the FFT (must be power of 2)
 */
void compute_spectrum(const float *time_data, float *magnitude, int fft_size);

/**
 * Apply a Hann window to a time-domain signal in-place.
 *
 * @param data  Input/output: array of floats to be windowed
 * @param n     Length of the array
 */
void apply_hann_window(float *data, int n);

#endif //SAECTUBE_DSP_H
