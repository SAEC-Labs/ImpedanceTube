//
// Created by torq on 6/20/26.
//

#ifndef SAECTUBE_GUI_H
#define SAECTUBE_GUI_H

#include "ring_buffer.h"

/**
 * Launch the GTK GUI and enter the main event loop.
 *
 * This function blocks until the main window is closed.
 *
 * @param argc  Pointer to argc from main()
 * @param argv  Pointer to argv from main()
 * @param rb    Ring buffer containing audio samples
 * @return      0 on normal exit, -1 on error
 */
int gui_run(int argc, char **argv, RingBuffer *rb);

#endif //SAECTUBE_GUI_H
