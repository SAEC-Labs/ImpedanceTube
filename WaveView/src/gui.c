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
//#include <math.h>

//GUI state struct
typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *waveform_area;
    GtkWidget *spectrum_area;
    GtkWidget *status_label;
    GtkWidget *device_combo;
    GtkWidget *signal_type_combo;
    GtkWidget *generate_button;
    GtkWidget *cancel_button;
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

    //signal params
    SignalParams signal_params;
} GUIState;

//check if device name contains "pulse"
static int is_pulse_device(const char *name) {
    if (name == NULL) return 0;
    return (strstr(name, "pulse") != NULL || strstr(name, "pulse") != NULL);
}

//update dialog labels and visibility based on signal type
static void update_dialog_visibility(GUIState *state) {
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

static void show_signal_dialog(GUIState *state) {
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
static gboolean on_dialog_closed(GtkWindow *dialog, gpointer user_data) {
    (void) user_data;
    gtk_widget_set_visible(GTK_WIDGET(dialog), FALSE);
    //gtk_widget_hide(GTK_WIDGET(dialog));
    return GDK_EVENT_STOP; //prevent default destruction
}

static void on_dialog_generate(GtkButton *button, gpointer user_data) {
    GUIState *state = (GUIState*) user_data;
    GtkBuilder *builder = state->builder;
    guint signal_type = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->signal_type_combo));

    //update SignalParams based on signal type
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
        } default:
            state->signal_params.is_active = 0;
            break;
    }

    //apply to audio subsystem
    audio_update_signal_params(&state->signal_params);

    //update status
    const char *type_names[] = {
        "Sine", "Linear Sweep", "Log Sweep", "White Noise", "Pink Noise", "Brownian Noise"
    };
    char status[256];
    snprintf(status, sizeof(status), "Device: %s | Signal: %s | Rate: %d Hz | %s", audio_get_device_name(), type_names[signal_type], SAMPLE_RATE, state->is_streaming ? "Running" : "Stopped");

    gtk_label_set_text(GTK_LABEL(state->status_label), status);

    //hide dialog instead of destroying
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(builder, "signal_params_dialog"));
    //gtk_widget_hide(dialog);
    //gtk_window_destroy(GTK_WINDOW(dialog));
    gtk_widget_set_visible(dialog, FALSE);
}

static void on_dialog_cancel(GtkButton *button, gpointer user_data) {
    GUIState *state = (GUIState*) user_data;
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(state->builder, "signal_params_dialog"));

    //gtk_window_destroy(GTK_WINDOW(dialog));
    //gtk_widget_hide(dialog);
    gtk_widget_set_visible(dialog, FALSE);
}

//signal handler
static void on_device_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    GUIState *state = (GUIState*) user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));

    if (selected == GTK_INVALID_LIST_POSITION) return;

    const AudioDeviceInfo *info = audio_get_device_info(selected);
    if (info == NULL) return;

    if (is_pulse_device(info->name)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), "Error: PulseAudio devices not supported. Select hardware device.");
        return;
    }

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
        if (is_pulse_device(info->name)) continue; // filter out PulseAudio
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


static void populate_signal_type_combo(GUIState *state) {
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
}


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

/*static void on_generate_button_clicked(GtkButton *button, gpointer user_data) {
    GUIState *state = (GUIState*) user_data;
    show_signal_dialog(state);
} */


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

    //generate button
    //g_signal_connect(state->generate_button, "clicked", G_CALLBACK(on_generate_button_clicked), state);

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
    state->signal_type_combo = GTK_WIDGET(gtk_builder_get_object(builder, "signal_type_combo"));
    state->start_stop_button = GTK_WIDGET(gtk_builder_get_object(builder, "start_stop_button"));
    state->status_label = GTK_WIDGET(gtk_builder_get_object(builder, "status_label"));

    //get dialog buttons stored in GUIState
    state->generate_button = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_generate_button"));
    state->cancel_button = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_cancel_button"));

    //dialog
    GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(builder, "signal_params_dialog"));

    g_printerr("DEBUG: Widgets retrieved: window=%p, waveform=%p, spectrum=%p, combo=%p, button=%p, status=%p\n",
              state->window, state->waveform_area, state->spectrum_area,
              state->device_combo, state->signal_type_combo, state->start_stop_button, state->status_label, state->generate_button, state->cancel_button, dialog);


    if (!state->window || !state->waveform_area || !state->spectrum_area ||
        !state->device_combo || !state->signal_type_combo || !state->start_stop_button || !state->status_label ||
        !state->generate_button || !state->cancel_button || !dialog) {
        fprintf(stderr, "app_activate: Failed to get all widgets from UI file.\n");
        g_object_unref(builder);
        return;
    }

    state->builder = builder;
    g_printerr("DEBUG: All widgets OK\n");

    //ensure dialog buttons are visible
    gtk_widget_set_visible(state->generate_button, TRUE);
    gtk_widget_set_visible(state->cancel_button, TRUE);

    //connect dialog signals
    //GtkWidget *dialog = GTK_WIDGET(gtk_builder_get_object(builder, "signal_params_dialog"));
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_dialog_closed), state);

    g_signal_connect(state->generate_button, "clicked", G_CALLBACK(on_dialog_generate), state);
    g_signal_connect(state->cancel_button, "clicked", G_CALLBACK(on_dialog_cancel), state);

    //set transient parent for dialog
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(state->window));

    //Populate combos
    populate_device_combo(state);
    populate_signal_type_combo(state);
    g_printerr("DEBUG: Device combo populated");

    //Set up signal handlers and draw functions
    setup_callbacks(state);
    g_printerr("DEBUG: Callbacks set up\n");

    //init signal params
    state->signal_params.type = SIGNAL_SINE;
    state->signal_params.frequency = 440.0f;
    state->signal_params.frequency_end = 1000.0f;
    state->signal_params.amplitude = 0.5f;
    state->signal_params.sweep_duration = 5.0f;
    state->signal_params.is_active = 0;
    state->signal_params.sample_rate = SAMPLE_RATE;

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