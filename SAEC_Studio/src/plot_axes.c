//
// Created by torq on 10/31/26.
//

#include "plot_axes.h"
#include <math.h>
#include <stdio.h>
#include <string.h>


//style constants
#define COLOR_BG         0.08, 0.08, 0.10
#define COLOR_GRID       0.25, 0.25, 0.30
#define COLOR_AXIS       0.55, 0.55, 0.60
#define COLOR_TEXT       0.85, 0.85, 0.90
#define COLOR_TEXT_DIM   0.65, 0.65, 0.70

#define FONT_TICK        9.0
#define FONT_AXIS_TITLE  10.0
#define TICK_LEN         4.0
#define TICK_GAP         3.0
#define TITLE_GAP        8.0


static void format_tick(char *buf, size_t buflen, double v, int log_scale) {
    if (log_scale && v >= 1000.0 && v < 1.0e6 && fabs(v - round(v)) < 0.01) {
        snprintf(buf, buflen, "%.0fk", v / 1000.0);
    } else if (fabs(v - round(v)) < 0.0005) {
        snprintf(buf, buflen, "%.f", v);
    }  else if (fabs(v) < 0.01 && v != 0.0) {
        snprintf(buf, buflen, "%.1e", v);
    } else if (fabs(v) < 1.0) {
        snprintf(buf, buflen, "%.2f", v);
    } else if (fabs(v) < 10.0) {
        snprintf(buf, buflen, "%.1f", v);
    } else {
        snprintf(buf, buflen, "%.0f", v);
    }
}

//collect tick positions for an axis into an array, returns countt
static int collect_ticks(const AxisConfig *axis, double *ticks, int max_ticks) {
    int n = 0;
    if (max_ticks < 1) {
        return 0;
    }

    if (axis->log_scale) {
        if (axis->min_value <= 0.0 || axis->max_value <= 0.0 || axis->max_value <= axis->min_value) {
            return 0;
        }

        const double log_min = log10(axis->min_value);
        const double log_max = log10(axis->max_value);
        const int d_min = (int)floor(log_min);
        const int d_max = (int)floor(log_max);
        static const int mult[3] = {1, 2, 5};

        for (int d = d_min; d <= d_max && n < max_ticks; d++) {
            for (int m = 0; m < 3 && n < max_ticks; m++) {
                const double v = (double)mult[m] * pow(10.0, d);
                if (v < axis->min_value) {
                    continue;
                }
                if (v > axis->max_value) {
                    continue;
                }
                ticks[n++] = v;
            }
        }
    } else {
        int intervals = axis->num_ticks - 1;
        if (intervals < 1) {
            intervals = 4;
        }
        const double span = axis->max_value - axis->min_value;
        if (fabs(span) < 1e-12) {
            return 0;
        }
        for (int i = 0; i <= intervals && n < max_ticks; i++) {
            ticks[n++] = axis->min_value + (span * i) / (double)intervals;
        }
    }
    return n;
}


void plot_frame_init(PlotFrame *frame, int canvas_width, int canvas_height, int margin_left, int margin_top, int margin_right, int margin_bottom) {
    frame->width  = canvas_width;
    frame->height = canvas_height;
    frame->plot_x = margin_left;
    frame->plot_y = margin_top;
    frame->plot_w = canvas_width  - margin_left - margin_right;
    frame->plot_h = canvas_height - margin_top  - margin_bottom;

    if (frame->plot_w < 1) {
        frame->plot_w = 1;
    }
    if (frame->plot_h < 1) {
        frame->plot_h = 1;
    }
}

double plot_map_x(const PlotFrame *frame, const AxisConfig *x, double x_value) {
    if (x->log_scale) {
        if (x_value <= 0.0) {
            x_value = x->min_value;
        }
        const double log_min = log10(x->min_value);
        const double log_max = log10(x->max_value);
        const double span = log_max - log_min;
        if (fabs(span) < 1e-12) {
            return frame->plot_x + frame->plot_w * 0.5;
        }
        const double t = (log10(x_value) - log_min) / span;
        return frame->plot_x + t * frame->plot_w;
    }

    const double span = x->max_value - x->min_value;
    if (fabs(span) < 1e-12) {
        return frame->plot_x + frame->plot_w * 0.5;
    }
    const double t = (x_value - x->min_value) / span;
    return frame->plot_x + t * frame->plot_w;
}

double plot_map_y(const PlotFrame *frame, const AxisConfig *y, double y_value) {
    if (y->log_scale) {
        if (y_value <= 0.0) {
            y_value = y->min_value;
        }
        const double log_min = log10(y->min_value);
        const double log_max = log10(y->max_value);
        const double span = log_max - log_min;
        if (fabs(span) < 1e-12) {
            return frame->plot_y + frame->plot_h * 0.5;
        }
        const double t = (log10(y_value) - log_min) / span;
        return frame->plot_y + frame->plot_h - t * frame->plot_h;
    }

    const double span = y->max_value - y->min_value;
    if (fabs(span) < 1e-12) {
        return frame->plot_y + frame->plot_h * 0.5;
    }
    const double t = (y_value - y->min_value) / span;
    return frame->plot_y + frame->plot_h - t * frame->plot_h;
}


void plot_draw_axes(cairo_t *cr, const PlotFrame *frame, const AxisConfig *x_axis, const AxisConfig *y_axis) {
    //Faint grid
    double x_ticks[24];
    double y_ticks[24];
    const int nx = collect_ticks(x_axis, x_ticks, 24);
    const int ny = collect_ticks(y_axis, y_ticks, 24);

    cairo_save(cr);
    cairo_set_line_width(cr, 0.5);
    cairo_set_source_rgba(cr, COLOR_GRID, 0.55);

    for (int i = 0; i < nx; i++) {
        const double px = plot_map_x(frame, x_axis, x_ticks[i]);
        cairo_move_to(cr, px, frame->plot_y);
        cairo_line_to(cr, px, frame->plot_y + frame->plot_h);
    }
    for (int i = 0; i < ny; i++) {
        const double py = plot_map_y(frame, y_axis, y_ticks[i]);
        cairo_move_to(cr, frame->plot_x, py);
        cairo_line_to(cr, frame->plot_x + frame->plot_w, py);
    }

    cairo_stroke(cr);
    cairo_restore(cr);

    //Axis lines
    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, COLOR_AXIS);

    //Left Y axis
    cairo_move_to(cr, frame->plot_x, frame->plot_y);
    cairo_line_to(cr, frame->plot_x, frame->plot_y + frame->plot_h);

    //bottom X axis
    cairo_line_to(cr, frame->plot_x + frame->plot_w, frame->plot_y + frame->plot_h);

    cairo_stroke(cr);
    cairo_restore(cr);

    //Ticks marks and labels
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_TICK);
    cairo_set_source_rgb(cr, COLOR_TEXT);

    //X ticks below axis
    for (int i = 0; i < nx; i++) {
        const double px = plot_map_x(frame, x_axis, x_ticks[i]);
        const double py = frame->plot_y + frame->plot_h;

        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgb(cr, COLOR_AXIS);
        cairo_move_to(cr, px, py);
        cairo_line_to(cr, px, py + TICK_LEN);
        cairo_stroke(cr);

        char label[32];
        format_tick(label, sizeof(label), x_ticks[i], x_axis->log_scale);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        cairo_set_source_rgb(cr, COLOR_TEXT);
        cairo_move_to(cr, px - ext.width / 2.0 - ext.x_bearing, py + TICK_LEN + TICK_GAP - ext.y_bearing);
        cairo_show_text(cr, label);
    }

    //Y ticks left of axis
    for (int i = 0; i < ny; i++) {
        const double px = frame->plot_x;
        const double py = plot_map_y(frame, y_axis, y_ticks[i]);

        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgb(cr, COLOR_AXIS);
        cairo_move_to(cr, px, py);
        cairo_line_to(cr, px - TICK_LEN, py);
        cairo_stroke(cr);

        char label[32];
        format_tick(label, sizeof(label), y_ticks[i], y_axis->log_scale);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        cairo_set_source_rgb(cr, COLOR_TEXT);
        cairo_move_to(cr, px - TICK_LEN - TICK_GAP - ext.width - ext.x_bearing, py - ext.height / 2.0 - ext.y_bearing);
        cairo_show_text(cr, label);
    }

    cairo_restore(cr);

    //Axis titles
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, FONT_AXIS_TITLE);
    cairo_set_source_rgb(cr, COLOR_TEXT_DIM);

    //X axis title below tick labels, center
    {
        char title[64];
        if (x_axis->unit && x_axis->unit[0])
            snprintf(title, sizeof(title), "%s (%s)", x_axis->label, x_axis->unit);
        else
            snprintf(title, sizeof(title), "%s", x_axis->label);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, title, &ext);

        const double cx = frame->plot_x + frame->plot_w / 2.0;
        const double cy = frame->height - TITLE_GAP - ext.height - ext.y_bearing;

        cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, cy);
        cairo_show_text(cr, title);
    }

    //Y axis title,rotated 90 degrees, centered vertically
    {
        char title[64];
        if (y_axis->unit && y_axis->unit[0])
            snprintf(title, sizeof(title), "%s (%s)", y_axis->label, y_axis->unit);
        else
            snprintf(title, sizeof(title), "%s", y_axis->label);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, title, &ext);

        const double cx = TITLE_GAP + ext.height / 2.0;
        const double cy = frame->plot_y + frame->plot_h / 2.0;

        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, -M_PI / 2.0);

        cairo_move_to(cr, -ext.width / 2.0 - ext.x_bearing, -ext.height / 2.0 - ext.y_bearing);

        cairo_show_text(cr, title);
        cairo_restore(cr);
    }
    cairo_restore(cr);
}



