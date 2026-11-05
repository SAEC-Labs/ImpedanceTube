//
// Created by torq on 10/31/26.
//

/**
 * Reusable plot axes and grid rendering for Cairo
 */

#ifndef SAEC_STUDIO_PLOT_AXES_H
#define SAEC_STUDIO_PLOT_AXES_H

#include <cairo.h>

//layout of a plot region within a canvas
typedef struct {
    int width;
    int height;
    int plot_x;
    int plot_y;
    int plot_w;
    int plot_h;
} PlotFrame;

typedef struct {
    const char *label;
    const char *unit;
    double min_value;
    double max_value;
    int log_scale;
    int num_ticks;
} AxisConfig;

/**
 * Compute the inner plot rect from the canvas and maregins.
 */
void plot_frame_init(PlotFrame *frame, int canvas_width, int canvas_height, int margin_left, int margin_top,
                     int margin_right, int margin_bottom);


/**
 * Draw grid lines, axis lines, tick marks, tick labels, and axis titles.
 * Should be callled once per frame, before drawing data.
 */
void plot_draw_axes(cairo_t *cr, const PlotFrame *frame, const AxisConfig *x_axis, const AxisConfig *y_axis);


/**
 * Map a data value to a pixel coordinate (in x direction).
 * Returns the pixel x for the given data value.
 */
double plot_map_x(const PlotFrame *frame, const AxisConfig *x, double x_value);


/**
 * Map a data value to a pixel coordinate (Y direction).
 * Y is inverted (larger data value -> smaller pixel Y).
 */
double plot_map_y(const PlotFrame *frame, const AxisConfig *y, double y_value);



#endif //SAEC_STUDIO_PLOT_AXES_H
