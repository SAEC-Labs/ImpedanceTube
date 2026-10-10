//
// Created by torq on 09/8/26.
//

/** Welch's spectral averaging for two-channel measurements
 *
 * Accumulates a sliding buffer of time-domain samples from two
 * microphone channels, applies a Hann window, computes FFTs, and
 * averages auto-spectra and cross-spectra across K segments.
 *
 * Outputs (when status == MEASURE_STATUS_COMPLETE):
 *   G11      – averaged auto-spectrum of channel 1  (real)
 *   G22      – averaged auto-spectrum of channel 2  (real)
 *   G12_real – real part of averaged cross-spectrum
 *   G12_img – imaginary part of averaged cross-spectrum
 *
 * All spectra are one-sided (f ≥ 0), with bins 1..N/2−1 doubled to
 * preserve total power. Bin 0 (DC) and bin N/2 (Nyquist) are kept as is.
 */

#ifndef SAEC_STUDIO_WELCH_H
#define SAEC_STUDIO_WELCH_H


#define MEASURE_FFT_SIZE 2048
#define MEASURE_SHIFT 1024
#define MEASURE_NUM_SEGMENTS 32
#define MEASURE_NUM_BINS (MEASURE_FFT_SIZE / 2 + 1 ) //1025

typedef enum {
    MEASURE_STATUS_IDLE = 0,
    MEASURE_STATUS_ACCUMULATING,
    MEASURE_STATUS_COMPLETE
} MeasureStatus;

/**
 * Initialise the measurement subsystem. Allocates all buffers and
 * pre-computes the Hann window. Safe to call once at startup.
 * @return 0 on success, -1 on failure.
 */
int measure_init(void);

/**
 * Free all allocated resources.
 */
void measure_free(void);

/**
 * Reset accumulators for new measurement
 * Buffers remain allocated, only running sums and state are cleared
 */
void measure_reset(void);


/**
 * Feed a block of samples (one per channel).
 *
 * This is the main entry point of feeding. Feed arbitrary block sizes, the
 * internal buffer handles alignment to FFT boundaries automatically.
 *
 * @param ch1          Channel 1 samples (length = num_samples)
 * @param ch2          Channel 2 samples (length = num_samples)
 * @param num_samples  Samples per channel
 * @return  -1 on error, 0 if still accumulating, 1 if measurement complete
 */
int measure_feed_block(const float *ch1, const float *ch2, int num_samples);

/**
 * Get current status
 */
MeasureStatus measure_get_status(void);

/**
 * Get progress as a percentage
 */
int measure_get_progress(void);

/**
 * Gte number of freq bins, always MEASURE_NUM_BINS
 */
int measure_get_num_bins(void);

//averaged sprectra, valid when status == COMPLETE
const float *measure_get_G11(void);       //auto-spectrum ch1   (real)
const float *measure_get_G22(void);       //auto-spectrum ch2   (real)
const float *measure_get_G12_real(void);  //cross-spectrum ch1->ch2 (real)
const float *measure_get_G12_imag(void);  //cross-spectrum ch1->ch2 (img)

/**
 * Convert a bin index to freq in Hz
 */
float measure_bin_to_hz(int bin, float sample_rate);


/**
 * Do a self test.
 * Feed on two synthetic sine waves and verifies the math... please be correct (~_~)
 */
void measure_selftest(void);

#endif //SAEC_STUDIO_WELCH_H
