/**
 * gui.c – GTK4 GUI implementation
 *
 * Creates a window with two drawing areas:
 *   - Top: Waveform (time domain) in green
 *   - Bottom: Spectrum (frequency domain) in blue
 *
 * A timer updates the display every 50ms by reading from the ring buffer.
 */

#include "gui.h"
#include "config.h"
#include "dsp.h"
#include "audio.h"
#include <gtk/gtk.h>
#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * GUI State Structure
 * ------------------------------------------------------------------------- */

typedef struct {
    GtkWidget *window;
    GtkWidget *waveform_area;
    GtkWidget *spectrum_area;
    GtkWidget *status_label;
    RingBuffer *rb;

    /* Plot buffers */
    float *waveform_buffer;      /* Time-domain samples for waveform plot */
    float *spectrum_buffer;      /* Frequency-domain magnitudes for spectrum plot */
    int fft_size;
    int num_freq_bins;
    int waveform_frames;         /* Number of samples in waveform_buffer */

} GUIState;

/* -------------------------------------------------------------------------
 * Drawing Callbacks (GTK4 style)
 * ------------------------------------------------------------------------- */

/**
 * Draw the waveform (time domain) on the drawing area.
 * GTK4 signature: void callback(GtkDrawingArea *area, cairo_t *cr,
 *                               int width, int height, gpointer user_data)
 */
static void on_draw_waveform(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    /* Clear background – dark grey/black */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (state->waveform_frames < 2) {
        return;
    }

    /* Draw waveform in green */
    cairo_set_source_rgb(cr, 0.0, 0.9, 0.2);
    cairo_set_line_width(cr, 1.5);

    double x_step = (double)width / state->waveform_frames;
    double y_mid = height / 2.0;
    double y_scale = height / 2.0;

    cairo_move_to(cr, 0, y_mid + state->waveform_buffer[0] * y_scale);
    for (int i = 1; i < state->waveform_frames; i++) {
        double x = i * x_step;
        double y = y_mid + state->waveform_buffer[i] * y_scale;
        cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

/**
 * Draw the spectrum (frequency domain) on the drawing area.
 * GTK4 signature: void callback(GtkDrawingArea *area, cairo_t *cr,
 *                               int width, int height, gpointer user_data)
 */
static void on_draw_spectrum(GtkDrawingArea *area, cairo_t *cr,
                             int width, int height, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    /* Clear background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (state->spectrum_buffer == NULL) {
        return;
    }

    /* Find maximum for normalisation */
    float max_val = 0.001f; /* Avoid division by zero */
    for (int i = 0; i < state->num_freq_bins; i++) {
        if (state->spectrum_buffer[i] > max_val) {
            max_val = state->spectrum_buffer[i];
        }
    }

    /* Draw spectrum as filled area (polyline + fill to bottom) */
    cairo_move_to(cr, 0, height);
    for (int i = 0; i < state->num_freq_bins; i++) {
        double x = (double)i / state->num_freq_bins * width;
        double y = height - (state->spectrum_buffer[i] / max_val) * (height - 10);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);

    /* Fill with a subtle gradient effect (semi-transparent fill, solid line) */
    cairo_set_source_rgba(cr, 0.0, 0.5, 0.8, 0.3);
    cairo_fill_preserve(cr);

    cairo_set_source_rgb(cr, 0.0, 0.8, 1.0);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}

/* -------------------------------------------------------------------------
 * Timer Callback – Updates plots every 50ms
 * ------------------------------------------------------------------------- */

static gboolean update_plots(gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    /* Read samples from ring buffer into waveform buffer */
    int frames = ring_buffer_read(state->rb, state->waveform_buffer, state->fft_size);
    if (frames > 0) {
        state->waveform_frames = frames;

        /* If we have enough samples, compute the spectrum */
        if (frames >= state->fft_size) {
            compute_spectrum(state->waveform_buffer, state->spectrum_buffer, state->fft_size);
        }

        /* Redraw both drawing areas */
        gtk_widget_queue_draw(state->waveform_area);
        gtk_widget_queue_draw(state->spectrum_area);
    }

    return G_SOURCE_CONTINUE;
}

/* -------------------------------------------------------------------------
 * Window Close Handler
 * ------------------------------------------------------------------------- */

static void on_window_closed(GtkWindow *window, gpointer user_data)
{
    (void) window;
    (void) user_data;
    g_application_quit(G_APPLICATION(gtk_window_get_application(window)));
}

/* -------------------------------------------------------------------------
 * Application Activation Callback
 * ------------------------------------------------------------------------- */

static void app_activate(GtkApplication *app, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    /* Create main window */
    GtkWidget *window = gtk_application_window_new(app);
    state->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "WaveView – Real-time Microphone Input");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 700);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);

    g_signal_connect(window, "close-request", G_CALLBACK(on_window_closed), NULL);

    /* Main vertical box container */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(vbox, 5);
    gtk_widget_set_margin_end(vbox, 5);
    gtk_widget_set_margin_top(vbox, 5);
    gtk_widget_set_margin_bottom(vbox, 5);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* --- Title label --- */
    GtkWidget *title_label = gtk_label_new("🎤 Live Microphone Input");
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(title_label, FALSE);
    gtk_box_append(GTK_BOX(vbox), title_label);

    /* --- Waveform drawing area (GTK4 style) --- */
    state->waveform_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(state->waveform_area, TRUE);
    gtk_widget_set_vexpand(state->waveform_area, TRUE);
    gtk_widget_set_size_request(state->waveform_area, -1, 250);

    /* GTK4: Use gtk_drawing_area_set_draw_func() instead of signal */
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->waveform_area),
                                   on_draw_waveform, state, NULL);
    gtk_box_append(GTK_BOX(vbox), state->waveform_area);

    /* --- Spectrum drawing area (GTK4 style) --- */
    state->spectrum_area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(state->spectrum_area, TRUE);
    gtk_widget_set_vexpand(state->spectrum_area, TRUE);
    gtk_widget_set_size_request(state->spectrum_area, -1, 250);

    /* GTK4: Use gtk_drawing_area_set_draw_func() instead of signal */
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->spectrum_area),
                                   on_draw_spectrum, state, NULL);
    gtk_box_append(GTK_BOX(vbox), state->spectrum_area);

    /* --- Separator --- */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(vbox), sep);

    /* --- Status bar --- */
    char status_text[512];
    snprintf(status_text, sizeof(status_text),
             "Device: %s  |  Sample Rate: %d Hz  |  FFT Size: %d  |  Status: Running",
             audio_get_device_name(), SAMPLE_RATE, state->fft_size);
    state->status_label = gtk_label_new(status_text);
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(state->status_label, 5);
    gtk_widget_set_margin_end(state->status_label, 5);
    gtk_widget_set_margin_top(state->status_label, 3);
    gtk_widget_set_margin_bottom(state->status_label, 3);
    gtk_box_append(GTK_BOX(vbox), state->status_label);

    /* Show the window */
    gtk_window_present(GTK_WINDOW(window));

    /* Start audio stream */
    if (audio_start() != 0) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to start audio stream!");
    }

    /* Start update timer (50ms = 20 Hz refresh) */
    g_timeout_add(50, update_plots, state);
}

/* -------------------------------------------------------------------------
 * Public API – gui_run()
 * ------------------------------------------------------------------------- */

int gui_run(int argc, char **argv, RingBuffer *rb)
{
    if (rb == NULL) {
        fprintf(stderr, "gui_run: ring buffer is NULL\n");
        return -1;
    }

    /* Allocate and initialise GUI state */
    GUIState *state = (GUIState*) calloc(1, sizeof(GUIState));
    if (state == NULL) {
        fprintf(stderr, "gui_run: failed to allocate GUI state\n");
        return -1;
    }

    state->rb = rb;
    state->fft_size = FFT_SIZE;
    state->num_freq_bins = FFT_SIZE / 2;

    /* Allocate plot buffers */
    state->waveform_buffer = (float*) malloc(FFT_SIZE * sizeof(float));
    if (state->waveform_buffer == NULL) {
        fprintf(stderr, "gui_run: failed to allocate waveform buffer\n");
        free(state);
        return -1;
    }

    state->spectrum_buffer = (float*) malloc((FFT_SIZE / 2) * sizeof(float));
    if (state->spectrum_buffer == NULL) {
        fprintf(stderr, "gui_run: failed to allocate spectrum buffer\n");
        free(state->waveform_buffer);
        free(state);
        return -1;
    }

    /* Clear buffers */
    memset(state->waveform_buffer, 0, FFT_SIZE * sizeof(float));
    memset(state->spectrum_buffer, 0, (FFT_SIZE / 2) * sizeof(float));
    state->waveform_frames = 0;

    /* Create GTK application */
    GtkApplication *app = gtk_application_new("com.waveview.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), state);

    /* Run the application (blocks until window closes) */
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    /* Clean up GTK resources */
    g_object_unref(app);

    /* Stop audio if still running */
    audio_stop();

    /* Free GUI state buffers */
    free(state->waveform_buffer);
    free(state->spectrum_buffer);
    free(state);

    return status;
}