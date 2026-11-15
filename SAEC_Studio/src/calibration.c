//
// Created by torq on 11/9/26.
//

/* Implementation of rigid termination calibration */

#include "calibration.h"
#include "dsp/welch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#ifndef M_PI2
#define M_PI2 3.14159265358979323846
#endif

typedef struct {
    int num_bins;
    int initialized;
    int has_calibration;
    TubeParams params;
    CalibrationDiagnostics diagnostics;
    float *corr_real;
    float *corr_imag;
    int *valid;
} CalibrationContext;

static CalibrationContext c_ctx = {0};


double calibration_speed_of_sound(const double temp_c) {
    return 331.3 + 0.606 * temp_c;
}

static void set_timestamp_now(char *buf, size_t buflen) {
    const time_t now = time(NULL);
    const struct tm *t = gmtime(&now);

    if (t) strftime(buf, buflen, "%Y-%m-&dT%H:%M:%SZ", t);
    else strncpy(buf, "0000-00-00T00:00:00Z", buflen - 1);

}

/* compute theoretical H12 at a single bin, real valued */
static double theoretical_h12_at(const int bin, const TubeParams *p) {
    // f = bin . fs / fft_size
    const double fs = p->sample_rate;
    const double N = p->fft_size;
    const double f = (double)bin * fs / N;
    const double k = 2.0 * M_PI2 * f / p->speed_of_sound_mps;

    const double cx1 = cos(k * p->x1_to_cap_m);
    const double cx2 = cos(k * p->x2_to_cap_m);

    if (fabs(cx1) < 1e-12) {
        return 0.0; //pole, caller skips
    }
    return cx2 / cx1;
}


int calibration_init(const int num_bins) {
    if (c_ctx.initialized) {
        return 0;
    }
    if (num_bins <= 0) {
        return -1;
    }

    c_ctx.num_bins = num_bins;
    c_ctx.corr_real = (float*)calloc(num_bins, sizeof(float));
    c_ctx.corr_imag = (float*)calloc(num_bins, sizeof(float));
    c_ctx.valid = (int*)calloc(num_bins, sizeof(int));

    if (!c_ctx.corr_real || !c_ctx.corr_imag || !c_ctx.valid) {
        calibration_free();
        return -1;
    }

    //default tube params
    c_ctx.params.mic_spacing_m = CAL_DEFAULT_MIC_SPACING;
    c_ctx.params.x1_to_cap_m = CAL_DEFAULT_X1_TO_CAP_M;
    c_ctx.params.x2_to_cap_m = CAL_DEFAULT_X2_TO_CAP_M;
    c_ctx.params.speed_of_sound_mps = CAL_DEFAULT_SPEED_OF_SOUND;
    c_ctx.params.sample_rate = 44100;
    c_ctx.params.fft_size = MEASURE_FFT_SIZE;
    c_ctx.params.num_bins = num_bins;

    c_ctx.diagnostics.measured_at_temperature_c = CAL_DEFAULT_TEMPERATURE_C;
    c_ctx.has_calibration = 0;
    c_ctx.initialized = 1;

    printf("calibration: initialized (%d bins, geometry x1=%.3f m, x2=%.3f m, c=%.1f m/s)\n",
        num_bins, c_ctx.params.x1_to_cap_m, c_ctx.params.x2_to_cap_m, c_ctx.params.speed_of_sound_mps);
    return 0;
}

void calibration_free(void) {
    free(c_ctx.corr_real);
    free(c_ctx.corr_imag);
    free(c_ctx.valid);
    memset(&c_ctx, 0, sizeof(c_ctx));
}

void calibration_clear(void) {
    if (!c_ctx.initialized) {
        return;
    }

    memset(c_ctx.corr_real, 0, c_ctx.num_bins * sizeof(float));
    memset(c_ctx.corr_imag, 0, c_ctx.num_bins * sizeof(float));
    memset(c_ctx.valid,     0, c_ctx.num_bins * sizeof(int));
    memset(&c_ctx.diagnostics, 0, sizeof(c_ctx.diagnostics));
    c_ctx.has_calibration = 0;
    printf("calibration: cleared\n");
}

void calibration_set_tube_parameters(const TubeParams *params) {
    if (!c_ctx.initialized || !params) {
        return;
    }
    c_ctx.params = *params;
    c_ctx.params.num_bins = c_ctx.num_bins;
    printf("calibration: parameters set (x1=%.3f, x2=%.3f, c=%.1f m/s)\n",
           params->x1_to_cap_m, params->x2_to_cap_m, params->speed_of_sound_mps);
}

const TubeParams* calibration_get_tube_parameters(void) {
    return &c_ctx.params;
}


/* ------Computation--------------------------------*/

int calibration_compute_theoretical_h12(float *h12_real, float *h12_imag) {
    if (!c_ctx.initialized || !h12_real || !h12_imag) {
        return -1;
    }

    for (int i = 0; i < c_ctx.num_bins; i++) {
        const double v = theoretical_h12_at(i, &c_ctx.params);
        h12_real[i] = (float)v;
        h12_imag[i] = 0.0f;
    }
    return 0;
}

int calibration_compute_from_rigid(const float *G11, const float *G22, const float *G12_real, const float *G12_imag) {
    if (!c_ctx.initialized || !G11 || !G22 || !G12_real || !G12_imag) {
        return -1;
    }

    double max_amp_db = 0.0;
    double max_phase_deg = 0.0;
    double coh_sum = 0.0;
    int coh_count = 0;
    int valid_count = 0;
    double min_freq = 1e9, max_freq = 0.0;

    const double fs = c_ctx.params.sample_rate;
    const double N = c_ctx.params.fft_size;
    const double df = fs / N;

    for (int i = 0; i < c_ctx.num_bins; i++) {
        //Identity defaults, will be overwritten on success
        c_ctx.corr_real[i] = 1.0f;
        c_ctx.corr_imag[i] = 0.0f;
        c_ctx.valid[i] = 0;

        if (i == 0) {
            continue; //DC excluded
        }

        const double g11 = G11[i];
        const double g22 = G22[i];
        const double s12r = G12_real[i];
        const double s12i = G12_imag[i];

        //Gate 1 for coherence
        double coh = 0.0;
        if (g11 > 1e-20 && g22 > 1e-20) {
            coh = (s12r * s12r + s12i * s12i) / (g11 * g22);

            if (coh > 1.0) {
                coh = 1.0;
            }
            if (coh < 0.0) {
                coh = 0.0;
            }
        }
        if (isfinite(coh)) {
            coh_sum += coh;
            coh_count++;
        }
        if (coh < CAL_COHERENCE_MIN) continue;

        //Gate 2 for theoretical H12 poles
        const double f = (double)i * df;
        const double k = 2.0 * M_PI * f / c_ctx.params.speed_of_sound_mps;
        const double cx1 = cos(k * c_ctx.params.x1_to_cap_m);
        const double cx2 = cos(k * c_ctx.params.x2_to_cap_m);

        if (fabs(cx1) < CAL_POLE_THRESHOLD) continue;
        if (fabs(cx2) < CAL_POLE_THRESHOLD) continue;

        const double h12_theo = cx2 / cx1;

        // Gate 3 for valid S11
        if (g11 < 1e-20) continue;

        //Compute measured H12 and correction C = H12_meas / H12_theo (~_~)
        const double h12_meas_real = s12r / g11;
        const double h12_meas_imag = s12i / g11;

        const double c_re = h12_meas_real / h12_theo;
        const double c_im = h12_meas_imag / h12_theo;

        //Gate 4 for finite check
        if (!isfinite(c_re) || !isfinite(c_im)) continue;

        //Accept this bin now!!!
        c_ctx.corr_real[i] = (float)c_re;
        c_ctx.corr_imag[i] = (float)c_im;
        c_ctx.valid[i] = 1;
        valid_count++;

        //Diagnostica
        const double mag = sqrt(c_re * c_re + c_im * c_im);
        const double amp_db = 20.0 * log10(mag > 1e-12 ? mag : 1e-12);
        const double phase_d = atan2(c_im, c_re) * 180.0 / M_PI2;

        if (fabs(amp_db) > max_amp_db) {
            max_amp_db = fabs(amp_db);
        }
        if (fabs(phase_d) > max_phase_deg) {
            max_phase_deg = fabs(phase_d);
        }

        if (f < min_freq) min_freq = f;
        if (f > max_freq) max_freq = f;
    }

    //Finalize diagnostica
    c_ctx.diagnostics.num_valid_bins = valid_count;
    c_ctx.diagnostics.max_amplitude_dev_db = max_amp_db;
    c_ctx.diagnostics.max_phase_dev_deg = max_phase_deg;

    c_ctx.diagnostics.mean_coherence = (coh_count > 0) ? coh_sum / (double)coh_count : 0.0;

    c_ctx.diagnostics.min_freq_hz = (valid_count > 0) ? min_freq : 0.0;
    c_ctx.diagnostics.max_freq_hz = (valid_count > 0) ? max_freq : 0.0;

    set_timestamp_now(c_ctx.diagnostics.timestamp, sizeof(c_ctx.diagnostics.timestamp));

    fprintf(stderr, "[calib] total=%d  coh_count=%d  valid_count=%d\n", c_ctx.num_bins - 1, coh_count, valid_count);

    if (valid_count < CAL_MIN_VALID_BINS) {
        fprintf(stderr, "calibration_compute_from_rigid: only %d valid bins " "(< %d required). Check excitation band and mic setup.\n", valid_count, CAL_MIN_VALID_BINS);
        c_ctx.has_calibration = 0;
        return -1;
    }

    c_ctx.has_calibration = 1;

    printf("calibration: computed  valid=%d/%d  maxAmp=%.2f dB  maxPhase=%.2f°  meanCoh=%.3f\n",
           valid_count, c_ctx.num_bins,
           max_amp_db, max_phase_deg, c_ctx.diagnostics.mean_coherence);

    printf("valid band: %.0f Hz – %.0f Hz\n", c_ctx.diagnostics.min_freq_hz, c_ctx.diagnostics.max_freq_hz);
    return 0;
}

int calibration_apply(const float *in_real, const float *in_imag, float *out_real, float *out_imag) {
    if (!c_ctx.initialized || !in_real || !in_imag || !out_real || !out_imag) {
        return -1;
    }

    for (int i = 0; i < c_ctx.num_bins; i++) {
        //Pass hrough for invalid bins
        if (!c_ctx.valid[i]) {
            out_real[i] = in_real[i];
            out_imag[i] = in_imag[i];
            continue;
        }

        const float cr = c_ctx.corr_real[i];
        const float ci = c_ctx.corr_imag[i];
        const double den = (double)cr * cr + (double)ci * ci;

        if (den < 1e-20) {
            out_real[i] = in_real[i];
            out_imag[i] = in_imag[i];
            continue;
        }

        const double inr = in_real[i];
        const double ini = in_imag[i];
        const double rr  = (inr * cr + ini * ci) / den;
        const double ri  = (ini * cr - inr * ci) / den;

        out_real[i] = (float)rr;
        out_imag[i] = (float)ri;
    }
    return 0;
}



/*------------------Persistence------------------------------*/

int calibration_save(const char *path) {
    if (!c_ctx.initialized || !c_ctx.has_calibration || !path) {
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "calibration_save: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(f, "# SAEC Studio Calibration File v1\n");
    fprintf(f, "# Rigid termination calibration with theoretical cos ratio model.\n");
    fprintf(f, "\n");
    fprintf(f, "version 1\n");
    fprintf(f, "timestamp             %s\n", c_ctx.diagnostics.timestamp);
    fprintf(f, "mic_spacing_m         %.9g\n", c_ctx.params.mic_spacing_m);
    fprintf(f, "x1_to_cap_m           %.9g\n", c_ctx.params.x1_to_cap_m);
    fprintf(f, "x2_to_cap_m           %.9g\n", c_ctx.params.x2_to_cap_m);
    fprintf(f, "speed_of_sound_mps    %.9g\n", c_ctx.params.speed_of_sound_mps);
    fprintf(f, "sample_rate           %d\n",   c_ctx.params.sample_rate);
    fprintf(f, "fft_size              %d\n",   c_ctx.params.fft_size);
    fprintf(f, "num_bins              %d\n",   c_ctx.num_bins);
    fprintf(f, "num_valid_bins        %d\n",   c_ctx.diagnostics.num_valid_bins);
    fprintf(f, "min_freq_hz           %.9g\n", c_ctx.diagnostics.min_freq_hz);
    fprintf(f, "max_freq_hz           %.9g\n", c_ctx.diagnostics.max_freq_hz);
    fprintf(f, "max_amplitude_dev_db  %.9g\n", c_ctx.diagnostics.max_amplitude_dev_db);
    fprintf(f, "max_phase_dev_deg     %.9g\n", c_ctx.diagnostics.max_phase_dev_deg);
    fprintf(f, "mean_coherence        %.9g\n", c_ctx.diagnostics.mean_coherence);
    fprintf(f, "temperature_c         %.9g\n", c_ctx.diagnostics.measured_at_temperature_c);

    fprintf(f, "\n# Correction array: corr <index> <valid> <real> <imag>\n");

    for (int i =0; i < c_ctx.num_bins; i++) {
        fprintf(f, "corr %d %d %.7g %.7g\n", i, c_ctx.valid[i], c_ctx.corr_real[i], c_ctx.corr_imag[i]);
    }

    fclose(f);
    printf("calibration: saved to '%s'\n", path);

    return 0;
}

int calibration_load(const char *path) {
    if (!c_ctx.initialized || !path) {
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "calibration_load: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    TubeParams tp = c_ctx.params;
    int num_bins_file = 0;
    double ts_temp = 0.0;
    char ts_buf[32] = {0};

    //Reset correction srrays
    for (int i = 0; i < c_ctx.num_bins; i++) {
        c_ctx.corr_real[i] = 1.0f;
        c_ctx.corr_imag[i] = 0.0f;
        c_ctx.valid[i] = 0;
    }

    char line[512];
    int corr_loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        //skip blank lines and comments
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }

        char key[32];
        if (sscanf(line, "corr %*d %*d %*f %*f") == 0 && strncmp(line, "corr ", 5) == 0) {
            int idx, vld;
            float rr, ii;

            if (sscanf(line, "corr %d %d %f %f", &idx, &vld, &rr, &ii) == 4) {
                if (idx >= 0 && idx < c_ctx.num_bins) {
                    c_ctx.valid[idx] = vld ? 1 : 0;
                    c_ctx.corr_real[idx] = rr;
                    c_ctx.corr_imag[idx] = ii;
                    corr_loaded++;
                }
            }
            continue;
        }
        if (sscanf(line, "%31s", key) != 1) {
            continue;
        }
        if (strcmp(key, "timestamp") == 0) {
            sscanf(line, "timestamp %31s", ts_buf);
        } else {
            double v;
            if (sscanf(line, "%*s %lf", &v) != 1) {
                continue;
            }

            if      (strcmp(key, "mic_spacing_m")        == 0) tp.mic_spacing_m        = v;
            else if (strcmp(key, "x1_to_cap_m")          == 0) tp.x1_to_cap_m          = v;
            else if (strcmp(key, "x2_to_cap_m")          == 0) tp.x2_to_cap_m          = v;
            else if (strcmp(key, "speed_of_sound_mps")   == 0) tp.speed_of_sound_mps   = v;
            else if (strcmp(key, "sample_rate")          == 0) tp.sample_rate          = (int)v;
            else if (strcmp(key, "fft_size")             == 0) tp.fft_size             = (int)v;
            else if (strcmp(key, "num_bins")             == 0) num_bins_file           = (int)v;
            else if (strcmp(key, "num_valid_bins")       == 0) c_ctx.diagnostics.num_valid_bins        = (int)v;
            else if (strcmp(key, "min_freq_hz")          == 0) c_ctx.diagnostics.min_freq_hz           = v;
            else if (strcmp(key, "max_freq_hz")          == 0) c_ctx.diagnostics.max_freq_hz           = v;
            else if (strcmp(key, "max_amplitude_dev_db") == 0) c_ctx.diagnostics.max_amplitude_dev_db  = v;
            else if (strcmp(key, "max_phase_dev_deg")    == 0) c_ctx.diagnostics.max_phase_dev_deg     = v;
            else if (strcmp(key, "mean_coherence")       == 0) c_ctx.diagnostics.mean_coherence        = v;
            else if (strcmp(key, "temperature_c")        == 0) ts_temp                                 = v;
        }
    }

    fclose(f);

    if (num_bins_file != c_ctx.num_bins) {
        fprintf(stderr, "calibration_load: num_bins mismatch (file=%d, expected=%d)\n", num_bins_file, c_ctx.num_bins);
        return -1;
    }
    if (corr_loaded == 0) {
        fprintf(stderr, "calibration_load: no correction entries found\n");
        return -1;
    }

    //Commit
    c_ctx.params = tp;
    c_ctx.params.num_bins = c_ctx.num_bins;
    c_ctx.diagnostics.measured_at_temperature_c = ts_temp;

    if (ts_buf[0]) {
        strncpy(c_ctx.diagnostics.timestamp, ts_buf, sizeof(c_ctx.diagnostics.timestamp) - 1);
    }
    c_ctx.has_calibration = 1;

    printf("calibration: loaded from '%s' (%d corr entries, valid=%d/%d)\n", path, corr_loaded, c_ctx.diagnostics.num_valid_bins, c_ctx.num_bins);
    return 0;
}

/*-----------------------------------------------------------------------------------------
 State Queries
*----------------------------------------------------------------------------------------*/

int calibration_is_loaded(void) {
    return c_ctx.has_calibration;
}

int calibration_is_valid_bin(const int bin) {
    return (bin >= 0 && bin < c_ctx.num_bins) ? c_ctx.valid[bin] : 0;
}

int calibration_get_num_bins(void) {
    return c_ctx.num_bins;
}
const CalibrationDiagnostics* calibration_get_diagnostics(void) {
    return &c_ctx.diagnostics;
}

const float *calibration_get_corr_real(void) {
    return c_ctx.corr_real;
}

const float *calibration_get_corr_imag(void) {
    return c_ctx.corr_imag;
}


void calibration_selftest(void)
{
    printf("\n===== calibration_selftest (lets verify our math (>_<) =====\n");

    const int nb = MEASURE_NUM_BINS;
    if (calibration_init(nb) != 0) {
        printf("[calib] FAILED: init\n");
        return;
    }

    // --- Tube geometry for tests ---
    TubeParams tp = *calibration_get_tube_parameters();
    tp.x1_to_cap_m        = 0.10;
    tp.x2_to_cap_m        = 0.05;
    tp.speed_of_sound_mps = 343.0;
    tp.sample_rate        = 44100;
    tp.fft_size           = MEASURE_FFT_SIZE;
    calibration_set_tube_parameters(&tp);

    /* =====================================================================
     * Test 1: theoretical H12 at bin's ACTUAL center frequency
     * ===================================================================== */
    {
        float *h12r = malloc(nb * sizeof(float));
        float *h12i = malloc(nb * sizeof(float));

        calibration_compute_theoretical_h12(h12r, h12i);

        const int bin = 4;
        const double f_actual = (double)bin * 44100.0 / (double)MEASURE_FFT_SIZE;
        const double k_actual = 2.0 * M_PI * f_actual / 343.0;
        const double expected = cos(k_actual * 0.05) / cos(k_actual * 0.10);

        printf("[calib T1] H12_theo at bin %d (%.2f Hz): got %.6f  expected %.6f\n", bin, f_actual, h12r[bin], expected);
        printf("[calib T1] %s\n", fabs(h12r[bin] - expected) < 1e-3 ? "PASS" : "FAIL");

        free(h12r);
        free(h12i);
    }

    /* =====================================================================
     * T2: pole detection near 858 Hz (f = c / (4·x1) = 343/0.4 = 857.5)
     * ===================================================================== */
    {
        const double f_pole   = 343.0 / (4.0 * 0.10);
        const int bin_pole = (int)(f_pole * MEASURE_FFT_SIZE / 44100.0);
        const double f_bin = (double)bin_pole * 44100.0 / MEASURE_FFT_SIZE;
        const double cx1 = cos(2.0 * M_PI * f_bin / 343.0 * 0.10);

        printf("[calib T2] pole test near %.0f Hz (bin %d, %.0f Hz): |cos(kx1)|=%.4f\n",
               f_pole, bin_pole, f_bin, fabs(cx1));
        printf("[calib T2] %s\n", fabs(cx1) < 0.2 ? "PASS (pole present)" : "FAIL");
    }

    /* =====================================================================
     * T3: synthetic bias recovery with coherence = 1
     *
     * Injection :
     *   Want:  H12_meas(f) = bias · h12_theo(f),  γ²(f) = 1 at every bin
     *   Choose: S11 = 1
     *           S12 = bias · h12_theo · S11 = bias · h12_theo
     *           S22 = |bias · h12_theo|^2   (so that |S12|^2 = S11·S22)
     *
     * This guarantees coherence = 1 exactly (|S12|^2 = S11·S22).
     * ===================================================================== */
    {
        float *G11 = calloc(nb, sizeof(float));
        float *G22 = calloc(nb, sizeof(float));
        float *G12r = calloc(nb, sizeof(float));
        float *G12i = calloc(nb, sizeof(float));
        float *H12r = malloc(nb * sizeof(float));
        float *H12i = malloc(nb * sizeof(float));

        calibration_compute_theoretical_h12(H12r, H12i);

        //Known bias: 3% amplitude, ~1.11° phase
        const double bias_r = 1.03;
        const double bias_i = 0.02;
        const double bias_sq = bias_r * bias_r + bias_i * bias_i;

        int injected = 0;
        for (int i = 1; i < nb; i++) {
            const double h = H12r[i];

            //Skip bins where h is 0 or huge ,can't inject cleanly there
            if (fabs(h) < 1e-3 || fabs(h) > 1e3) {
                G11[i] = 1e-30f;
                G22[i] = 1e-30f;
                G12r[i] = 0.0f;
                G12i[i] = 0.0f;
                continue;
            }

            G11[i]  = 1.0f;
            G22[i]  = (float)(bias_sq * h * h);   // ensures coh = 1
            G12r[i] = (float)(bias_r * h);
            G12i[i] = (float)(bias_i * h);
            injected++;
        }

        printf("[calib T3] injected %d bins with coherence=1\n", injected);

        const int rc = calibration_compute_from_rigid(G11, G22, G12r, G12i);
        printf("[calib T3] compute_from_rigid returned %d\n", rc);

        const CalibrationDiagnostics *dg = calibration_get_diagnostics();
        printf("[calib T3] valid=%d  maxAmp=%.4f dB  maxPhase=%.4f°  meanCoh=%.4f\n",
               dg->num_valid_bins, dg->max_amplitude_dev_db,
               dg->max_phase_dev_deg, dg->mean_coherence);

        const double expected_amp_db  = 20.0 * log10(sqrt(bias_sq));
        const double expected_phase_d = atan2(bias_i, bias_r) * 180.0 / M_PI;
        printf("[calib T3] expected maxAmp=%.4f dB  maxPhase=%.4f°\n", expected_amp_db, expected_phase_d);

        //Verify a specific bin (pick one that's likely valid)
        const int test_bin = 10;
        printf("[calib T3] C[%d]: %.6f + j·%.6f  (expected %.4f + j·%.4f)\n",
               test_bin,
               calibration_get_corr_real()[test_bin],
               calibration_get_corr_imag()[test_bin],
               bias_r, bias_i);

        const int t3_ok = (rc == 0) &&
                    (fabs(calibration_get_corr_real()[test_bin] - bias_r) < 0.01f) &&
                    (fabs(calibration_get_corr_imag()[test_bin] - bias_i) < 0.01f);
        printf("[calib T3] %s\n", t3_ok ? "PASS" : "FAIL");

        /* =================================================================
         * T4: save / clear / load
         * ================================================================= */
        const char *test_path = "/Templates/saecstudio_calibration_selftest.txt";
        const int rsave = calibration_save(test_path);
        printf("[calib T4] save -> %d (%s)\n", rsave, test_path);

        const float saved_r = calibration_get_corr_real()[test_bin];
        const float saved_i = calibration_get_corr_imag()[test_bin];

        calibration_clear();

        printf("[calib T4] after clear: is_loaded=%d\n", calibration_is_loaded());

        const int rload = calibration_load(test_path);
        printf("[calib T4] load -> %d\n", rload);
        printf("[calib T4] is_loaded=%d  C[%d]=%.6f+j·%.6f  (saved %.6f+j·%.6f)\n",
               calibration_is_loaded(), test_bin,
               calibration_get_corr_real()[test_bin],
               calibration_get_corr_imag()[test_bin],
               saved_r, saved_i);

        const int t4_ok = (rload == 0) &&
                    (fabsf(calibration_get_corr_real()[test_bin] - saved_r) < 1e-5f) &&
                    (fabsf(calibration_get_corr_imag()[test_bin] - saved_i) < 1e-5f);
        printf("[calib T4] %s\n", t4_ok ? "PASS" : "FAIL");

        /* =================================================================
         * T5: apply tests
         *   5a. Invalid bin -> pass through (identity)
         *   5b. Valid bin   -> correct division by C
         * ================================================================= */
        float *tmp_r = calloc(nb, sizeof(float));
        float *tmp_i = calloc(nb, sizeof(float));

        //5a: find an invalid bin and verify pass-through
        int invalid_bin = -1;
        for (int i = 1; i < nb; i++) {
            if (!calibration_is_valid_bin(i)) {
                invalid_bin = i;
                break;
            }
        }
        if (invalid_bin < 0) {
            printf("[calib T5a] SKIP: no invalid bins found\n");
        } else {
            tmp_r[invalid_bin] = 0.8f;
            tmp_i[invalid_bin] = 0.3f;

            calibration_apply(tmp_r, tmp_i, tmp_r, tmp_i);

            printf("[calib T5a] invalid bin %d  in=(0.8000+j·0.3000)  out=(%.4f+j·%.4f)\n",
                   invalid_bin,
                   tmp_r[invalid_bin], tmp_i[invalid_bin]);
            printf("[calib T5a] %s\n",
                   (fabsf(tmp_r[invalid_bin] - 0.8f) < 1e-5f &&
                    fabsf(tmp_i[invalid_bin] - 0.3f) < 1e-5f) ? "PASS" : "FAIL");
        }

        //5b: on a valid bin, verify out = in / C exactly
        int valid_bin = -1;
        for (int i = 1; i < nb; i++) {
            if (calibration_is_valid_bin(i)) { valid_bin = i; break; }
        }
        if (valid_bin < 0) {
            printf("[calib T5b] SKIP: no valid bins found\n");
        } else {
            //Clear previous test data
            tmp_r[valid_bin] = 0.8f;
            tmp_i[valid_bin] = 0.3f;

            calibration_apply(tmp_r, tmp_i, tmp_r, tmp_i);

            //Expected: (in_r + j·in_i) / (C_r + j·C_i)
            const double cr = calibration_get_corr_real()[valid_bin];
            const double ci = calibration_get_corr_imag()[valid_bin];
            const double den = cr*cr + ci*ci;
            const double exp_r = (0.8*cr + 0.3*ci) / den;
            const double exp_i = (0.3*cr - 0.8*ci) / den;

            printf("[calib T5b] valid bin %d  C=(%.4f+j·%.4f)\n", valid_bin, cr, ci);
            printf("[calib T5b] in=(0.8000+j·0.3000)  out=(%.4f+j·%.4f)  expected=(%.4f+j·%.4f)\n", tmp_r[valid_bin], tmp_i[valid_bin], exp_r, exp_i);
            printf("[calib T5b] %s\n", (fabs(tmp_r[valid_bin] - exp_r) < 1e-4 && fabs(tmp_i[valid_bin] - exp_i) < 1e-4) ? "PASS" : "FAIL");
        }

        free(tmp_r);
        free(tmp_i);
        free(G11);
        free(G22);
        free(G12r);
        free(G12i);
        free(H12r);
        free(H12i);
    }

    calibration_free();

    printf("===== calibration_selftest complete =====\n\n");
}