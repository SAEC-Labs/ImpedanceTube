//
// Created by torq on 11/9/26.
//

/**
 * Calibration data model
 *
 * METHOD: RIGID TERMINATION
 *
 * Physical model: with a perfectly rigid cap at x = 0, the standing-wave
 *  pressure at distance x from the cap is p(x) ∝ cos(k·x), where
 *  k = 2πf/c. Then the 'theoretical' transfer function is
 *
 *  H12_rigid(f) = cos(k·x2) / cos(k·x1)
 *
 *  Any deviation of the 'measured' H12_rigid from this real-valued
 *  prediction is attributed to the electrical bias of the two
 *  microphones / ADC channels. We capture this as a complex
 *  correction factor:
 *
 *  C(f) = H12_measured(f) / H12_theoretical(f)
 *
 *  Future sample measurements are then corrected by:
 *
 *  H12_corrected(f) = H12_raw(f) / C(f)
 *
 * Conventions:
 *   - Cap face at x = 0.
 *   - Mic 1 at x1_to_cap_m (further from cap).
 *   - Mic 2 at x2_to_cap_m (closer to cap).
 *   - x1 > x2 > 0.
 *
 * Persistence: Save/Load plain text key value format (JSON later)
 */

#ifndef SAEC_STUDIO_CALIBRATION_H
#define SAEC_STUDIO_CALIBRATION_H

/* -------------------------------------------------
 * TUBE GEOMETRY
 * --------------------------------------------------
 */

#define CAL_DEFAULT_MIC_SPACING 0.050
#define CAL_DEFAULT_X1_TO_CAP_M 0.100
#define CAL_DEFAULT_X2_TO_CAP_M 0.050
#define CAL_DEFAULT_SPEED_OF_SOUND 343.0
#define CAL_DEFAULT_TEMPERATURE_C 20.0

//validity thresolds
#define CAL_POLE_THRESHOLD 0.10 // |cos| < this, skip
#define CAL_COHERENCE_MIN 0.50 // coh below this one, skip

//calibratiom will be useless if fewer bins than this are valid
#define CAL_MIN_VALID_BINS 50


typedef struct {
 double mic_spacing_m; // s = x1 - x2
 double x1_to_cap_m; //mic 1 distance to cap
 double x2_to_cap_m; //mic 2 distance to cap
 double speed_of_sound_mps; // c
 int sample_rate;
 int fft_size;
 int num_bins;
} TubeParams;

typedef struct {
 int num_valid_bins;
 double min_freq_hz;  //lowest bin's center freq
 double max_freq_hz; //highest valid bin's center freq
 double max_amplitude_dev_db; //max |20.log10(|C|)| over valid bins
 double max_phase_dev_deg; //max |arg(C)| over valid bins
 double mean_coherence;
 double measured_at_temperature_c;
 char timestamp[32]; // "YYYY-MM-DDTHH:MM:SSZ"
} CalibrationDiagnostics;

//lifecyle

int calibration_init(int num_bins);
void calibration_free(void);

/***
 * Invalidate any current calibration (buffers remain allocated)
 * use before starting a new rigid measurement
 */
void calibration_clear(void);

//Tube params

void calibration_set_tube_params(const TubeParams *params);
const TubeParams* calibration_get_tube_params(void);

/* ---------------------------------------------------------------------------
 * computation
 * ---------------------------------------------------------------------------*/

/**
 * Compute the correction function C(f) from a rigid-termination
 * measurement.
 *
 * @param G11       Auto-spectrum of channel 1 (real, length num_bins)
 * @param G22       Auto-spectrum of channel 2 (real, length num_bins)
 * @param G12_real  Cross-spectrum real part
 * @param G12_imag  Cross-spectrum imaginary part
 *
 * @return 0 on success, -1 on error (invalid params or too few valid bins)
 */
int calibration_compute_from_rigid(const float *G11, const float *G22, const float *G12_real, const float *G12_imag);


/**
 * Compute the theoretical H12_rigid at all bins for the current tube
 * parameters. Useful for diagnostics or display.
 *
 * @param h12_real  Output real part (length num_bins)
 * @param h12_imag  Output imaginary part (always 0 in theory)
 *
 * @return 0 on success, -1 on error
 */
int calibration_compute_theoretical_h12(float *h12_real, float *h12_imag);


/**
 * Apply the stored correction to a raw measured H12:
 *     H12_corrected = H12_raw / C
 *
 * Invalid bins are passed through unchanged. Caller is expected to
 * check `calibration_is_valid_bin(i)` before using those bins.
 *
 * @param in_real   Raw H12 real (length num_bins)
 * @param in_imag   Raw H12 imag (length num_bins)
 * @param out_real  Corrected H12 real (length num_bins)
 * @param out_imag  Corrected H12 imag (length num_bins)
 *
 * @return 0 on success, -1 on error
 */
int calibration_apply(const float *in_real, const float *in_imag, float *out_real, float *out_imag);


/**
 * Save the current calibration to disk. Plaon text key values lines
 * @return 0 on success, -1 on error
 */
int calibration_save(const char *path);

/**
 * Load a calibration file. Replaces any existing calibration
 * @return 0 on success, -1 on error
 */
int calibration_load(const char *path);



int calibration_is_loaded(void);
int calibration_is_valid_bin(int bin);
int calibration_get_num_bins(void);
const CalibrationDiagnostics* calibration_get_diagnostics(void);

//direct access for display, but only valid when calibration_is_loaded()
const float *calibration_get_corr_real(void);
const float *calibration_get_corr_imag(void);

//utils

//speed of sound approximation with: c = 331.3 + 0.606·T(°C) m/s
double calibration_speed_of_sound(double temp_c);

void calibration_selftest(void);

#endif //SAEC_STUDIO_CALIBRATION_H
