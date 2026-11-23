//
// Created by torq on 11/20/26.
//

#ifndef SAEC_STUDIO_TRANSMISSION_H
#define SAEC_STUDIO_TRANSMISSION_H

/**
 * transmission.h -> Two-microphone upstream-downstream transmission measurements
 *
 * THIS IS THE FIRST TIME THIS METHOD IS BEING DONE!!
 *
 * Physics:
 *   H12(f)   = P2(f)/P1(f) = G12/G11   (raw transfer function)
 *   Tp(f)    = H12_sample(f) / H12_ref(f) * exp(-j·k0·L)
 *   k0(f)    = 2π·f / c0
 *   TL(f)    = -20·log10(|Tp(f)|)
 *
 * Reference-based method: the empty-tube measurement cancels
 * microphone channel gain/phase mismatch and propagation through the
 * d_u and d_d air sections, leaving only the specimen-referenced
 * transmission response and the specimen thickness phase factor.
 */

#include <stdint.h>

/* Default speed of sound, m/s at ~20 degrres C */
#define TRANSMISSION_DEFAULT_C0 343.0

//numeric safety thresholds
#define TRANSMISSION_EPS = 1e-20f
#define TRANSMISSION_MAG_FLOOR_DB (-200.0f)

typedef struct {
    int num_valid_bins;
    double mean_coherence;
    char timestamp[32];
} TransmissionDiagnostics;

int transmission_init(int num_bins);
void transmission_free(void);

/* -------------------------------------------------------------------------
 * Reference handling
 *
 *computes H_ref = G12/G11 internally,
 * marks bins with G11 < EPS or |H_ref| < EPS as invalid, and stores the
 * result. Diagonistica are computed from the coherence.
 * ------------------------------------------------------------------------- */
int transmission_capture_reference(const float *G11, const float *G22, const float *G12_real, const float *G12_imag, double thickness_m, int sample_rate);


int  transmission_has_reference(void);
void transmission_clear_reference(void);
int  transmission_save_reference(const char *path);
int  transmission_load_reference(const char *path);

/* -------------------------------------------------------------------------
 * Compute Tp and TL for a sample measurement
 *
 * Requires has_reference() == 1. Computes Tp per bin using
 *   Tp = (H_sample / H_ref) * exp(-j·k0·L)
 * with k0 = 2π·f/c0 and f = bin_index · sample_rate / fft_size.
 * Returns 0 on success, -1 on error (missing reference or bad args).
 * ------------------------------------------------------------------------- */
int  transmission_compute(const float *G11, const float *G22, const float *G12_real,const float *G12_imag, int sample_rate);

const float *transmission_get_Tp_real(void);
const float *transmission_get_Tp_imag(void);
const float *transmission_get_Tp_mag_db(void);   //20·log10|Tp|
const float *transmission_get_TL_db(void);       //-20·log10|Tp|

//some future placeholders, return NULL until implemented!!
const float *transmission_get_phase_unwrapped(void);
const float *transmission_get_group_delay_us(void);
const float *transmission_get_inferred_alpha(void);

//Metadata
const TransmissionDiagnostics *transmission_get_diagnostics(void);
int  transmission_get_num_bins(void);
double transmission_get_thickness_m(void);

//selftest
void transmission_selftest(void);

#endif //SAEC_STUDIO_TRANSMISSION_H
