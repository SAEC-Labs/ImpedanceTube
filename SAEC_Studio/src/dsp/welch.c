//
// Created by torq on 09/8/26.
//

/**
 * Welche's spectral averaging
 *
 * {Input block->sliding buffer->Hann window->FFT->accumul S11,S22,S12->divide by K.W }
 */

#include "welch.h"
#include "external/kissfft/kiss_fftr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI2
#define M_PI2 3.14159265358979323846
#endif

typedef struct {
    //FFT infra
    kiss_fftr_cfg fft_cfg;
    float *window; //hann window, length N
    double window_power; // W = Σ w^2 [n]

    //working buffer, sliding
    float *work_ch1; //lenth N
    float *work_ch2; //length N
    int samples_filled; //0..N

    //FFT output buffers, length N/2 + 1
    kiss_fft_cpx *spectrum_ch1;
    kiss_fft_cpx *spectrum_ch2;

    //accumulators, running over K segments, length NUM_BINS
    double *acc_S11;
    double *acc_S22;
    double *acc_S12_real;
    double *acc_S12_imag;

    //averaged results, one-sided spectra, after finalize
    float *G11;
    float *G22;
    float *G12_real;
    float *G12_imag;

    //state
    int segments_done;
    MeasureStatus status;
    int initialized;
} MeasureContext;

static MeasureContext m_ctx = {0};


static void measure_process_segment(void) {
    //applt precomputed hann window
    for (int i = 0; i < MEASURE_FFT_SIZE; i++) {
        m_ctx.work_ch1[i] *= m_ctx.window[i];
        m_ctx.work_ch2[i] *= m_ctx.window[i];
    }

    //forward real FFT on both channels
    kiss_fftr(m_ctx.fft_cfg, m_ctx.work_ch1, m_ctx.spectrum_ch1);
    kiss_fftr(m_ctx.fft_cfg, m_ctx.work_ch2, m_ctx.spectrum_ch2);

    //accumulate auto and cross-spectra
    for (int i = 0; i < MEASURE_NUM_BINS; i++) {
        const double xr = m_ctx.spectrum_ch1[i].r;
        const double xi = m_ctx.spectrum_ch1[i].i;
        const double yr = m_ctx.spectrum_ch2[i].r;
        const double yi = m_ctx.spectrum_ch2[i].i;

        //auto-spectra, |X|^2 , |Y|^2
        m_ctx.acc_S11[i] += xr * xr + xi * xi;
        m_ctx.acc_S22[i] += yr * yr + yi * yi;

        /* Cross-spectrum: X* · Y
         *   X* = xr − j·xi
         *   X* · Y = (xr·yr + xi·yi) + j·(xr·yi − xi·yr)
         */
        m_ctx.acc_S12_real[i] += xr * yr + xi * yi;
        m_ctx.acc_S12_imag[i] += xr * yi - xi * yr;
    }

    m_ctx.segments_done++;
}

static void measure_finalize(void) {
    //welch normalization, dividing by (K.W)
    const double norm = 1.0 / ((double)MEASURE_NUM_SEGMENTS * m_ctx.window_power);

    for (int i = 0; i < MEASURE_NUM_BINS; i++) {
        m_ctx.G11[i] = (float)(m_ctx.acc_S11[i] * norm);
        m_ctx.G22[i] = (float)(m_ctx.acc_S22[i] * norm);
        m_ctx.G12_real[i] = (float)(m_ctx.acc_S12_real[i] * norm);
        m_ctx.G12_imag[i] = (float)(m_ctx.acc_S12_imag[i] * norm);
    }

    //IMPORTANT!! Convert two-sided to one-sided, double bins 1..N/2-1
    for (int i = 1; i < MEASURE_NUM_BINS - 1; i++) {
        m_ctx.G11[i] *= 2.0f;
        m_ctx.G22[i] *= 2.0f;
        m_ctx.G12_real[i] *= 2.0f;
        m_ctx.G12_imag[i] *= 2.0f;
    }

    m_ctx.status = MEASURE_STATUS_COMPLETE;
}

int measure_init(void) {
    if (m_ctx.initialized) return 0;

    //FFT config
    m_ctx.fft_cfg = kiss_fftr_alloc(MEASURE_FFT_SIZE, 0, NULL, NULL);

    if (!m_ctx.fft_cfg) goto fail;

    //Hann window and its power
    m_ctx.window = (float*)malloc(MEASURE_FFT_SIZE * sizeof(float));

    if (!m_ctx.window) goto fail;

    m_ctx.window_power = 0.0;
    for (int i = 0; i < MEASURE_FFT_SIZE; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI2 * i / (MEASURE_FFT_SIZE - 1)));
        m_ctx.window[i] = w;
        m_ctx.window_power += (double)w * (double)w;
    }

    //sliding buffers
    m_ctx.work_ch1 = (float*)calloc(MEASURE_FFT_SIZE, sizeof(float));
    m_ctx.work_ch2 = (float*)calloc(MEASURE_FFT_SIZE, sizeof(float));

    if (!m_ctx.work_ch1 || !m_ctx.work_ch2) goto fail;

    //FFT output buffers
    m_ctx.spectrum_ch1 = (kiss_fft_cpx*)calloc(MEASURE_NUM_BINS, sizeof(kiss_fft_cpx));
    m_ctx.spectrum_ch2 = (kiss_fft_cpx*)calloc(MEASURE_NUM_BINS, sizeof(kiss_fft_cpx));

    if (!m_ctx.spectrum_ch1 || !m_ctx.spectrum_ch2) goto fail;

    //our accumulators now !!!
    m_ctx.acc_S11 = (double*)calloc(MEASURE_NUM_BINS, sizeof(double));
    m_ctx.acc_S22 = (double*)calloc(MEASURE_NUM_BINS, sizeof(double));
    m_ctx.acc_S12_real = (double*)calloc(MEASURE_NUM_BINS, sizeof(double));
    m_ctx.acc_S12_imag = (double*)calloc(MEASURE_NUM_BINS, sizeof(double));

    if (!m_ctx.acc_S11 || !m_ctx.acc_S22 || !m_ctx.acc_S12_real || !m_ctx.acc_S12_imag) goto fail;

    //Averaged results
    m_ctx.G11 = (float*)calloc(MEASURE_NUM_BINS, sizeof(float));
    m_ctx.G22 = (float*)calloc(MEASURE_NUM_BINS, sizeof(float));
    m_ctx.G12_real = (float*)calloc(MEASURE_NUM_BINS, sizeof(float));
    m_ctx.G12_imag = (float*)calloc(MEASURE_NUM_BINS, sizeof(float));

    if (!m_ctx.G11 || !m_ctx.G22 || !m_ctx.G12_real || !m_ctx.G12_imag) goto fail;

    m_ctx.samples_filled = 0;
    m_ctx.segments_done = 0;
    m_ctx.status = MEASURE_STATUS_IDLE;
    m_ctx.initialized = 1;

    printf("measure: initialized (FFT=%d, shift=%d, segments=%d, Hann window)\n", MEASURE_FFT_SIZE, MEASURE_SHIFT, MEASURE_NUM_SEGMENTS);
    return 0;


    fail:
    fprintf(stderr, "measure_init: failed to allocate memory for FFT\n");
    measure_free();
    return -1;
}

void measure_free(void) {
    if (m_ctx.fft_cfg) {
        kiss_fftr_free(m_ctx.fft_cfg);
    }
    free(m_ctx.window);
    free(m_ctx.work_ch1);
    free(m_ctx.work_ch2);
    free(m_ctx.spectrum_ch1);
    free(m_ctx.spectrum_ch2);
    free(m_ctx.acc_S11);
    free(m_ctx.acc_S22);
    free(m_ctx.acc_S12_real);
    free(m_ctx.acc_S12_imag);
    free(m_ctx.G11);
    free(m_ctx.G22);
    free(m_ctx.G12_real);
    free(m_ctx.G12_imag);

    memset(&m_ctx, 0, sizeof(m_ctx));
}

void measure_reset(void) {
    if (!m_ctx.initialized) {
        return;
    }

    memset(m_ctx.work_ch1, 0, MEASURE_FFT_SIZE * sizeof(float));
    memset(m_ctx.work_ch2, 0, MEASURE_FFT_SIZE * sizeof(float));

    memset(m_ctx.acc_S11,      0, MEASURE_NUM_BINS * sizeof(double));
    memset(m_ctx.acc_S22,      0, MEASURE_NUM_BINS * sizeof(double));
    memset(m_ctx.acc_S12_real, 0, MEASURE_NUM_BINS * sizeof(double));
    memset(m_ctx.acc_S12_imag, 0, MEASURE_NUM_BINS * sizeof(double));

    memset(m_ctx.G11,      0, MEASURE_NUM_BINS * sizeof(float));
    memset(m_ctx.G22,      0, MEASURE_NUM_BINS * sizeof(float));
    memset(m_ctx.G12_real, 0, MEASURE_NUM_BINS * sizeof(float));
    memset(m_ctx.G12_imag, 0, MEASURE_NUM_BINS * sizeof(float));

    m_ctx.samples_filled = 0;
    m_ctx.segments_done = 0;
    m_ctx.status = MEASURE_STATUS_IDLE;

    printf("measure: reset\n");
}

int measure_feed_block(const float *ch1, const float *ch2, int num_samples) {
    if (!m_ctx.initialized || !ch1 || !ch2 || num_samples <= 0) {
        return -1;
    }
    if (m_ctx.status == MEASURE_STATUS_COMPLETE) {
        return 1;
    }

    if (m_ctx.status == MEASURE_STATUS_IDLE) {
        m_ctx.status = MEASURE_STATUS_ACCUMULATING;
    }

    int pos = 0;

    while (pos < num_samples) {
        const int space = MEASURE_FFT_SIZE - m_ctx.samples_filled;
        int to_copy = (num_samples - pos < space) ? (num_samples - pos) : space;

        memcpy(m_ctx.work_ch1 + m_ctx.samples_filled, ch1 + pos, to_copy * sizeof(float));
        memcpy(m_ctx.work_ch2 + m_ctx.samples_filled, ch2 + pos, to_copy * sizeof(float));

        m_ctx.samples_filled += to_copy;
        pos += to_copy;

        if (m_ctx.samples_filled == MEASURE_FFT_SIZE) {
            measure_process_segment();

            if (m_ctx.segments_done >= MEASURE_NUM_SEGMENTS) {
                measure_finalize();
                return 1; //complete, hopefully (~_~)
            }

            //slider buffer to keep trailing (N - shift) samples
            const int kept = MEASURE_FFT_SIZE - MEASURE_SHIFT;

            memmove(m_ctx.work_ch1, m_ctx.work_ch2 + MEASURE_SHIFT, kept * sizeof(float));
            memmove(m_ctx.work_ch2, m_ctx.work_ch2 + MEASURE_SHIFT, kept * sizeof(float));

            m_ctx.samples_filled = kept;
        }
    }

    return 0;
}

MeasureStatus measure_get_status(void) {
    return m_ctx.status;
}

int measure_get_progress(void) {
    if (m_ctx.status == MEASURE_STATUS_COMPLETE) {
        return 100;
    }

    if (MEASURE_NUM_SEGMENTS <= 0) return 0;

    return (m_ctx.segments_done * 100) / MEASURE_NUM_SEGMENTS;
}

int measure_get_num_bins(void) {
    return MEASURE_NUM_BINS;
}

const float *measure_get_G11(void) { return m_ctx.G11; }
const float *measure_get_G22(void) { return m_ctx.G22; }
const float *measure_get_G12_real(void) { return m_ctx.G12_real; }
const float *measure_get_G12_imag(void) { return m_ctx.G12_imag; }

float measure_bin_to_hz(int bin, float sample_rate) {
    return (float)bin * sample_rate / (float)MEASURE_FFT_SIZE;
}

/* -------------------------------------------------------------------
 * Self Test
 * ------------------------------------------------------------------*/










