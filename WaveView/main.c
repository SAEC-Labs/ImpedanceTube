/**Application entry point.
 *
 *Initializes the ring buffer, audio subsystem and launches the gui
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "src/config.h"
#include "src/ring_buffer.h"
#include "src/audio.h"
#include "src/gui.h"


//signal handler for nice termination with Ctrl+C

static volatile int keep_running = 1;

static void signal_handler(int sig) {
    (void) sig;
    keep_running = 0;
    fprintf(stderr, "\nReceived interrupt signal. Shutting down...\n");

}

int main(int argc, char **argv) {
    int ret = 0;

    //signal for Ctrl+C
    signal(SIGINT, signal_handler);

    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║     WaveView – Live Microphone Monitor    ║\n");
    printf("║          (C) 2026 SAEC Team               ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");

    printf("Configuration:\n");
    printf("  Sample Rate:     %d Hz\n", SAMPLE_RATE);
    printf("  FFT Size:        %d points\n", FFT_SIZE);
    printf("  Ring Buffer:     %d samples (~%.1f seconds)\n",
           RING_BUFFER_SIZE, (float)RING_BUFFER_SIZE / SAMPLE_RATE);
    printf("\n");

    //Create ring buffer
    printf("Initializing ring buffer... ");
    RingBuffer *rb = ring_buffer_create(RING_BUFFER_SIZE);

    if (rb == NULL) {
        fprintf(stderr, "ring_buffer_create failed\n");
        return 1;
    }
    printf("Ok\n");

    //initialize audio
    printf("Initializing audio subsystem... ");
    if (audio_init(rb) != 0) {
        fprintf(stderr, "audio_init failed\n");
        ring_buffer_destroy(rb); //make sure to destroy the buffer
        return 1;
    }
    printf("Ok (device: %s)\n", audio_get_device_name());

    //Launch GUI: blocks until window is closed
    printf("Launching GUI...\n\n");
    ret = gui_run(argc, argv, rb);

    //cleanup
    printf("\nCleaning Up... ");
    audio_terminate();
    ring_buffer_destroy(rb);
    printf("Ok\n");

    printf("\nWaveView exited.\n");

    return ret;
}






