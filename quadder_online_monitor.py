"""FTA SCD Microstrip Raw Data Viewer with UDP Streaming Support"""

import sys
import numpy as np
import pandas as pd
import socket
import threading

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QPushButton, QLabel,
    QHBoxLayout, QComboBox, QSizePolicy, QFileDialog, QCheckBox, QGroupBox, QSpinBox, QLineEdit
)
from PyQt6.QtGui import QIntValidator


from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


UDP_IP   = "127.0.0.1"
UDP_PORT = 8890
BUF_SIZE = 65535

EVENT_START = 0xfa4af1ca
QUADDER_START = 0xbaba1a9a
QUADDER_END   = 0x0bedface

def reorder(v):
    """Reorder ADC channels from multiplexer in the correct sequence."""
    reordered = [0] * len(v)
    j = 0
    order = [12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1]

    for ch in range(128):
        for adc in order:
            reordered[adc * 128 + ch] = v[j]
            j += 1

    return reordered

def decode_quadder(words):
    """Decode the raw data from the quadder."""
    channels = []
    for w in words:
        ch_low  = (w & 0xFFFF)
        ch_high = ((w >> 16) & 0xFFFF)
        channels.append(ch_low)
        channels.append(ch_high)
    return channels

class EventViewer(QWidget):
    def __init__(self):
        super().__init__()
        self.trees = {}
        self.current_tree = None
        self.events = []
        self.index = 0
        self.current_file = ""
        self.current_calib_file = None
        self.calib_df = None

        self.udp_running = False
        self.udp_thread = None
        self.udp_stop_event = threading.Event()
        self.udp_event_id = 0

        self.udp_pending = False
        self.udp_ch = None

        self.udp_line = None

        self.udp_bar = None
        self.udp_n_bins = 100

        # Accumulation state
        self.udp_accum_sum = None
        self.udp_accum_count = 0

        self.COLUMNS = [
            "channel",
            "va_id",
            "va_channel",
            "pedestal",
            "sigma_raw",
            "sigma",
            "flag",
            "extra",
        ]

        self.setWindowTitle("HERD SCD FTA Online Event Viewer")
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(10, 10, 10, 10)
        self.layout.setSpacing(8)

        # ---------- Files Group ----------
        file_box = QGroupBox("Files")
        file_layout = QVBoxLayout(file_box)

        self.calib_label = QLabel("No calibration file opened")
        file_layout.addWidget(self.calib_label)

        self.open_calib_btn = QPushButton("Open calibration file")
        self.open_calib_btn.clicked.connect(self.open_calib_file)
        file_layout.addWidget(self.open_calib_btn)

        self.subtract_pedestal = QCheckBox("Subtract pedestal")
        self.subtract_pedestal.setChecked(False)
        self.subtract_pedestal.setEnabled(False)
        file_layout.addWidget(self.subtract_pedestal)

        self.layout.addWidget(file_box)

        # ---------- Matplotlib Figure ----------
        self.fig = Figure()
        self.canvas = FigureCanvas(self.fig)
        self.canvas.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.layout.addWidget(self.canvas)

        # Initial plot
        ax = self.fig.add_subplot(111)
        ax.grid(True, alpha=0.2)
        self.canvas.draw()

        # ---------- Figure axis limits ----------
        axis_layout = QHBoxLayout()
        self.x_axis_min_label = QLabel("X min: ")
        self.x_axis_min_label.setFixedWidth(50)
        axis_layout.addWidget(self.x_axis_min_label)
        self.x_axis_min = QLineEdit()
        self.x_axis_min.setValidator(QIntValidator(0, 1791))
        self.x_axis_min.setText("0")
        self.x_axis_min.textChanged.connect(self._on_axis_limit_changed)
        axis_layout.addWidget(self.x_axis_min)

        self.x_axis_max_label = QLabel("X max: ")
        self.x_axis_max_label.setFixedWidth(50)
        axis_layout.addWidget(self.x_axis_max_label)
        self.x_axis_max = QLineEdit()
        self.x_axis_max.setValidator(QIntValidator(0, 1791))
        self.x_axis_max.setText("1791")
        self.x_axis_max.textChanged.connect(self._on_axis_limit_changed)
        axis_layout.addWidget(self.x_axis_max)

        self.y_axis_min_label = QLabel("Y min: ")
        self.y_axis_min_label.setFixedWidth(50)
        axis_layout.addWidget(self.y_axis_min_label)
        self.y_axis_min = QLineEdit()
        self.y_axis_min.setValidator(QIntValidator(-16383, 16383))
        self.y_axis_min.setText("-20")
        self.y_axis_min.textChanged.connect(self._on_axis_limit_changed)
        axis_layout.addWidget(self.y_axis_min)

        self.y_axis_max_label = QLabel("Y max: ")
        self.y_axis_max_label.setFixedWidth(50)
        axis_layout.addWidget(self.y_axis_max_label)
        self.y_axis_max = QLineEdit()
        self.y_axis_max.setValidator(QIntValidator(-16383, 16383))
        self.y_axis_max.setText("80")
        self.y_axis_max.textChanged.connect(self._on_axis_limit_changed)
        axis_layout.addWidget(self.y_axis_max)

        self.y_axis_min = QSpinBox()
        self.y_axis_min_label = QLabel("Y min: ")
        self.y_axis_min_label.setFixedWidth(50)
        axis_layout.addWidget(self.y_axis_min_label)

        self.auto_axis = QCheckBox("Auto")
        self.auto_axis.setChecked(True)
        self.auto_axis.stateChanged.connect(self._on_axis_limit_changed)
        axis_layout.addWidget(self.auto_axis)

        self.layout.addLayout(axis_layout)

        # ---------- UDP Controls ----------
        udp_layout = QHBoxLayout()
        self.udp_btn = QPushButton("Start UDP")
        self.udp_btn.clicked.connect(self.toggle_udp)
        udp_layout.addWidget(self.udp_btn)
        
        self.udp_select_quadder = QComboBox()
        self.udp_select_quadder_label = QLabel("Quadder:")
        self.udp_select_quadder_label.setFixedWidth(60)
        udp_layout.addWidget(self.udp_select_quadder_label)
        
        for i in range(8):
            self.udp_select_quadder.addItem(f"{i}")
        self.udp_select_quadder.setCurrentIndex(0)
        self.udp_select_quadder.currentIndexChanged.connect(self._on_quadder_change)
        udp_layout.addWidget(self.udp_select_quadder)

        self.accumulate_checkbox = QCheckBox("Accumulate")
        self.accumulate_checkbox.setChecked(False)
        self.accumulate_checkbox.setStyleSheet("QCheckBox { margin-left: auto; }")
        self.accumulate_checkbox.stateChanged.connect(self._on_accumulate_toggled)
        udp_layout.addWidget(self.accumulate_checkbox)

        udp_box = QGroupBox("UDP Stream")
        udp_box.setLayout(udp_layout)
        self.layout.addWidget(udp_box)

        # ---------- Redraw Timer ----------
        self.redraw_timer = QTimer(self)
        self.redraw_timer.setInterval(40)
        self.redraw_timer.timeout.connect(self.redraw_if_pending)
        self.redraw_timer.start()

        # ---------- Global Styles ----------
        app.setStyleSheet("""
        QWidget {
            background-color: #f6f7f8;
            color: #1f2933;
            font-size: 10pt;
        }
        QGroupBox {
            border: 1px solid #d1d5db;
            border-radius: 6px;
            margin-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
            font-weight: 600;
        }
        QPushButton {
            background-color: #e5e7eb;
            border: 1px solid #cbd5e1;
            padding: 4px 10px;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #dbeafe;
        }
        QLineEdit, QComboBox {
            background-color: white;
            border: 1px solid #cbd5e1;
            border-radius: 4px;
            padding: 2px 6px;
        }
        """)
        
    # ----------------- quadder change handling -----------------
    def _on_quadder_change(self):
        self._reset_accumulation()
        self.udp_pending = True # trigger immediate redraw

    # ----------------- Axis limit changing handling -----------------
    def _on_axis_limit_changed(self):
        ax = self.fig.axes[0]
        ax.set_xlim(int(self.x_axis_min.text()), int(self.x_axis_max.text()))
        ax.set_ylim(int(self.y_axis_min.text()), int(self.y_axis_max.text()))
        self.canvas.draw()

    # ----------------- Accumulation -----------------
    def _on_accumulate_toggled(self, state):
        if state == 0:  # unchecked
            self._reset_accumulation()
            self.udp_pending = True  # trigger immediate redraw with last single event

    def _reset_accumulation(self):
        self.udp_accum_sum = None
        self.udp_accum_count = 0
        if self.udp_bar is not None:
            self.udp_bar.remove()
            self.udp_bar = None

    # ----------------- UDP -----------------
    # NOTE: Only UDP stream containing 1 quadder is supported at the moment
    def toggle_udp(self):
        if self.udp_running:
            self.stop_udp()
            self.udp_btn.setText("Start UDP")
        else:
            self.start_udp()
            self.udp_btn.setText("Stop UDP")

    def start_udp(self):
        if self.udp_running:
            return
                
        self.udp_running = True
        self.udp_stop_event.clear()
        self.udp_thread = threading.Thread(target=self.udp_loop, daemon=True)
        self.udp_thread.start()

    def stop_udp(self):
        if not self.udp_running:
            return
        
        self.udp_stop_event.set()
        self.udp_running = False
        if self.udp_thread:
            self.udp_thread.join(timeout=1)
            self.udp_thread = None

    def udp_loop(self):
        # Open UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.settimeout(0.2)

        in_event = False
        in_quadder = False
        words_read = 0
        num_quadders = 0
        quadder_read = 0
        quadder_words = []

        # Read UDP packets
        while not self.udp_stop_event.is_set():
            try:
                data, _ = sock.recvfrom(BUF_SIZE) # Buffered read to get full event data
            except socket.timeout:
                continue
            
            n = len(data) // 4 # Number of words in the packet
            for i in range(n):
                w = int.from_bytes(data[4 * i:4 * i + 4], "little")
                words_read += 1

                # Search for event start
                if w == EVENT_START:
                    in_event = True
                    in_quadder = False
                    quadder_read = 0 # Reset quadder read counter
                    quadder_words.clear() # Clear quadder words buffer
                    continue

                if not in_event:
                    continue
                
                # Read number of quadders in the event
                if in_event and words_read == 4:
                    num_quadders = w & 0xFFF
                
                if w == QUADDER_START:
                    quadder_read += 1
                    
                    # Check if we are at the correct quadder number based on the dropdown selection
                    selected_quadder = self.udp_select_quadder.currentText()
                    if quadder_read == int(selected_quadder) + 1:
                        in_quadder = True
                        quadder_words.clear()
                        continue

                if w == QUADDER_END and in_quadder and quadder_read == int(selected_quadder) + 1:
                    channels = reorder(decode_quadder(quadder_words[8:]))
                    ch = np.array(channels[:1792], dtype=np.int32)

                    self.udp_event_id += 1
                    self.udp_ch = ch
                    self.udp_pending = True

                    in_event = False
                    in_quadder = False
                    quadder_words.clear()
                    continue

                if in_quadder and quadder_read == int(selected_quadder) + 1:
                    quadder_words.append(w)

        sock.close()

    def redraw_if_pending(self):
        if not self.udp_pending:
            return

        ax = self.fig.axes[0]
        quadder = self.udp_select_quadder.currentText()

        if self.udp_line is None:
            x = np.arange(1792)
            self.udp_line, = ax.plot(x, np.zeros_like(x), label="quadder" + quadder)
            ax.set_xlabel("Channel")
            ax.set_ylabel("ADC count")
            ax.set_xticks(np.arange(0, 1792, 64))
            ax.set_xticklabels(np.arange(0, 1792, 64))
            ax.grid(True, alpha=0.2)

        x = np.arange(1792)
        accumulating = self.accumulate_checkbox.isChecked()

        ch = self.udp_ch.copy()
        if self.calib_df is not None and self.subtract_pedestal.isChecked():
            pedestal_values = self.calib_df[self.calib_df["name"] == int(quadder)]["pedestal"].to_numpy()
            if len(pedestal_values) == len(ch):
                ch = ch - pedestal_values

        if accumulating:
            if self.udp_accum_sum is None:
                self.udp_accum_sum = np.zeros(1792, dtype=np.float64)
            self.udp_accum_sum += ch
            self.udp_accum_count += 1

            # Bin the accumulated sum
            n = self.udp_n_bins
            bin_size = 1792 // n
            indices = np.arange(0, bin_size * n, bin_size)
            binned = np.add.reduceat(self.udp_accum_sum, indices)
            bin_centers = indices + bin_size / 2

            # Remove old bar container
            if self.udp_bar is not None:
                self.udp_bar.remove()
            self.udp_bar = ax.bar(bin_centers, binned, width=bin_size * 0.9, color='tab:blue', alpha=0.7)

            self.udp_line.set_visible(False)
            plot_data = self.udp_accum_sum  # for y-scale
            title = f"UDP Accumulated {self.udp_accum_count} events"
        else:
            plot_data = ch
            title = f"UDP Event {self.udp_event_id}"

        self.udp_line.set_data(x, plot_data)
        self.udp_line.set_visible(not accumulating)
        ax.set_title(title)
        display_data = plot_data

        if self.auto_axis.isChecked():
            # Auto-scale Y axis with a small margin
            ax.set_xlim(0, 1791)
            ymin = float(np.min(display_data))
            ymax = float(np.max(display_data))
            margin = max((ymax - ymin) * 0.05, 10)
            ax.set_ylim(ymin - margin, ymax + margin)

        self.canvas.draw_idle()
        self.udp_pending = False

    def open_calib_file(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open calibration file", "", "Calibration Files (*.cal)")
        if not file_path:
            return

        self.current_calib_file = file_path
        self.calib_label.setText(f"Opened calibration file: {self.current_calib_file}")

        self.calib_df = self.read_calibration_file(file_path)
        self.subtract_pedestal.setEnabled(True)

    # ----------------- Calibration File Parsing -----------------
    def read_calibration_file(self, file_path):
        records = []
        current_idx = -1
        header_lines_seen = 0
        in_data_block = False

        with open(file_path, "r") as f:
            for line in f:
                line = line.strip()
                if line.startswith("#name="):
                    current_idx += 1
                if line.startswith("#"):
                    header_lines_seen += 1
                    in_data_block = header_lines_seen >= 18
                    continue
                if not line:
                    header_lines_seen = 0
                    in_data_block = False
                    continue
                if not in_data_block:
                    continue
                parts = [p.strip() for p in line.split(",")]
                if len(parts) != 8:
                    continue
                records.append([current_idx] + parts)

        df = pd.DataFrame(records, columns=["name"] + self.COLUMNS)
        df[self.COLUMNS] = df[self.COLUMNS].apply(pd.to_numeric, errors="coerce")
        return df
        
if __name__ == "__main__":
    app = QApplication(sys.argv)
    viewer = EventViewer()
    viewer.show()
    sys.exit(app.exec())