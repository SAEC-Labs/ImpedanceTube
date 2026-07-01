"""
SAEC Acoustic Impedance Tube - Data Acquisition Dashboard
Author: Ronald Mutua Mwau
Description: Python-based serial monitor and real-time visualization tool for STM32 hardware, 
specifically designed for testing acoustic impedance tube microphone sensors.
"""
import sys
import threading
import time
import wave
import os
from datetime import datetime
import numpy as np
import pyaudio
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets, QtGui
import serial
import serial.tools.list_ports

class AudioSerialApp(QtWidgets.QMainWindow):

    def __init__(self):
        super().__init__()

        # Initialize State variables
        self.is_running = False
        self.is_recording = False
        self.recorded_frames = []
        self.serial_conn = None
        self.read_thread = None
        self.last_detected_ports = []
        
        # Audio Metadata Tracking
        self.record_start_time = None
        self.last_wav_filename = ""
        self.total_playback_duration = 0.0
        self.playback_start_time = None
        
        # Dynamic Sample Rate Calibration variables
        self.sample_count_total = 0
        self.stream_start_wall_time = 0.0
        self.calibrated_sample_rate = 8000

        # Set up folders, UI Layout & Apply Global stylesheet
        os.makedirs("recordings", exist_ok=True)
        self.init_ui()
        self.apply_modern_theme()

        self.p = pyaudio.PyAudio()

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_plot)
        
        self.ui_status_timer = QtCore.QTimer()
        self.ui_status_timer.timeout.connect(self.update_status_counters)
        self.ui_status_timer.start(100) 

        self.port_monitor_timer = QtCore.QTimer()
        self.port_monitor_timer.timeout.connect(self.auto_check_ports)
        self.port_monitor_timer.start(1000)

        self.plot_buffer = np.zeros(4096)

    def init_ui(self):
        self.setWindowTitle("SAEC Acoustic Impedance Tube - Data Acquisition Dashboard")
        self.setWindowIcon(QtGui.QIcon("src/SAEC_logo.png"))
        self.resize(1100, 700)

        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QtWidgets.QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(15)

        # --- Top Section: Header Group ---
        header_widget = QtWidgets.QWidget()
        header_widget.setObjectName("HeaderCard")
        header_layout = QtWidgets.QHBoxLayout(header_widget)
        
        logo_label = QtWidgets.QLabel()
        pixmap = QtGui.QPixmap("src/SAEC_logo.png")
        if not pixmap.isNull():
            logo_label.setPixmap(pixmap.scaledToHeight(50, QtCore.Qt.TransformationMode.SmoothTransformation))
        header_layout.addWidget(logo_label)
        header_layout.addSpacing(15)
        
        text_layout = QtWidgets.QVBoxLayout()
        text_layout.setAlignment(QtCore.Qt.AlignmentFlag.AlignVCenter)
        title_label = QtWidgets.QLabel("Audio Stream Monitor")
        title_label.setStyleSheet("font-size: 20px; font-weight: bold; color: #141B22; background: transparent;")
        subtitle_label = QtWidgets.QLabel("Smart Acoustics and Elastics Centre - Data Acquisition Dashboard")
        subtitle_label.setStyleSheet("font-size: 13px; color: #475569; background: transparent;")
        text_layout.addWidget(title_label)
        text_layout.addWidget(subtitle_label)
        header_layout.addLayout(text_layout)
        
        # Live Session Status Display
        self.lbl_status_badge = QtWidgets.QLabel("Device Idle")
        self.lbl_status_badge.setObjectName("StatusBadge")
        self.lbl_status_badge.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.lbl_status_badge.setFixedWidth(240)
        header_layout.addWidget(self.lbl_status_badge)
        
        main_layout.addWidget(header_widget)

        # --- Configuration Toolbar panel ---
        config_group = QtWidgets.QGroupBox("Hardware & Capture Configurations")
        config_layout = QtWidgets.QHBoxLayout(config_group)
        config_layout.setContentsMargins(12, 15, 12, 12)

        config_layout.addWidget(QtWidgets.QLabel("Port:"))
        self.combo_port = QtWidgets.QComboBox()
        self.refresh_ports()
        config_layout.addWidget(self.combo_port)
        
        self.btn_refresh = QtWidgets.QPushButton("🔄")
        self.btn_refresh.setFixedWidth(35)
        self.btn_refresh.clicked.connect(self.refresh_ports)
        config_layout.addWidget(self.btn_refresh)

        config_layout.addWidget(QtWidgets.QLabel("Baud:"))
        self.combo_baud = QtWidgets.QComboBox()
        self.combo_baud.addItems(["9600", "115200", "230400", "460800", "921600"])
        self.combo_baud.setCurrentText("115200")
        config_layout.addWidget(self.combo_baud)

        # Target Rate, Chunk Size, and Data Type are hardcoded internally
        self.chunk_size = 512
        self.data_type = np.uint16

        main_layout.addWidget(config_group)

        # --- Display & Mode Controls Section ---
        view_group = QtWidgets.QGroupBox("View Settings")
        view_layout = QtWidgets.QHBoxLayout(view_group)
        
        self.radio_both = QtWidgets.QRadioButton("Split View")
        self.radio_plot = QtWidgets.QRadioButton("Visual Plotter Only")
        self.radio_text = QtWidgets.QRadioButton("Terminal Logs Only")
        self.radio_both.setChecked(True)
        
        self.radio_both.toggled.connect(self.update_view_visibility)
        self.radio_plot.toggled.connect(self.update_view_visibility)
        self.radio_text.toggled.connect(self.update_view_visibility)
        
        view_layout.addWidget(self.radio_both)
        view_layout.addWidget(self.radio_plot)
        view_layout.addWidget(self.radio_text)
        
        view_layout.addSpacing(40)
        
        view_layout.addStretch()
        
        main_layout.addWidget(view_group)

        # --- Control Action Buttons Panel ---
        control_layout = QtWidgets.QHBoxLayout()

        self.btn_start = QtWidgets.QPushButton("Connect Device")
        self.btn_start.setObjectName("StartButton")
        self.btn_start.clicked.connect(self.toggle_stream)
        control_layout.addWidget(self.btn_start)

        self.btn_record = QtWidgets.QPushButton("Record Session")
        self.btn_record.setEnabled(False)
        self.btn_record.clicked.connect(self.toggle_record)
        control_layout.addWidget(self.btn_record)

        self.btn_play = QtWidgets.QPushButton("Play Last Track")
        self.btn_play.setEnabled(False)
        self.btn_play.clicked.connect(self.play_audio)
        control_layout.addWidget(self.btn_play)

        main_layout.addLayout(control_layout)

        # --- Central Visual split panel view ---
        self.display_splitter = QtWidgets.QSplitter(QtCore.Qt.Orientation.Vertical)
        self.display_splitter.setHandleWidth(8)
        
        # Plotter Display
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground("#FFFFFF")
        self.plot_widget.getAxis('left').setPen('#94A3B8')
        self.plot_widget.getAxis('bottom').setPen('#94A3B8')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.15)
        
        self.curve = self.plot_widget.plot(
            pen=pg.mkPen(color='#006cb5', width=2)
        )
        
        # Hardcoded default plot width per user preference
        self.plot_widget.setXRange(4096 - 512, 4096)
        self.display_splitter.addWidget(self.plot_widget)
        
        # Text Monitor Log Display panel
        self.text_monitor = QtWidgets.QPlainTextEdit()
        self.text_monitor.setReadOnly(True)
        self.text_monitor.setObjectName("TerminalMonitor")
        self.text_monitor.setMaximumBlockCount(100)
        self.display_splitter.addWidget(self.text_monitor)

        main_layout.addWidget(self.display_splitter)

    def apply_modern_theme(self):
        qss = """
            QWidget { background-color: #F8F9FA; color: #141B22; font-family: 'Segoe UI', sans-serif; font-size: 13px; }
            #HeaderCard { background-color: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 8px; padding: 12px; }
            #StatusBadge { background-color: #F8F9FA; border: 1px solid #CBD5E1; border-radius: 6px; font-family: 'Consolas', monospace; font-weight: bold; font-size: 13px; color: #475569; }
            QGroupBox { font-weight: bold; color: #141B22; border: 1px solid #CBD5E1; border-radius: 8px; margin-top: 12px; padding-top: 8px; background-color: #FFFFFF; }
            QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; background-color: #FFFFFF; }
            QComboBox, QLineEdit { background-color: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 5px; padding: 4px 8px; color: #141B22; min-width: 60px; }
            QComboBox::drop-down { border: none; }
            QComboBox:hover, QLineEdit:hover { border: 1px solid #006cb5; }
            QRadioButton { spacing: 6px; color: #475569; }
            QRadioButton::checked { color: #006cb5; font-weight: bold; }
            QPushButton { background-color: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; padding: 8px 16px; font-weight: bold; color: #141B22; }
            QPushButton:hover { background-color: #F1F5F9; border-color: #94A3B8; }
            QPushButton:disabled { background-color: #F8F9FA; color: #94A3B8; border-color: #E2E8F0; }
            #StartButton { background-color: #006cb5; color: #ffffff; border: none; }
            #StartButton:hover { background-color: #005a96; }
            #TerminalMonitor { background-color: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 8px; font-family: 'Consolas', monospace; font-size: 13px; color: #141B22; padding: 8px; }
            QSplitter::handle { background-color: #CBD5E1; border-radius: 3px; }
        """
        self.setStyleSheet(qss)

    def update_status_counters(self):
        if self.is_recording and self.record_start_time:
            elapsed = time.time() - self.record_start_time
            m, s = divmod(int(elapsed), 60)
            self.lbl_status_badge.setText(f"🔴 RECORDING: {m:02d}:{s:02d}")
            self.lbl_status_badge.setStyleSheet("color: #a31e22; border-color: #a31e22; font-size: 14px; background-color: #fef2f2;")
            
        elif self.playback_start_time:
            elapsed = time.time() - self.playback_start_time
            if elapsed >= self.total_playback_duration:
                self.playback_start_time = None
                if self.is_running:
                    self.lbl_status_badge.setText(f"Live (~{self.calibrated_sample_rate} Hz)")
                    self.lbl_status_badge.setStyleSheet("color: #006cb5; border-color: #006cb5; background-color: #f0f9ff;")
                else:
                    self.lbl_status_badge.setText("Device Idle")
                    self.lbl_status_badge.setStyleSheet("color: #475569; border-color: #CBD5E1; background-color: #F8F9FA;")
            else:
                cur_m, cur_s = divmod(int(elapsed), 60)
                tot_m, tot_s = divmod(int(self.total_playback_duration), 60)
                self.lbl_status_badge.setText(f"🔊 PLAYING: {cur_m:02d}:{cur_s:02d} / {tot_m:02d}:{tot_s:02d}")
                self.lbl_status_badge.setStyleSheet("color: #f4a01c; border-color: #f4a01c; background-color: #fffbeb;")
                
        elif self.is_running:
            # Constantly display the measured live data transmission speed
            self.lbl_status_badge.setText(f"Live (~{self.calibrated_sample_rate} Hz)")
            self.lbl_status_badge.setStyleSheet("color: #006cb5; border-color: #006cb5; background-color: #f0f9ff;")
        else:
            self.lbl_status_badge.setText("Device Idle")
            self.lbl_status_badge.setStyleSheet("color: #475569; border-color: #CBD5E1; background-color: #F8F9FA;")

    def update_view_visibility(self):
        if self.radio_both.isChecked():
            self.plot_widget.setVisible(True)
            self.text_monitor.setVisible(True)
        elif self.radio_plot.isChecked():
            self.plot_widget.setVisible(True)
            self.text_monitor.setVisible(False)
        else:
            self.plot_widget.setVisible(False)
            self.text_monitor.setVisible(True)

    def refresh_ports(self):
        current_selection = self.combo_port.currentText()
        self.combo_port.clear()
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.last_detected_ports = sorted(ports)
        if ports:
            self.combo_port.addItems(ports)
            if current_selection in ports:
                self.combo_port.setCurrentText(current_selection)
        else:
            self.combo_port.addItem("No Ports Found")

    def auto_check_ports(self):
        if self.is_running:
            return
        current_ports = sorted([port.device for port in serial.tools.list_ports.comports()])
        if current_ports != self.last_detected_ports:
            self.refresh_ports()

    def toggle_stream(self):
        if not self.is_running:
            port = self.combo_port.currentText()
            if port == "No Ports Found" or not port:
                QtWidgets.QMessageBox.warning(self, "Configuration Error", "Please select a valid COM port.")
                return
            
            try:
                baud = int(self.combo_baud.currentText())
            except ValueError:
                QtWidgets.QMessageBox.warning(self, "Input Error", "Please verify numeric entries.")
                return

            self.plot_widget.setYRange(0, 4096)

            try:
                # Open connection directly without safety verification blocks
                self.serial_conn = serial.Serial(port, baud, timeout=1)
                self.is_running = True
                
                self.btn_start.setText("Disconnect Device")
                self.btn_start.setStyleSheet("background-color: #a31e22; color: #ffffff; font-weight: bold; border: none;")
                self.btn_record.setEnabled(True)
                
                self.combo_port.setEnabled(False)
                self.combo_baud.setEnabled(False)

                # Reset counters for dynamic timing calibration
                self.sample_count_total = 0
                self.stream_start_wall_time = time.time()

                self.read_thread = threading.Thread(target=self.read_serial_data, daemon=True)
                self.read_thread.start()

                self.timer.start(33)
            except Exception as e:
                QtWidgets.QMessageBox.critical(self, "Connection Error", f"Could not open serial port {port}:\n{e}")
        else:
            self.stop_stream()

    def stop_stream(self):
        self.is_running = False
        self.timer.stop()
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
            
        self.btn_start.setText("Connect Device")
        self.btn_start.setStyleSheet("")  
        self.btn_record.setEnabled(False)
        self.combo_port.setEnabled(True)
        self.combo_baud.setEnabled(True)
        
        if self.is_recording:
            self.toggle_record()

    def read_serial_data(self):
        buffer = bytearray()
        while self.is_running:
            try:
                if self.serial_conn and self.serial_conn.is_open:
                    in_waiting = self.serial_conn.in_waiting
                    if in_waiting > 0:
                        chunk = self.serial_conn.read(in_waiting)
                        buffer.extend(chunk)
                        
                        new_samples = []
                        new_raw_bytes = []
                        
                        while len(buffer) >= 4:
                            if buffer[2] == 0xAA and buffer[3] == 0xBB:
                                sample = buffer[0] | (buffer[1] << 8)
                                new_samples.append(sample)
                                if self.is_recording:
                                    new_raw_bytes.append(bytes(buffer[:2]))
                                del buffer[:4]
                            else:
                                del buffer[0]
                                
                        if new_samples:
                            n = len(new_samples)
                            if n >= len(self.plot_buffer):
                                self.plot_buffer[:] = new_samples[-len(self.plot_buffer):]
                            else:
                                self.plot_buffer[:-n] = self.plot_buffer[n:]
                                self.plot_buffer[-n:] = new_samples
                                
                            if self.is_recording:
                                self.recorded_frames.extend(new_raw_bytes)
                                
                    else:
                        time.sleep(0.001)
            except Exception as e:
                print(f"Serial worker read fault: {e}")
                break

    def update_plot(self):
        if self.is_running:
            if self.plot_widget.isVisible():
                self.curve.setData(self.plot_buffer)
            
            if self.text_monitor.isVisible():
                latest_val = int(self.plot_buffer[-1])
                self.text_monitor.appendPlainText(f"Sample: {latest_val}")

    def toggle_record(self):
        if not self.is_recording:
            self.recorded_frames = []
            self.is_recording = True
            
            # Anchor current runtime parameters
            self.record_start_time = time.time()  
            self.record_start_sample_index = self.sample_count_total
            
            self.btn_record.setText("Stop Recording")
            self.btn_record.setStyleSheet("background-color: #a31e22; color: #ffffff; font-weight: bold; border: none;")
            self.btn_play.setEnabled(False)
        else:
            self.is_recording = False
            self.btn_record.setText("Record Session")
            self.btn_record.setStyleSheet("")
            if len(self.recorded_frames) > 0:
                self.btn_play.setEnabled(True)
                self.save_wav_file()
            self.record_start_time = None

    def save_wav_file(self):
        """NEW: Auto-calculates sample rate using true hardware timing density metrics."""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.last_wav_filename = f"recordings/audio_{timestamp}.wav"
        
        # Determine exactly how many samples were captured during the specific recording duration
        samples_captured = len(self.recorded_frames)
        recording_duration_seconds = time.time() - self.record_start_time
        
        # Calculate true hardware capture velocity rate dynamically
        true_capture_sample_rate = int(samples_captured / recording_duration_seconds)
        if true_capture_sample_rate < 100: 
            true_capture_sample_rate = 8000 # Fallback safety line
            
        all_bytes = b"".join(self.recorded_frames)
        audio_np = np.frombuffer(all_bytes, dtype=self.data_type)

        if self.data_type == np.uint16:
            audio_fixed = ((audio_np.astype(np.int32) - 2048) * 16).astype(np.int16)
        elif self.data_type == np.uint8:
            audio_fixed = ((audio_np.astype(np.int32) - 128) * 256).astype(np.int16)
        else:
            audio_fixed = audio_np.astype(np.int16)

        with wave.open(self.last_wav_filename, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            # Inject the precise measured execution speed into wav file header configuration properties
            wf.setframerate(true_capture_sample_rate)
            wf.writeframes(audio_fixed.tobytes())
            
        print(f"Session saved to disk. Measured speed: {true_capture_sample_rate} Hz -> {self.last_wav_filename}")

    def play_audio(self):
        if not self.last_wav_filename or not os.path.exists(self.last_wav_filename):
            return

        def play_worker():
            try:
                wf = wave.open(self.last_wav_filename, "rb")
                total_samples = wf.getnframes()
                rate = wf.getframerate()
                self.total_playback_duration = total_samples / float(rate)
                self.playback_start_time = time.time()

                stream = self.p.open(
                    format=self.p.get_format_from_width(wf.getsampwidth()),
                    channels=wf.getnchannels(),
                    rate=rate,
                    output=True,
                )
                data = wf.readframes(self.chunk_size)
                while data and self.playback_start_time is not None:
                    stream.write(data)
                    data = wf.readframes(self.chunk_size)
                stream.stop_stream()
                stream.close()
            except Exception as e:
                print(f"Playback error: {e}")
            finally:
                self.playback_start_time = None

        threading.Thread(target=play_worker, daemon=True).start()

    def closeEvent(self, event):
        self.stop_stream()
        self.p.terminate()
        event.accept()

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    main_win = AudioSerialApp()
    main_win.show()
    sys.exit(app.exec())