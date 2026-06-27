cmake_minimum_required(VERSION 4.2)
project(SAECtube C)

set(CMAKE_C_STANDARD 11)

# Find the PkgConfig module
find_package(PkgConfig REQUIRED)

# Use pkg_check_modules to locate GTK4
# This will set GTK4_CFLAGS and GTK4_LIBRARIES automatically
pkg_check_modules(GTK4 REQUIRED gtk4)

pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

add_executable(SAECtube main.c
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
)

# Link the libs
target_link_libraries(SAECtube
        ${GTK4_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
        m                   #math library
)

# Include the header directories
target_include_directories(SAECtube PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${PORTAUDIO_INCLUDE_DIRS}
        src
        src/external/kissfft
)

# Apply compiler flags
target_compile_options(SAECtube PRIVATE
        ${GTK4_CFLAGS_OTHER}
        ${CAIRO_CFLAGS_OTHER}
        ${PORTAUDIO_CFLAGS_OTHER}
)