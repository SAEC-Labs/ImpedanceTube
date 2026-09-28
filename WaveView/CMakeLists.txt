cmake_minimum_required(VERSION 4.2)
project(WaveView C)

set(CMAKE_C_STANDARD 11)

# Find the PkgConfig module
find_package(PkgConfig REQUIRED)

# Use pkg_check_modules to locate GTK4
# This will set GTK4_CFLAGS and GTK4_LIBRARIES automatically
pkg_check_modules(GTK4 REQUIRED gtk4)

pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# GResource compilation to embed .ui file into the executable
find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

set(GRESOURCE_XML ${CMAKE_CURRENT_SOURCE_DIR}/ui/resources.gresource.xml)
set(GRESOURCE_C   ${CMAKE_CURRENT_BINARY_DIR}/resources.c)

add_custom_command(
        OUTPUT  ${GRESOURCE_C}
        COMMAND ${GLIB_COMPILE_RESOURCES}
        --target=${GRESOURCE_C}
        --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}/ui
        --generate-source
        ${GRESOURCE_XML}
        DEPENDS ${GRESOURCE_XML}
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/main_window.ui
        VERBATIM
)

add_executable(WaveView main.c
        src/config.h
        src/ring_buffer.h
        src/ring_buffer.c
        src/audio.h
        src/audio.c
        src/dsp.h
        src/dsp.c
        src/gui.h
        src/gui.c
        src/external/kissfft/kiss_fft.h
        src/external/kissfft/kiss_fft.c
        src/external/kissfft/kiss_fftr.h
        src/external/kissfft/kiss_fftr.c
        src/signals/sine_wave.c
        src/signals/sine_wave.h
        src/signals/signals.h
        src/signals/linear_sweep.c
        src/signals/linear_sweep.h
        src/signals/log_sweep.c
        src/signals/log_sweep.h
        src/signals/white_noise.c
        src/signals/white_noise.h
        src/signals/pink_noise.c
        src/signals/pink_noise.h
        ${GRESOURCE_C} # generated C fle
)

# Link the libs
target_link_libraries(WaveView
        ${GTK4_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
        m                   #math library
)

# Include the header directories
target_include_directories(WaveView PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${PORTAUDIO_INCLUDE_DIRS}
        src
        src/external/kissfft

)

# Apply compiler flags
target_compile_options(WaveView PRIVATE
        ${GTK4_CFLAGS_OTHER}
        ${CAIRO_CFLAGS_OTHER}
        ${PORTAUDIO_CFLAGS_OTHER}
)


