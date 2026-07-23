/**
 * gui.c – GTK4 GUI with GtkBuilder
 *
 * Uses a .ui file to define the window layout. Widgets are accessed by name
 * and connected to signal handlers.
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

//GUI state struct
typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *waveform_area;
    GtkWidget *spectrum_area;
    GtkWidget *status_label;
    GtkWidget *device_combo;
    GtkWidget *start_stop_button;
    GtkBuilder *builder;
    RingBuffer *rb;

    //Plot buffers
    float *waveform_buffer;
    float *spectrum_buffer;
    int fft_size;
    int num_freq_bins;
    int waveform_frames;

    //Stream stat
    int is_streaming;
} GUIState;

//drawing callbacks, gtk4 styles
static void on_draw_waveform(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (state->waveform_frames < 2) return;

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

static void on_draw_spectrum(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (state->spectrum_buffer == NULL) return;

    float max_val = 0.001f;
    for (int i = 0; i < state->num_freq_bins; i++) {
        if (state->spectrum_buffer[i] > max_val) max_val = state->spectrum_buffer[i];
    }

    cairo_move_to(cr, 0, height);
    for (int i = 0; i < state->num_freq_bins; i++) {
        double x = (double)i / state->num_freq_bins * width;
        double y = height - (state->spectrum_buffer[i] / max_val) * (height - 10);
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, width, height);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.0, 0.5, 0.8, 0.3);
    cairo_fill_preserve(cr);

    cairo_set_source_rgb(cr, 0.0, 0.8, 1.0);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}

//timer callback, updates plots every 50ms
static gboolean update_plots(gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    if (!state->is_streaming) {
        return G_SOURCE_CONTINUE;
    }

    int frames = ring_buffer_read(state->rb, state->waveform_buffer, state->fft_size);
    if (frames > 0) {
        state->waveform_frames = frames;
        if (frames >= state->fft_size) {
            compute_spectrum(state->waveform_buffer, state->spectrum_buffer, state->fft_size);
        }
        gtk_widget_queue_draw(state->waveform_area);
        gtk_widget_queue_draw(state->spectrum_area);
    }

    return G_SOURCE_CONTINUE;
}

//signal handler
static void on_device_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) return;

    const AudioDeviceInfo *info = audio_get_device_info(selected);
    if (info == NULL) return;

    if (audio_select_device(info->index) == 0) {
        char status[256];
        snprintf(status, sizeof(status),
                 "Device: %s  |  Sample Rate: %d Hz  |  FFT Size: %d  |  %s",
                 audio_get_device_name(), SAMPLE_RATE, state->fft_size,
                 state->is_streaming ? "Running" : "Stopped");
        gtk_label_set_text(GTK_LABEL(state->status_label), status);
    } else {
        gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to switch device");
    }
}

//start/stop button
static void on_start_stop_toggled(GtkToggleButton *button, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    if (gtk_toggle_button_get_active(button)) {
        //Start
        if (audio_start() == 0) {
            state->is_streaming = 1;
            gtk_button_set_label(GTK_BUTTON(button), "Stop");
            char status[256];
            snprintf(status, sizeof(status),
                     "Device: %s  |  Rate: %d Hz  |  FFT: %d  |  Running",
                     audio_get_device_name(), SAMPLE_RATE, state->fft_size);
            gtk_label_set_text(GTK_LABEL(state->status_label), status);
        } else {
            gtk_toggle_button_set_active(button, FALSE);
            gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to start stream");
        }
    } else {
        //Stop
        if (audio_stop() == 0) {
            state->is_streaming = 0;
            gtk_button_set_label(GTK_BUTTON(button), "Start");
            char status[256];
            snprintf(status, sizeof(status),
                     "Device: %s  |  Rate: %d Hz  |  FFT: %d  |  Stopped",
                     audio_get_device_name(), SAMPLE_RATE, state->fft_size);
            gtk_label_set_text(GTK_LABEL(state->status_label), status);

            //Clear plots
            state->waveform_frames = 0;
            memset(state->waveform_buffer, 0, state->fft_size * sizeof(float));
            memset(state->spectrum_buffer, 0, (state->fft_size / 2) * sizeof(float));
            gtk_widget_queue_draw(state->waveform_area);
            gtk_widget_queue_draw(state->spectrum_area);
        } else {
            gtk_toggle_button_set_active(button, TRUE);
            gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to stop stream");
        }
    }
}

static void on_window_closed(GtkWindow *window, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;

    g_printerr("DEBUG: on_window_closed() called\n");

    if (state->is_streaming) {
        g_printerr("DEBUG: Stopping audio stream from window close\n");
        audio_stop();
    }

    //release the hold when the window is closed, held in app_activate()
    g_application_release(G_APPLICATION(gtk_window_get_application(window)));

    g_printerr("DEBUG: Quitting application from window close\n");
    g_application_quit(G_APPLICATION(gtk_window_get_application(window)));
}

//device combo box
static void populate_device_combo(GUIState *state)
{
    GtkStringList *string_list = gtk_string_list_new(NULL);
    int num_devices = audio_get_device_count();

    for (int i = 0; i < num_devices; i++) {
        const AudioDeviceInfo *info = audio_get_device_info(i);
        if (info) {
            gtk_string_list_append(string_list, info->name);
        }
    }

    GtkWidget *drop_down = gtk_drop_down_new(G_LIST_MODEL(string_list), NULL);
    //Replace the old combo box with the new drop-down
    //Get parent container and replace child
    GtkWidget *parent = gtk_widget_get_parent(state->device_combo);
    if (parent) {
        gtk_box_remove(GTK_BOX(parent), state->device_combo);
        gtk_box_append(GTK_BOX(parent), drop_down);
        gtk_widget_set_hexpand(drop_down, TRUE);
        state->device_combo = drop_down;
        g_signal_connect(state->device_combo, "notify::selected",
                         G_CALLBACK(on_device_changed), state);
    }

    //Select first item
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->device_combo), 0);
    g_object_unref(string_list);
}

//setup draw funcs and signal handlers
static void setup_callbacks(GUIState *state)
{
    //Drawing areas
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->waveform_area), on_draw_waveform, state, NULL);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->spectrum_area), on_draw_spectrum, state, NULL);

    //Device combo
    //g_signal_connect(state->device_combo, "changed", G_CALLBACK(on_device_changed), state);

    //Start/Stop button
    g_signal_connect(state->start_stop_button, "toggled", G_CALLBACK(on_start_stop_toggled), state);

    //Window close
    g_signal_connect(state->window, "close-request", G_CALLBACK(on_window_closed), state);
}

//activation callback
static void app_activate(GtkApplication *app, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;
    GtkBuilder *builder;
    GError *error = NULL;

    g_printerr("DEBUG: app_activate() called\n");

    //Hold the application to prevent premature exit. released by on_window_closed()
    g_application_hold(G_APPLICATION(app));

    //Load UI from file
    builder = gtk_builder_new_from_file("main_window.ui");
    if (builder == NULL) {
        //fprintf(stderr, "app_activate: Failed to load UI file.\n");
        g_printerr("DEBUG: Failed to load UI file: ui/main_window.ui\n");
        return;
    }
    g_printerr("DEBUG: UI file loaded successfully\n");

    //Get widgets by name
    state->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    state->waveform_area = GTK_WIDGET(gtk_builder_get_object(builder, "waveform_area"));
    state->spectrum_area = GTK_WIDGET(gtk_builder_get_object(builder, "spectrum_area"));
    state->device_combo = GTK_WIDGET(gtk_builder_get_object(builder, "device_combo"));
    state->start_stop_button = GTK_WIDGET(gtk_builder_get_object(builder, "start_stop_button"));
    state->status_label = GTK_WIDGET(gtk_builder_get_object(builder, "status_label"));

    g_printerr("DEBUG: Widgets retrieved: window=%p, waveform=%p, spectrum=%p, combo=%p, button=%p, status=%p\n",
              state->window, state->waveform_area, state->spectrum_area,
              state->device_combo, state->start_stop_button, state->status_label);


    if (!state->window || !state->waveform_area || !state->spectrum_area ||
        !state->device_combo || !state->start_stop_button || !state->status_label) {
        fprintf(stderr, "app_activate: Failed to get all widgets from UI file.\n");
        g_object_unref(builder);
        return;
    }

    state->builder = builder;
    g_printerr("DEBUG: All widgets OK\n");

    //Populate device list
    populate_device_combo(state);
    g_printerr("DEBUG: Device combo populated");

    //Set up signal handlers and draw functions
    setup_callbacks(state);
    g_printerr("DEBUG: Callbacks set up\n");

    //Set initial status
    char status[256];
    snprintf(status, sizeof(status),
             "Device: %s  |  Rate: %d Hz  |  FFT: %d  |  Stopped",
             audio_get_device_name(), SAMPLE_RATE, state->fft_size);
    gtk_label_set_text(GTK_LABEL(state->status_label), status);

    //Show the window
    gtk_window_present(GTK_WINDOW(state->window));
    g_printerr("DEBUG: Window presented\n");

    //check if window is visible
    gboolean is_visible = gtk_widget_get_visible(state->window);
    g_printerr("DEBUG: Window visible? %d\n", is_visible);

    //Auto-start (toggle button will fire event)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->start_stop_button), TRUE);
    g_printerr("DEBUG: Auto-start toggled\n");

    //Start timer
    g_timeout_add(50, update_plots, state);
    g_printerr("DEBUG: Timer started\n");

    g_printerr("DEBUG: app_activate() completed successfully\n");
}


int gui_run(int argc, char **argv, RingBuffer *rb)
{
    if (rb == NULL) {
        fprintf(stderr, "gui_run: ring buffer is NULL\n");
        return -1;
    }

    GUIState *state = (GUIState*) calloc(1, sizeof(GUIState));
    if (state == NULL) {
        fprintf(stderr, "gui_run: failed to allocate GUI state\n");
        return -1;
    }

    state->rb = rb;
    state->fft_size = FFT_SIZE;
    state->num_freq_bins = FFT_SIZE / 2;
    state->is_streaming = 0;

    state->waveform_buffer = (float*) malloc(FFT_SIZE * sizeof(float));
    state->spectrum_buffer = (float*) malloc((FFT_SIZE / 2) * sizeof(float));
    if (state->waveform_buffer == NULL || state->spectrum_buffer == NULL) {
        fprintf(stderr, "gui_run: failed to allocate plot buffers\n");
        free(state->waveform_buffer);
        free(state->spectrum_buffer);
        free(state);
        return -1;
    }

    memset(state->waveform_buffer, 0, FFT_SIZE * sizeof(float));
    memset(state->spectrum_buffer, 0, (FFT_SIZE / 2) * sizeof(float));
    state->waveform_frames = 0;

    GtkApplication *app = gtk_application_new("com.waveview.app", G_APPLICATION_DEFAULT_FLAGS);
    state->app = app;
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), state);

    g_printerr("DEBUG: About to call g_application_run()\n");
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_printerr("DEBUG: g_application_run() returned with status: %d\n", status);

    g_object_unref(app);

    free(state->waveform_buffer);
    free(state->spectrum_buffer);
    free(state);

    return status;
}