# WaveView - GUI software for the Impedance Test Tube
WaveView is a pure‑C, cross‑platform application for real‑time audio capture, waveform visualization, and FFT‑based spectrum analysis. It uses:
- **PortAudio** for low‑latency audio I/O (ALSA on Linux, ASIO/WASAPI on Windows)
- **KissFFT** for fast real‑time FFT
- **GTK4 + Cairo** for a clean, responsive graphical interface.

It is designed as the software frontend for the acoustic impedance tube project, and serves as a general‑purpose audio visualizer and the starting point for more advanced DSP applications.

## Implemeted Features

- 🎤 Real‑time microphone capture (default input device)
- 📈 Live waveform display (time domain)
- 📊 Live FFT spectrum display (frequency domain)
- 🔄 Thread‑safe, lock‑free ring buffer for sample transfer
- 🧩 Modular C architecture (audio, DSP, GUI, ring buffer)
- 🐧 Works on Linux (ALSA) and Windows (ASIO/WASAPI)
## Planned Features

### 🖥️ GUI Enhancements

- **Start/Stop controls** – manual acquisition control
- **Device selection** – choose input device from a dropdown
- **Excitation signal generator** – sine, sweep, white noise with frequency range sliders
- **Multi‑tab display** – separate tabs for waveform, spectrum, transfer function, absorption coefficient
- **Peak frequency marker** – click‑to‑measure dominant frequency
- **Data logging** – save raw WAV files, CSV with timestamps, and JSON metadata (sample info, environment)
- **Export plots** – save waveform/spectrum as PNG
- **Dark/Light theme toggle** – for comfortable viewing

### 📊 Heavy DSP Backend

- **Two‑channel (stereo) processing** – for two‑microphone transfer‑function method
- **Cross‑spectrum & auto‑spectrum** – `S12`, `S11`, `S22` with averaging
- **Transfer function** – `H12 = S12 / S11`
- **Reflection coefficient** – `R` (complex)
- **Normal incidence absorption coefficient** – `α(f) = 1 - |R(f)|²`
- **Coherence function** – `γ²(f)` to validate measurement quality
- **Surface impedance** – real and imaginary parts
- **Frequency‑dependent uncertainty** – confidence intervals based on coherence

### 🔌 Hardware Integration

- **STM32 USB Audio support** – replace PC microphone with custom DAQ
- **Simultaneous 4‑channel capture** – for full four‑microphone transmission loss measurements
- **Auto‑detection of USB audio interfaces** – plug‑and‑play support

## Dependencies

| Library | Purpose | Linux package | Windows (MSYS2) package |
|---------|---------|---------------|--------------------------|
| PortAudio | Audio I/O | `libportaudio2`, `portaudio19-dev` | `mingw-w64-ucrt-x86_64-portaudio` |
| GTK4 | GUI | `libgtk-4-dev` | `mingw-w64-ucrt-x86_64-gtk4` |
| Cairo | 2D drawing | `libcairo2-dev` | (included with GTK4) |
| KissFFT | FFT | bundled in `src/external/` | bundled in `src/external/` |

## Building

### On Linux (Debian/Ubuntu/Kali)

1. **Install dependencies**  
   ```bash
   sudo apt update
   sudo apt install cmake gcc libportaudio2 portaudio19-dev libgtk-4-dev libcairo2-dev
2. **clone the repo**
   ```bash
   git clone https://github.com/SAEC-Labs/ImpedanceTube.git
   cd WaveView
3. **buid with Cmake**
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
4. Run the binary

## On Windows - Native build with MSYS2
This method builds a native Windows executable inside the MSYS2 UCRT64 environment.

1. **Install MSYS2 from [ https://www.msys2.org/ ] and launch the UCRT64 terminal.**
2. **Install Dependencies**
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-portaudio \
          mingw-w64-ucrt-x86_64-gtk4 \
          mingw-w64-ucrt-x86_64-make
3. **clone and build**
   ```bash
   git clone https://github.com/SAEC-Labs/ImpedanceTube.git
   chdir WaveView
   mkdir build && chdir build
   cmake .. -G "MinGW Makefiles"
   make -j$(nproc)
4. **Run the `.exe` file

## Usage
1. Plug in a microphone (or use the built‑in one).
2.Launch the application.
3. Speak, whistle, or make noise – the waveform and spectrum update live.
4. Close the window or press Ctrl+C to exit.

## Authors & Credits
1. **SAEC Team** – Mechatronics Engineering students, DeKUT
2. **KissFFT** – Mark Borgerding (public domain / BSD)
3. **PortAudio** – PortAudio community (MIT)
4. **GTK** – The GTK team (LGPL)

## License
See the `LICENSE` for details

