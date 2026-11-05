//
// Created by torq on 10/13/26.
//

/**
 * Derived spectral quantities (H12, coherence)
 *
 * Consumes the raw auto- and cross-spectra produced by dsp/welch.c
 * and computes:
 *   H12(f)          = S12 / S11         transfer function (complex)
 *   |H12(f)|        = 20·log10(|H12|)   magnitude in dB
 *   arg(H12(f))     = phase in radians
 *   γ²(f)           = |S12|² / (S11·S22)   coherence, 0..1
 *
 */

#ifndef SAEC_STUDIO_ANALYZER_H
#define SAEC_STUDIO_ANALYZER_H

/**
 * Initialise the analysis context for a given number of frequency bins.
 * @param num_bins  Number of bins (typically MEASURE_NUM_BINS...mmmh).
 * @return 0 on success, -1 on failure.
 */
int analysis_init(int num_bins);

/**
 * Free all buffers
 */
void analysis_free(void);

/**
 * Compute H12 and coherence from the averaged spectra.
 * All input arrays are length = num_bins, one-sided of course!.
 * Call this after a measurement completes.
 * @return 0 on success, -1 on error.
 */
int analysis_compute(const float *G11, const float *G22, const float *G12_real, const float *G12_imag);


/**
 * Accesssors , only valid after analysis_compute()
 * All buffers are lenght = num_bins
 */
const float *analysis_get_h12_mag_db(void);    //20·log10|H12| in dB
const float *analysis_get_h12_phase_rad(void); //atan2(imag, real)
const float *analysis_get_h12_real(void);      //e(H12)
const float *analysis_get_h12_imag(void);      //Im(H12)
const float *analysis_get_coherence(void);     //γ² in [0, 1]

#endif //SAEC_STUDIO_ANALYZER_H

