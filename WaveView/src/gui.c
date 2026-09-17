/**
 * gui.c – GTK4 GUI with GtkBuilder and Cairo for 2D plots
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

//channel detection modes
#define CHANNEL_MODE_UNKNOWN 0
#define CHANNEL_MODE_MONO 1
#define CHANNEL_MODE_STEREO 2

//GUI state struct
typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *waveform_area_ch1;
    GtkWidget *waveform_area_ch2;
    GtkWidget *spectrum_area_ch1;
    GtkWidget *spectrum_area_ch2;
    GtkWidget *waveform_col2; //for hide/show
    GtkWidget *spectrum_col2; //for hide/show
    GtkWidget *status_label;
    GtkWidget *device_combo;
    GtkWidget *signal_type_combo;
    GtkWidget *generate_button;
    GtkWidget *cancel_button;
    GtkWidget *start_stop_button;
    GtkBuilder *builder;
    RingBuffer *rb;

    //buffers
    float *interleaved_buffer; //FFT_SIZE * NUM_CHANNELS floats
    float *waveform_buffer_ch1;//FFT_SIZE floats
    float *waveform_buffer_ch2;
    float *spectrum_buffer_ch1; //FFT_SIZE/2 floats
    float *spectrum_buffer_ch2;
    int fft_size;
    int num_freq_bins;
    int waveform_frames;

    //Stream state
    int is_streaming;
    int channel_mode; //detected input mode

    //signal params
    SignalParams signal_params;
} GUIState;

/*-------------------------------------------------
 * channel mode heplers
 * -------------------------------------------------
 */

static const char* channel_mode_name(int mode)
{
    switch (mode) {
        case CHANNEL_MODE_MONO: return "Mono";
        case CHANNEL_MODE_STEREO: return "Stereo";
        default: return "...";
    }
}

/* show/hide second channel's plots in Mono mode (working with pc only) */
static void apply_channel_mode(GUIState *state)
{
    gboolean show_ch2 = (state->channel_mode == CHANNEL_MODE_STEREO);

    if (state->waveform_col2) {
        gtk_widget_set_visible(state->waveform_col2, show_ch2);
    }
    if (state->spectrum_col2) {
        gtk_widget_set_visible(state->spectrum_col2, show_ch2);
    }
}

/* rebuild the status label from current state */
static void refresh_status(GUIState *state)
{
    static const char *type_names[] = {
        "Sine", "Linear Sweep", "Log Sweep",
        "White Noise", "Pink Noise", "Brownian Noise"
    };

    const char *mode = channel_mode_name(state->channel_mode);
    const char *signal_str = (state->signal_params.is_active) ? type_names[state->signal_params.type] : "None";

    char status[512];
    snprintf(status, sizeof(status),
             "Device: %s [%s]  |  Signal: %s  |  Rate: %d Hz  |  FFT: %d  |  %s",
             audio_get_device_name(), mode, signal_str,
             SAMPLE_RATE, state->fft_size,
             state->is_streaming ? "Running" : "Stopped");
    gtk_label_set_text(GTK_LABEL(state->status_label), status);
}


//check if device name contains "pulse" (linux)
static int is_pulse_device(const char *name)
{
    if (name == NULL) return 0;
    return (strstr(name, "pulse") != NULL || strstr(name, "pulse") != NULL);
}

//update dialog labels and visibility based on signal type
static void update_dialog_visibility(GUIState *state)
{
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->signal_type_combo));
    const char *type_names[] = {
        "Sine Wave",
        "Linear Sweep",
        "Log Sweep (Future)",
        "White Noise (Future)",
        "Pink Noise (Future)",
        "Brownian Noise (Future)"
    };

    GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(state->builder, "dialog_signal_type_label"));

    char markup[256];
    snprintf(markup, sizeof(markup), "Signal Type: <span weight=\"bold\">%s</span>", type_names[selected]);

    gtk_label_set_markup(GTK_LABEL(label), markup);//ui file needs to provide a plaintext placeholder

    //show/hide params boxes
    GtkWidget *sine_box = GTK_WIDGET(gtk_builder_get_object(state->builder, "sine_params_box"));
    GtkWidget *sweep_box = GTK_WIDGET(gtk_builder_get_object(state->builder, "sweep_params_box"));
    GtkWidget *future_box = GTK_WIDGET(gtk_builder_get_object(state->builder, "future_params_box"));

    gtk_widget_set_visible(sine_box, (selected == 0));
    gtk_widget_set_visible(sweep_box, (selected == 1));
    gtk_widget_set_visible(future_box, (selected >= 2));
}

static void show_signal_dialog(GUIState *state)
{
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(state->builder, "signal_params_dialog"));

    if (dialog == NULL) {
        g_printerr("ERROR: dialog is NULL in show_signal_dialog()\n");
        return;
    }


    //set transient parent to prevent focus issues
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(state->window));

    update_dialog_visibility(state);

    //ensure buttons are visible
    gtk_widget_set_visible(state->generate_button, TRUE);
    gtk_widget_set_visible(state->cancel_button, TRUE);

    //show dialog
    gtk_widget_set_visible(dialog, TRUE);
    gtk_window_present(GTK_WINDOW(dialog));
}


//dialog management (hide/show instead of destroy)
static gboolean on_dialog_closed(GtkWindow *dialog, gpointer user_data)
{
    (void) user_data;
    gtk_widget_set_visible(GTK_WIDGET(dialog), FALSE);

    return GDK_EVENT_STOP; //prevent default destruction
}

static void on_dialog_generate(const GtkButton *button, const gpointer user_data)
{
    (void) button;
    GUIState *state = user_data;
    GtkBuilder *builder = state->builder;
    guint signal_type = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->signal_type_combo));

    state->signal_params.type = (SignalType) signal_type;
    state->signal_params.is_active = 1;

    switch (signal_type) {
        case SIGNAL_SINE: {
            GtkSpinButton *freq_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sine_freq_spin"));
            GtkScale *amp_scale = GTK_SCALE(gtk_builder_get_object(builder, "sine_amp_scale"));
            state->signal_params.frequency = gtk_spin_button_get_value(freq_spin);
            state->signal_params.amplitude = gtk_range_get_value(GTK_RANGE(amp_scale));
            break;
        }
        case SIGNAL_LINEAR_SWEEP: {
            GtkSpinButton *start_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sweep_start_spin"));
            GtkSpinButton *end_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sweep_end_spin"));
            GtkSpinButton *duration_spin = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "sweep_duration_spin"));
            GtkScale *amp_scale = GTK_SCALE(gtk_builder_get_object(builder, "sweep_amp_scale"));
            state->signal_params.frequency = gtk_spin_button_get_value(start_spin);
            state->signal_params.frequency_end = gtk_spin_button_get_value(end_spin);
            state->signal_params.sweep_duration = gtk_spin_button_get_value(duration_spin);
            state->signal_params.amplitude = gtk_range_get_value(GTK_RANGE(amp_scale));
            break;
        }
        default:
            state->signal_params.is_active = 0;
            break;
    }

    audio_update_signal_params(&state->signal_params);

    refresh_status(state);

    //Hide dialog
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(state->builder, "signal_params_dialog"));
    gtk_widget_set_visible(dialog, FALSE);
}

static void on_dialog_cancel(GtkButton *button, gpointer user_data)
{
    GUIState *state = user_data;
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(state->builder, "signal_params_dialog"));

    gtk_widget_set_visible(dialog, FALSE); //hide
}

//signal handler
static void on_device_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    GUIState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) return;

    const AudioDeviceInfo *info = audio_get_device_info(selected);
    if (info == NULL) return;

    if (is_pulse_device(info->name)) {
        gtk_label_set_text(GTK_LABEL(state->status_label),
                          "ERROR: PulseAudio devices not supported. Select hardware device."); //cause problems (linux)
        return;
    }

    if (audio_select_device(info->index) == 0) {
        //Reset channel mode on device change
        state->channel_mode = CHANNEL_MODE_MONO;
        apply_channel_mode(state);
        refresh_status(state);
    } else {
        gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to switch device");
    }
}

static void on_signal_type_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;
    show_signal_dialog(state);
}

//device combo box
static void populate_device_combo(GUIState *state)
{
    GtkStringList *string_list = gtk_string_list_new(NULL);
    int num_devices = audio_get_device_count();

    for (int i = 0; i < num_devices; i++) {
        const AudioDeviceInfo *info = audio_get_device_info(i);
        if (info == NULL) continue;
        if (is_pulse_device(info->name)) continue; // filter out PulseAudio (linux)
        gtk_string_list_append(string_list, info->name);
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


static void populate_signal_type_combo(GUIState *state)
{
    GtkStringList *string_list = gtk_string_list_new(NULL);
    const char *signal_names[] = {
        "Sine Wave",
        "Linear Sweep",
        "Log Sweep (Future)",
        "White Noise (Future)",
        "Pink Noise (Future)",
        "Brownian Noise (Future)"
    };

    for (size_t i = 0; i < G_N_ELEMENTS(signal_names); i++) {
        gtk_string_list_append(string_list, signal_names[i]);
    }

    GtkWidget *drop_down = gtk_drop_down_new(G_LIST_MODEL(string_list), NULL);
    GtkWidget *parent = gtk_widget_get_parent(state->signal_type_combo);

    if (parent) {
        gtk_box_remove(GTK_BOX(parent), state->signal_type_combo);
        gtk_box_append(GTK_BOX(parent), drop_down);
        gtk_widget_set_hexpand(drop_down, TRUE);
        state->signal_type_combo = drop_down;

        //connect signal to open dialog when selection changes
        g_signal_connect(state->signal_type_combo, "notify::selected", G_CALLBACK(on_signal_type_changed), state);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->signal_type_combo), 0);
    g_object_unref(string_list);

    //TODO: better coonect signal dialog when selection changes
}

/*------------------------------------------------------------
 * Drawing Helpers to be used across channels
 * ----------------------------------------------------------
 */

static void draw_waveform_common(cairo_t *cr, int width, int height, const float *buffer, int frames, double r, double g, double b)
{
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (frames < 2) return;

    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1.5);

    double x_step = (double)width / frames;
    double y_mid = height / 2.0;
    double y_scale = height / 2.0;

    cairo_move_to(cr, 0, y_mid + buffer[0] * y_scale);

    for (int i = 1; i < frames; i++) {
        double x = i * x_step;
        double y = y_mid + buffer[i] * y_scale;
        cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

static void draw_spectrum_common(cairo_t *cr, int width, int height, const float *spectrum, int num_bins, double r, double g, double b)
{
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    if (spectrum == NULL || num_bins < 2) return;

    float max_val = 0.001f;
    for (int i = 0; i < num_bins; i++) {
        if (spectrum[i] > max_val) max_val = spectrum[i];
    }

    cairo_move_to(cr, 0, height);
    for (int i = 0; i < num_bins; i++) {
        double x = (double)i / num_bins * width;
        double y = height - (spectrum[i] / max_val) * (height - 10);
        cairo_line_to(cr, x, y);
    }

    cairo_line_to(cr, width, height);
    cairo_close_path(cr);

    //subtle fill, semi transparent darker version of line colour
    cairo_set_source_rgba(cr, r * 0.4, g * 0.4, b * 0.4, 0.4);
    cairo_fill_preserve(cr);

    //bright stroke
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}

/*--------------------------------------------------------
 * channel-specific drawing callbacks
 * -------------------------------------------------------------
 */

static void on_draw_waveform_ch1(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) area;
    GUIState *state = user_data;

    // Green
    draw_waveform_common(cr, width, height, state->waveform_buffer_ch1, state->waveform_frames, 0.2, 1.0, 0.3);
}

static void on_draw_waveform_ch2(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) area;
    GUIState *state = user_data;

    //Amber
    draw_waveform_common(cr, width, height, state->waveform_buffer_ch2, state->waveform_frames, 1.0, 0.6, 0.1);
}

static void on_draw_spectrum_ch1(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) area;
    GUIState *state = user_data;

    //Cyan
    draw_spectrum_common(cr, width, height, state->spectrum_buffer_ch1, state->num_freq_bins, 0.0, 0.8, 1.0);
}

static void on_draw_spectrum_ch2(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void) area;
    GUIState *state = user_data;

    //Magenta
    draw_spectrum_common(cr, width, height, state->spectrum_buffer_ch2, state->num_freq_bins, 0.9, 0.3, 0.9);
}


//timer callback, updates plots every 50ms
static gboolean update_plots(gpointer user_data)
{
    GUIState *state = user_data;

    if (!state->is_streaming) {
        return G_SOURCE_CONTINUE;
    }

    int in_ch = audio_get_input_channels();
    if (in_ch < 1) in_ch = 1;

    int samples_wanted = state->fft_size * in_ch;
    int samples_read = ring_buffer_read(state->rb, state->interleaved_buffer, samples_wanted);

    if (samples_read <= 0) {
        return G_SOURCE_CONTINUE;
    }

    int frames_read = samples_read / in_ch;
    state->waveform_frames = frames_read;

    //Deinterleave
    for (int i = 0; i < frames_read; i++) {
        state->waveform_buffer_ch1[i] = state->interleaved_buffer[i * in_ch + 0];
        if (in_ch >= 2) {
            state->waveform_buffer_ch2[i] = state->interleaved_buffer[i * in_ch + 1];
        } else {
            state->waveform_buffer_ch2[i] = 0.0f;
        }
    }

    //Detect genuine stereo: ch2 must differ meaningfully from ch1
    if (state->channel_mode != CHANNEL_MODE_STEREO && in_ch >= 2) {
        for (int i = 0; i < frames_read; i++) {
            const float diff = fabsf(state->waveform_buffer_ch2[i] - state->waveform_buffer_ch1[i]); //absolute value of 1st argument
            if (diff > 1e-4f) {
                state->channel_mode = CHANNEL_MODE_STEREO;
                apply_channel_mode(state);
                refresh_status(state);
                break;
            }
        }
    }

    //Compute spectrums
    if (frames_read >= state->fft_size) {
        compute_spectrum(state->waveform_buffer_ch1, state->spectrum_buffer_ch1, state->fft_size);
        compute_spectrum(state->waveform_buffer_ch2, state->spectrum_buffer_ch2, state->fft_size);
    }

    //Redraw all plots (ch2 areas are hidden in mono mode)
    gtk_widget_queue_draw(state->waveform_area_ch1);
    gtk_widget_queue_draw(state->waveform_area_ch2);
    gtk_widget_queue_draw(state->spectrum_area_ch1);
    gtk_widget_queue_draw(state->spectrum_area_ch2);

    return G_SOURCE_CONTINUE;
}

//start/stop button
static void on_start_stop_toggled(GtkToggleButton *button, gpointer user_data)
{
    GUIState *state = user_data;

    if (gtk_toggle_button_get_active(button)) {
        if (audio_start() == 0) {
            state->is_streaming = 1;
            gtk_button_set_label(GTK_BUTTON(button), "Stop");
            refresh_status(state);
        } else {
            gtk_toggle_button_set_active(button, FALSE);
            gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to start stream");
        }
    } else {
        if (audio_stop() == 0) {
            state->is_streaming = 0;
            gtk_button_set_label(GTK_BUTTON(button), "Start");

            //Clear all buffers
            state->waveform_frames = 0;
            memset(state->waveform_buffer_ch1, 0, state->fft_size * sizeof(float));
            memset(state->waveform_buffer_ch2, 0, state->fft_size * sizeof(float));
            memset(state->spectrum_buffer_ch1, 0, state->num_freq_bins * sizeof(float));
            memset(state->spectrum_buffer_ch2, 0, state->num_freq_bins * sizeof(float));

            gtk_widget_queue_draw(state->waveform_area_ch1);
            gtk_widget_queue_draw(state->waveform_area_ch2);
            gtk_widget_queue_draw(state->spectrum_area_ch1);
            gtk_widget_queue_draw(state->spectrum_area_ch2);

            refresh_status(state);

        } else {
            gtk_toggle_button_set_active(button, TRUE);
            gtk_label_set_text(GTK_LABEL(state->status_label), "ERROR: Failed to stop stream");
        }
    }
}

static void on_window_closed(GtkWindow *window, gpointer user_data)
{
    (void) window;
    const GUIState *state = user_data;

    if (state->is_streaming) {
        audio_stop();
    }
    //Use stored app pointer, window's application is already NULL here
    if (state->app != NULL) {
        g_application_release(G_APPLICATION(state->app));
        g_application_quit(G_APPLICATION(state->app));
    }
}



//setup draw funcs and signal handlers
static void setup_callbacks(GUIState *state) {
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->waveform_area_ch1), on_draw_waveform_ch1, state, NULL);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->waveform_area_ch2), on_draw_waveform_ch2, state, NULL);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->spectrum_area_ch1), on_draw_spectrum_ch1, state, NULL);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->spectrum_area_ch2), on_draw_spectrum_ch2, state, NULL);

    g_signal_connect(state->start_stop_button, "toggled", G_CALLBACK(on_start_stop_toggled), state);
    g_signal_connect(state->window, "close-request", G_CALLBACK(on_window_closed), state);
}


//activation callback
static void app_activate(GtkApplication *app, const gpointer user_data)
{
    GUIState *state = user_data;

    g_application_hold(G_APPLICATION(app));

    GtkBuilder *builder = gtk_builder_new_from_file("main_window.ui");

    if (builder == NULL) {
        g_printerr("ERROR: Failed to load UI file.\n");
        return;
    }

    //Main window widgets
    state->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    state->waveform_area_ch1 = GTK_WIDGET(gtk_builder_get_object(builder, "waveform_area_ch1"));
    state->waveform_area_ch2 = GTK_WIDGET(gtk_builder_get_object(builder, "waveform_area_ch2"));
    state->spectrum_area_ch1 = GTK_WIDGET(gtk_builder_get_object(builder, "spectrum_area_ch1"));
    state->spectrum_area_ch2 = GTK_WIDGET(gtk_builder_get_object(builder, "spectrum_area_ch2"));

    //Column containers, for hide/show
    state->waveform_col2 = GTK_WIDGET(gtk_builder_get_object(builder, "waveform_col2"));
    state->spectrum_col2 = GTK_WIDGET(gtk_builder_get_object(builder, "spectrum_col2"));
    state->device_combo = GTK_WIDGET(gtk_builder_get_object(builder, "device_combo"));
    state->signal_type_combo = GTK_WIDGET(gtk_builder_get_object(builder, "signal_type_combo"));
    state->start_stop_button = GTK_WIDGET(gtk_builder_get_object(builder, "start_stop_button"));
    state->status_label = GTK_WIDGET(gtk_builder_get_object(builder, "status_label"));

    //Dialog buttons
    state->generate_button = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_generate_button"));
    state->cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_cancel_button"));
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(builder, "signal_params_dialog"));

    if (!state->window || !state->waveform_area_ch1 || !state->waveform_area_ch2 ||
        !state->spectrum_area_ch1 || !state->spectrum_area_ch2 ||
        !state->waveform_col2 || !state->spectrum_col2 ||
        !state->device_combo || !state->signal_type_combo ||
        !state->start_stop_button || !state->status_label ||
        !state->generate_button || !state->cancel_button || !dialog) {
        fprintf(stderr, "app_activate: Failed to get all widgets.\n");
        g_object_unref(builder);
        return;
    }

    state->builder = builder;

    //Ensure dialog buttons are visible
    gtk_widget_set_visible(state->generate_button, TRUE);
    gtk_widget_set_visible(state->cancel_button, TRUE);

    //Connect dialog signals
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_dialog_closed), state);
    g_signal_connect(state->generate_button, "clicked", G_CALLBACK(on_dialog_generate), state);
    g_signal_connect(state->cancel_button, "clicked", G_CALLBACK(on_dialog_cancel), state);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(state->window));

    //Populate combos
    populate_device_combo(state);
    populate_signal_type_combo(state);

    //Setup drawing + button callbacks
    setup_callbacks(state);

    //Initialize signal parameters
    state->signal_params.type = SIGNAL_SINE;
    state->signal_params.frequency = 440.0f;
    state->signal_params.frequency_end = 1000.0f;
    state->signal_params.amplitude = 0.5f;
    state->signal_params.sweep_duration = 5.0f;
    state->signal_params.is_active = 0;
    state->signal_params.sample_rate = SAMPLE_RATE;

    //Default to Mono mode; will auto-switch to Stereo if detected
    state->channel_mode = CHANNEL_MODE_MONO;
    apply_channel_mode(state);

    refresh_status(state);

    gtk_window_present(GTK_WINDOW(state->window));

    //Auto-start stream
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->start_stop_button), TRUE);

    //Start update timer (50ms)
    g_timeout_add(50, update_plots, state);
}


int gui_run(const int argc, char **argv, RingBuffer *rb)
{
    if (rb == NULL) {
        fprintf(stderr, "gui_run: ring buffer is NULL\n");
        return -1;
    }

    GUIState *state = calloc(1, sizeof(GUIState));
    if (state == NULL) {
        fprintf(stderr, "gui_run: failed to allocate GUI state\n");
        return -1;
    }

    state->rb = rb;
    state->fft_size = FFT_SIZE;
    state->num_freq_bins = FFT_SIZE / 2;
    state->is_streaming = 0;
    state->channel_mode = CHANNEL_MODE_MONO;

    //Allocate all buffers
    state->interleaved_buffer = (float*) malloc(FFT_SIZE * NUM_CHANNELS * sizeof(float));
    state->waveform_buffer_ch1 = (float*) malloc(FFT_SIZE * sizeof(float));
    state->waveform_buffer_ch2 = (float*) malloc(FFT_SIZE * sizeof(float));
    state->spectrum_buffer_ch1 = (float*) malloc((FFT_SIZE / 2) * sizeof(float));
    state->spectrum_buffer_ch2 = (float*) malloc((FFT_SIZE / 2) * sizeof(float));

    if (!state->interleaved_buffer || !state->waveform_buffer_ch1 ||
        !state->waveform_buffer_ch2 || !state->spectrum_buffer_ch1 ||
        !state->spectrum_buffer_ch2) {
        fprintf(stderr, "gui_run: failed to allocate plot buffers\n");

        //make sure to free() all
        free(state->interleaved_buffer);
        free(state->waveform_buffer_ch1);
        free(state->waveform_buffer_ch2);
        free(state->spectrum_buffer_ch1);
        free(state->spectrum_buffer_ch2);
        free(state);
        return -1;
    }

    //Clear all
    memset(state->interleaved_buffer, 0, FFT_SIZE * NUM_CHANNELS * sizeof(float));
    memset(state->waveform_buffer_ch1, 0, FFT_SIZE * sizeof(float));
    memset(state->waveform_buffer_ch2, 0, FFT_SIZE * sizeof(float));
    memset(state->spectrum_buffer_ch1, 0, (FFT_SIZE / 2) * sizeof(float));
    memset(state->spectrum_buffer_ch2, 0, (FFT_SIZE / 2) * sizeof(float));
    state->waveform_frames = 0;

    GtkApplication *app = gtk_application_new("com.waveview.app", G_APPLICATION_DEFAULT_FLAGS);
    state->app = app;
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), state);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    //Free all buffers
    free(state->interleaved_buffer);
    free(state->waveform_buffer_ch1);
    free(state->waveform_buffer_ch2);
    free(state->spectrum_buffer_ch1);
    free(state->spectrum_buffer_ch2);
    free(state);

    return status;
}