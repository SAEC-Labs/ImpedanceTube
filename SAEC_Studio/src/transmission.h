//
// Created by torq on 11/20/26.
//

#ifndef SAEC_STUDIO_TRANSMISSION_H
#define SAEC_STUDIO_TRANSMISSION_H

/**
 * transmission.h -> Two-microphone upstream-downstream transmission measurements
 *
 * THIS IS THE FIRST TIME THIS METHOD IS BEING DONE. YOU ARE WITNESSING AN INNOVATION IN REAL TIME!!
 *
 * Physics:
 *   H12(f)   = P2(f)/P1(f) = G12/G11   (raw transfer function)
 *   Tp(f)    = H12_sample(f) / H12_ref(f) * exp(-j·k0·L)
 *   k0(f)    = 2π·f / c0
 *   TL(f)    = -20·log10(|Tp(f)|)
 *   T(f)     = |Tp(f)|²  (power transmission coefficient)
 *   L2(f)    = 10·log10(G22(f))  (downstream power level dB)
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
#define TRANSMISSION_EPS 1e-20f
#define TRANSMISSION_MAG_FLOOR_DB (-200.0f)

//coherence gate, bins below this are marked invalid for TL
#define TRANSMISSION_COHERENCE_THRESHOLD 0.80f

#define TRANSMISSION_PHASE_SMOOTH_RADIUS 2 //+-2bins = 5bin moving avg

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
 *   Tp = (H_sample / H_ref) * exp(-j.k0.L)
 * with k0 = 2π * f/c0 and f = bin_index * sample_rate / fft_size.
 * Returns 0 on success, -1 on error (missing reference or bad args).
 * ------------------------------------------------------------------------- */
int  transmission_compute(const float *G11, const float *G22, const float *G12_real,const float *G12_imag, int sample_rate);

const float *transmission_get_Tp_real(void);
const float *transmission_get_Tp_imag(void);
const float *transmission_get_Tp_mag_db(void);   //20.log10|Tp|
const float *transmission_get_TL_db(void);       //-20.log10|Tp|

const float *transmission_get_power_T(void); //|Tp|^2
const float *transmission_get_downstream_level_db(void); //10.log10(G22)
const int *transmission_get_tl_valid(void); //per bin 0/1

//Diagnostica stats, recomputed on each compute over valid bins only
int transmission_get_num_valid_tl_bins(void);
double transmission_get_min_tl_db(void);
double transmission_get_max_tl_db(void);
double transmission_get_mean_tl_db(void);
int transmission_get_num_negative_tl_bins(void);

double transmission_get_mean_group_delay_us(void);
double transmission_get_phase_span_rad(void);

const float *transmission_get_phase_unwrapped(void); //radians
const float *transmission_get_group_delay_us(void); //micrro seconds
const float *transmission_get_inferred_alpha(void);



//Metadata
const TransmissionDiagnostics *transmission_get_diagnostics(void);
int transmission_get_num_bins(void);
double transmission_get_thickness_m(void);

//selftest
void transmission_selftest(void);

#endif //SAEC_STUDIO_TRANSMISSION_H
