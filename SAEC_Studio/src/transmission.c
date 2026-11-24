//
// Created by torq on 11/20/26.
//

#include "transmission.h"
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
    int has_reference;

    //stored reference H12
    float *h12_ref_re;
    float *h12_ref_im;
    int *h12_ref_valid;

    //sample params used with reference
    double thickness_m;
    double c0;
    int sample_rate;

    //computed Tp TL output
    float *tp_re;
    float *tp_im;
    float *tp_mag_db;
    float *tl_db;
    float *power_T;
    float *downstream_level_db;
    int *tl_valid;

    //From capture
    TransmissionDiagnostics diag;

    //Diagnostics from last compute over vlaid beans
    int num_valid_tl;
    int num_negative_tl;
    double min_tl;
    double max_tl;
    double mean_tl;
} TContext;

static TContext t_ctx = {0};


static void set_timestamp(char *buf, size_t buflen) {
    const time_t now = time(NULL);
    const struct tm *t = gmtime(&now);

    if (t) {
        strftime(buf, buflen, "%Y-%m-%d %H:%M:%SZ", t);
    } else {
        strncpy(buf, "0000-00-00T00:00:00Z", buflen - 1);
    }
}

/**
 * Complex ratio a/b.
 * Returns (re, im). Returns 0 if |b| too small.
 * You aren't supposed to understand this.
 */
static int complex_div(const double a_re, const double a_im, const double b_re, const double b_im, double *out_re, double *out_im) {
    const double den = b_re * b_re + b_im * b_im;
    if (den < (double)TRANSMISSION_EPS) {
        return 0;
    }
    *out_re = (a_re * b_re + a_im * b_im) / den;
    *out_im = (a_im * b_re  - a_re * b_im) / den;
    return 1;
}

int transmission_init(int num_bins) {
    if (t_ctx.initialized) {
        return 0;
    }
    if (num_bins <= 0) {
        return -1;
    }

    t_ctx.num_bins = num_bins;
    t_ctx.h12_ref_re = (float*)calloc(num_bins, sizeof(float));
    t_ctx.h12_ref_im = (float*)calloc(num_bins, sizeof(float));
    t_ctx.h12_ref_valid = (int*)calloc(num_bins, sizeof(int));

    t_ctx.tp_re = (float*)calloc(num_bins, sizeof(float));
    t_ctx.tp_im = (float*)calloc(num_bins, sizeof(float));
    t_ctx.tp_mag_db = (float*)calloc(num_bins, sizeof(float));
    t_ctx.tl_db = (float*)calloc(num_bins, sizeof(float));

    t_ctx.power_T = (float*)calloc(num_bins, sizeof(float));
    t_ctx.downstream_level_db = (float*)calloc(num_bins, sizeof(float));
    t_ctx.tl_valid = (int*)calloc(num_bins, sizeof(int));

    if (!t_ctx.h12_ref_re || !t_ctx.h12_ref_im || !t_ctx.h12_ref_valid || !t_ctx.tp_re || !t_ctx.tp_im ||
        !t_ctx.tp_mag_db || !t_ctx.tl_db || !t_ctx.power_T || !t_ctx.downstream_level_db || !t_ctx.tl_valid) {
        transmission_free();
        return -1;
    }

    t_ctx.thickness_m = 0.025;
    t_ctx.c0 = TRANSMISSION_DEFAULT_C0;
    t_ctx.sample_rate = 44100;
    t_ctx.has_reference = 0;
    t_ctx.initialized = 1;

    printf("transmission: initialized (%d bins, c0=%.1f m/s, coh_thresh=%.2f)\n", num_bins, t_ctx.c0, (double)TRANSMISSION_COHERENCE_THRESHOLD);
    return 0;
}

void transmission_free(void) {
    free(t_ctx.h12_ref_re);
    free(t_ctx.h12_ref_im);
    free(t_ctx.h12_ref_valid);
    free(t_ctx.tp_re);
    free(t_ctx.tp_im);
    free(t_ctx.tp_mag_db);
    free(t_ctx.tl_db);
    free(t_ctx.power_T);
    free(t_ctx.downstream_level_db);
    free(t_ctx.tl_valid);
    memset(&t_ctx, 0, sizeof(t_ctx));
}

void transmission_clear_reference(void) {
    if (!t_ctx.initialized) {
        return;
    }
    memset(t_ctx.h12_ref_re, 0, t_ctx.num_bins * sizeof(float));
    memset(t_ctx.h12_ref_im, 0, t_ctx.num_bins * sizeof(float));
    memset(t_ctx.h12_ref_valid, 0, t_ctx.num_bins * sizeof(int));
    memset(&t_ctx.diag, 0, sizeof(t_ctx.diag));

    t_ctx.has_reference = 0;
    printf("transmission: reference cleared\n");
}

//with coherence gating
int transmission_capture_reference(const float *G11, const float *G22, const float *G12_real, const float *G12_imag, double thickness_m, int sample_rate) {
    if (!t_ctx.initialized) {
        return -1;
    }
    if (!G11 || !G22 || !G12_real || !G12_imag) {
        return -1;
    }
    if (thickness_m <= 0.0) {
        return -1;
    }
    if (sample_rate <= 0) {
        return -1;
    }

    t_ctx.thickness_m = thickness_m;
    t_ctx.sample_rate = sample_rate;

    //NOTE: num_bins determine fft_size = 2 * (num_bins -1)

    //Reset arrays
    memset(t_ctx.h12_ref_re, 0, t_ctx.num_bins * sizeof(float));
    memset(t_ctx.h12_ref_im, 0, t_ctx.num_bins * sizeof(float));
    memset(t_ctx.h12_ref_valid, 0, t_ctx.num_bins * sizeof(int));

    int valid = 0;
    double coh_sum = 0.0;
    int coh_count = 0;

    for (int i = 1; i < t_ctx.num_bins; i++) {
        const double g11 = G11[i];
        const double g22 = G22[i];
        const double s12r = G12_real[i];
        const double s12i = G12_imag[i];

        if (g11 < TRANSMISSION_EPS || g22 < TRANSMISSION_EPS) {
            continue;
        }

        //Coherence diagnostica
        double coh = (s12r * s12r + s12i * s12i) / (g11 * g22);
        if (coh > 1.0) coh = 1.0;
        if (coh < 0.0) coh = 0.0;
        if (!isfinite(coh)) {
            continue;
        }
        coh_sum += coh;
        coh_count++;

        //Coherence gate to reject noisy ref bins
        if (coh < (double)TRANSMISSION_COHERENCE_THRESHOLD) {
            continue;
        }

        //H_ref
        const double h_re = s12r / g11;
        const double h_im = s12i / g11;

        if (!isfinite(h_re) || !isfinite(h_im)) {
            continue;
        }

        const double mag = sqrt(h_re * h_re + h_im * h_im);
        if (mag < 1e-8) {
            continue; //unusable ref, too small
        }

        t_ctx.h12_ref_re[i] = (float)h_re;
        t_ctx.h12_ref_im[i] = (float)h_im;
        t_ctx.h12_ref_valid[i] = 1;
        valid++;
    }

    t_ctx.diag.num_valid_bins = valid;
    t_ctx.diag.mean_coherence = (coh_count > 0) ? coh_sum / (double)coh_count : 0.0;

    set_timestamp(t_ctx.diag.timestamp, sizeof(t_ctx.diag.timestamp));

    t_ctx.has_reference = (valid > 0);

    printf("transmission: reference captured  valid=%d/%d  meanCoh=%.3f  L=%.4f m\n", valid, t_ctx.num_bins - 1, t_ctx.diag.mean_coherence, t_ctx.thickness_m);
    return 0;
}

int transmission_has_reference(void) {
    return t_ctx.has_reference;
}
int transmission_get_num_bins(void) {
    return t_ctx.num_bins;
}
double transmission_get_thickness_m(void) {
    return t_ctx.thickness_m;
}
const TransmissionDiagnostics *transmission_get_diagnostics(void) {
    return &t_ctx.diag;
}


/**
 * Persistence with plain text key value
 */
int transmission_save_reference(const char *path) {
    if (!t_ctx.initialized || !t_ctx.has_reference || !path) {
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "transmission_save: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(f, "# SAEC Studio Transmission Reference v1\n");
    fprintf(f, "# Two-microphone upstream-downstream empty-tube reference.\n");
    fprintf(f, "version          1\n");
    fprintf(f, "timestamp        %s\n", t_ctx.diag.timestamp);
    fprintf(f, "thickness_m      %.9g\n", t_ctx.thickness_m);
    fprintf(f, "sample_rate      %d\n",   t_ctx.sample_rate);
    fprintf(f, "num_bins         %d\n",   t_ctx.num_bins);
    fprintf(f, "c0               %.9g\n", t_ctx.c0);
    fprintf(f, "num_valid_bins   %d\n",   t_ctx.diag.num_valid_bins);
    fprintf(f, "mean_coherence   %.9g\n", t_ctx.diag.mean_coherence);
    fprintf(f, "\n# ref <bin> <valid> <real> <imag>\n");

    for (int i = 0; i < t_ctx.num_bins; i++) {
        fprintf(f, "ref %d %d %.7g %.7g\n", i, t_ctx.h12_ref_valid[i], t_ctx.h12_ref_re[i], t_ctx.h12_ref_im[i]);
    }

    fclose(f);
    printf("transmission: reference saved to '%s'\n", path);
    return 0;
}


int transmission_load_reference(const char *path) {
    if (!t_ctx.initialized || !path) {
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "transmission_load: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    double thickness = 0.0;
    double c0 = TRANSMISSION_DEFAULT_C0;
    int sample_rate= 44100;
    int num_bins_f = 0;
    char ts[32] = {0};
    int ref_count = 0;

    //Reset arrays (identity)
    for (int i = 0; i < t_ctx.num_bins; i++) {
        t_ctx.h12_ref_re[i] = 0.0f;
        t_ctx.h12_ref_im[i] = 0.0f;
        t_ctx.h12_ref_valid[i] = 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        if (strncmp(line, "ref ", 4) == 0) {
            int idx, valid;
            float rr, ii;
            if (sscanf(line, "ref %d %d %f %f", &idx, &valid, &rr, &ii) == 4) {
                if (idx >= 0 && idx < t_ctx.num_bins) {
                    t_ctx.h12_ref_valid[idx] = valid ? 1 : 0;
                    t_ctx.h12_ref_re[idx] = rr;
                    t_ctx.h12_ref_im[idx] = ii;
                    ref_count++;
                } //TODOO: strtol instead of sscanf
            }
            continue;
        }

        char key[32];
        if (sscanf(line, "%31s", key) != 1) {
            continue;
        }

        if (strcmp(key, "timestamp") == 0) {
            sscanf(line, "timestamp %31s", ts);
        } else {
            double v;
            if (sscanf(line, "%*s %lf", &v) != 1) {
                continue; //TODOO: use strtod instead
            }
            if (strcmp(key, "thickness_m") == 0) thickness = v;
            else if (strcmp(key, "sample_rate") == 0) sample_rate = (int)v;
            else if (strcmp(key, "num_bins") == 0) num_bins_f = (int)v;
            else if (strcmp(key, "c0") == 0) c0 = v;
            else if (strcmp(key, "num_valid_bins") == 0) t_ctx.diag.num_valid_bins = (int)v;
            else if (strcmp(key, "mean_coherence") == 0) t_ctx.diag.mean_coherence = v;
        }
    }
    fclose(f);

    if (num_bins_f != t_ctx.num_bins) {
        fprintf(stderr, "transmission_load: num_bins mismatch (file=%d, app=%d)\n", num_bins_f, t_ctx.num_bins);
        return -1;
    }
    if (ref_count == 0) {
        fprintf(stderr, "transmission_load: no ref entries found\n");
        return -1;
    }

    t_ctx.thickness_m = thickness;
    t_ctx.sample_rate = sample_rate;
    t_ctx.c0 = c0;

    if (ts[0]) {
        strncpy(t_ctx.diag.timestamp, ts, sizeof(t_ctx.diag.timestamp) - 1);
    }
    t_ctx.has_reference = 1;

    printf("transmission: reference loaded from '%s' (%d entries, valid=%d/%d)\n", path, ref_count, t_ctx.diag.num_valid_bins, t_ctx.num_bins);
    return 0;
}

/*-------------------------------------------------------
 * Compute Tp and TL like a good puter
 * --------------------------------------------------------*/

int transmission_compute(const float *G11, const float *G22, const float *G12_real, const float *G12_imag, int sample_rate) {
    (void)G22;

    if (!t_ctx.initialized || !t_ctx.has_reference) {
        return -1;
    }
    if (!G11 || !G12_real || !G12_imag) {
        return -1;
    }
    if (sample_rate <= 0) {
        return -1;
    }

    t_ctx.sample_rate = sample_rate;
    const int N = t_ctx.num_bins;
    const double fs = sample_rate;
    const double fft = 2.0 * (double)(N - 1);
    const double c0 = t_ctx.c0;
    const double L = t_ctx.thickness_m;

    //Reset diagnostica accumulators
    int valid_count = 0;
    int negative_count = 0;
    double tl_sum = 0.0;
    double tl_min = 1e9;
    double tl_max = -1e9;

    for (int i = 1; i < N; i++) {
        //default = zero (invalid)
        t_ctx.tp_re[i] = 0.0f;
        t_ctx.tp_im[i] = 0.0f;
        t_ctx.tp_mag_db[i] = TRANSMISSION_MAG_FLOOR_DB;
        t_ctx.tl_db[i] = -TRANSMISSION_MAG_FLOOR_DB;
        t_ctx.power_T[i] = 0.0f;
        t_ctx.downstream_level_db[i] = TRANSMISSION_MAG_FLOOR_DB;
        t_ctx.tl_valid[i] = 0;

        //DC
        if (i == 0) {
            continue;
        }

        //downstream level: always puted if G22 > 0 regardless of ref
        const double g22 = G22[i];
        if (g22 > TRANSMISSION_EPS && isfinite(g22)) {
            double lvl = 10.0 * log10(g22);
            if (lvl < (double)TRANSMISSION_MAG_FLOOR_DB) {
                lvl = (double)TRANSMISSION_MAG_FLOOR_DB;
            }
            t_ctx.downstream_level_db[i] = (float)lvl;
        }

        //skip bins with no valid reference
        if (!t_ctx.h12_ref_valid[i]) {
            continue;
        }

        const double g11 = G11[i];
        if (g11 < TRANSMISSION_EPS) {
            continue;
        }

        //Sample gate
        const double s12r = G12_real[i];
        const double s12i = G12_imag[i];
        if (g22 < TRANSMISSION_EPS) {
            continue;
        }
        double coh = (s12r * s12r + s12i * s12i) / (g11 * g22);
        if (coh > 1.0) coh = 1.0;
        if (coh < 0.0) coh = 0.0;
        if (!isfinite(coh)) {
            continue;
        }
        if (coh < (double)TRANSMISSION_COHERENCE_THRESHOLD) {
            continue;
        }


        //H_sample = G12 / G11
        const double h_re = s12r / g11;
        const double h_im =s12i / g11;

        //ratio = H_sample / H_ref
        double r_re, r_im;
        if (!complex_div(h_re, h_im, t_ctx.h12_ref_re[i], t_ctx.h12_ref_im[i], &r_re, &r_im)) {
            continue;
        }

        //Phase factor exp(-j.k0.L)
        const double f = (double)i * fs / fft;
        const double k0 = 2.0 * M_PI2 *f / c0;
        const double a = k0 * L; //phase angle
        const double ca = cos(a);
        const double sa = sin(a);

        //Tp = (r_re + j.r_im) . (ca - j.sa)
        const double tp_re_v = r_re * ca + r_im * sa;
        const double tp_im_v = -r_re * sa + r_im * ca;

        if (!isfinite(tp_re_v) || !isfinite(tp_im_v)) {
            continue;
        }

        t_ctx.tp_re[i] = (float)tp_re_v;
        t_ctx.tp_im[i] = (float)tp_im_v;

        //Magnitudes
        const double mag_sq = tp_re_v * tp_re_v + tp_im_v * tp_im_v;
        const double mag = sqrt(mag_sq);
        t_ctx.power_T[i] = (float)mag_sq;

        if (mag < 1e-10) {
            t_ctx.tp_mag_db[i] = TRANSMISSION_MAG_FLOOR_DB;
            t_ctx.tl_db[i] = -TRANSMISSION_MAG_FLOOR_DB;
        } else {
            const double mag_db = 20.0 * log10(mag);
            t_ctx.tp_mag_db[i] = (float)mag_db;
            t_ctx.tl_db[i] = (float)(-mag_db);

            //Accumulate diagnostica
            const double tl_val = -mag_db;
            tl_sum += tl_val;
            if (tl_val < tl_min) {
                tl_min = tl_val;
            }
            if (tl_val > tl_max) {
                tl_max = tl_val;
            }
            if (tl_val < 0.0) {
                negative_count++;
            }
        }
        t_ctx.tl_valid[i] = 1;
        valid_count++;
    }

    //commit diagnostica
    t_ctx.num_valid_tl = valid_count;
    t_ctx.num_negative_tl = negative_count;
    t_ctx.min_tl = (valid_count > 0) ? tl_min : 0.0;
    t_ctx.max_tl = (valid_count > 0) ? tl_max : 0.0;
    t_ctx.mean_tl = (valid_count > 0) ? tl_sum / (double)valid_count : 0.0;

    return 0;
}

/* Assessors */

const float *transmission_get_Tp_real(void) {
    return t_ctx.tp_re;
}
const float *transmission_get_Tp_imag(void) {
    return t_ctx.tp_im;
}
const float *transmission_get_Tp_mag_db(void) {
    return t_ctx.tp_mag_db;
}
const float *transmission_get_TL_db(void) {
    return t_ctx.tl_db;
}
const float *transmission_get_power_T(void) {
    return t_ctx.power_T;
}
const float *transmission_get_downstream_level_db(void) {
    return t_ctx.downstream_level_db;
}
const int *transmission_get_tl_valid(void) {
    return t_ctx.tl_valid;
}

int transmission_get_num_valid_tl_bins(void) {
    return t_ctx.num_valid_tl;
}
int transmission_get_num_negative_tl_bins(void) {
    return t_ctx.num_negative_tl;
}
double transmission_get_min_tl_db(void) {
    return t_ctx.min_tl;
}
double transmission_get_max_tl_db(void) {
    return t_ctx.max_tl;
}
double transmission_get_mean_tl_db(void) {
    return t_ctx.mean_tl;
}

/* future placeholders, return NULL until implemented */
const float *transmission_get_phase_unwrapped(void) { return NULL; }
const float *transmission_get_group_delay_us(void)  { return NULL; }
const float *transmission_get_inferred_alpha(void)  { return NULL; }



/**
 * Self test
 */