"""Microstrip Raw Data Viewer with UDP Streaming Support"""

import sys
import numpy as np
import ROOT
import pandas as pd
import socket
import threading
import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QPushButton, QLabel,
    QHBoxLayout, QComboBox, QSizePolicy, QFileDialog, QLineEdit, QCheckBox, QGroupBox
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


UDP_IP   = "127.0.0.1"
UDP_PORT = 8890
BUF_SIZE = 65535

EVENT_START = 0xfa4af1ca
BOARD_START = 0xbaba1a9a
BOARD_END   = 0x0bedface

def reorder(v):
    """Reorder ADC channels from multiplexer in the correct sequence."""
    reordered = [0] * len(v)
    j = 0
    order = [1, 0, 3, 2, 5, 4, 7, 6, 9, 8]

    for ch in range(128):
        for adc in order:
            reordered[adc * 128 + ch] = v[j]
            j += 1

    return reordered

def decode_board(words):
    """Decode the raw data from the board."""
    channels = []
    for w in words:
        ch_low  = (w & 0xFFFF) // 4 
        ch_high = ((w >> 16) & 0xFFFF) // 4
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
        self.udp_ch0 = None
        self.udp_ch1 = None

        self.udp_line0 = None
        self.udp_line1 = None

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

        self.setWindowTitle("Microstrip Event Viewer")
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(10, 10, 10, 10)
        self.layout.setSpacing(8)

        # ---------- Files Group ----------
        file_box = QGroupBox("Files")
        file_layout = QVBoxLayout(file_box)

        self.file_label = QLabel("No file opened")
        file_layout.addWidget(self.file_label)

        self.calib_label = QLabel("No calibration file opened")
        file_layout.addWidget(self.calib_label)

        self.open_btn = QPushButton("Open ROOT file")
        self.open_btn.clicked.connect(self.open_file)
        file_layout.addWidget(self.open_btn)

        self.open_calib_btn = QPushButton("Open calibration file")
        self.open_calib_btn.clicked.connect(self.open_calib_file)
        file_layout.addWidget(self.open_calib_btn)

        self.subtract_pedestal = QCheckBox("Subtract pedestal")
        self.subtract_pedestal.setChecked(False)
        self.subtract_pedestal.setEnabled(False)
        file_layout.addWidget(self.subtract_pedestal)

        self.layout.addWidget(file_box)

        # ---------- Tree Selector ----------
        self.tree_combo = QComboBox()
        self.tree_combo.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.tree_combo.currentTextChanged.connect(self.change_tree)
        self.tree_combo.setEnabled(False)
        self.layout.addWidget(self.tree_combo)

        # ---------- Matplotlib Figure ----------
        self.fig = Figure()
        self.canvas = FigureCanvas(self.fig)
        self.canvas.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.layout.addWidget(self.canvas)

        # Initial plot
        ax = self.fig.add_subplot(111)
        ax.grid(True, alpha=0.2)
        self.canvas.draw()

        # ---------- Event Navigation ----------
        nav_layout = QHBoxLayout()
        self.event_selector_label = QLabel("Event:")
        self.event_selector_label.setFixedWidth(50)
        nav_layout.addWidget(self.event_selector_label)

        self.event_selector = QLineEdit()
        self.event_selector.setFixedWidth(80)
        self.event_selector.setPlaceholderText("0")
        self.event_selector.textChanged.connect(self.change_event)
        nav_layout.addWidget(self.event_selector)

        self.prev_btn = QPushButton("Previous")
        self.prev_btn.clicked.connect(self.prev_event)
        self.prev_btn.setEnabled(False)
        nav_layout.addWidget(self.prev_btn)

        self.next_btn = QPushButton("Next")
        self.next_btn.clicked.connect(self.next_event)
        self.next_btn.setEnabled(False)
        nav_layout.addWidget(self.next_btn)

        nav_box = QGroupBox("Event Navigation")
        nav_box.setLayout(nav_layout)
        self.layout.addWidget(nav_box)

        # ---------- Zoom Controls ----------
        zoom_layout = QHBoxLayout()
        for w in ("xmin_input", "xmax_input", "ymin_input", "ymax_input"):
            setattr(self, w, QLineEdit())
            getattr(self, w).setFixedWidth(80)
            getattr(self, w).setPlaceholderText(w.replace("_input", "").upper())

        zoom_layout.addWidget(self.xmin_input)
        zoom_layout.addWidget(self.xmax_input)
        zoom_layout.addWidget(self.ymin_input)
        zoom_layout.addWidget(self.ymax_input)

        self.zoom_btn = QPushButton("Apply Zoom")
        self.zoom_btn.clicked.connect(self.apply_zoom)
        zoom_layout.addWidget(self.zoom_btn)

        self.reset_zoom_btn = QPushButton("Reset Zoom")
        self.reset_zoom_btn.clicked.connect(self.reset_zoom)
        zoom_layout.addWidget(self.reset_zoom_btn)

        zoom_box = QGroupBox("Zoom")
        zoom_box.setLayout(zoom_layout)
        self.layout.addWidget(zoom_box)

        # ---------- Screenshot ----------
        screenshot_layout = QHBoxLayout()
        self.save_btn = QPushButton("Save Screenshot")
        self.save_btn.setEnabled(False)
        self.save_btn.clicked.connect(self.save_screenshot)
        screenshot_layout.addWidget(self.save_btn)
        self.layout.addLayout(screenshot_layout)

        # ---------- UDP Controls ----------
        udp_layout = QHBoxLayout()
        self.udp_btn = QPushButton("Start UDP")
        self.udp_btn.clicked.connect(self.toggle_udp)
        udp_layout.addWidget(self.udp_btn)

        self.udp_select = QComboBox()
        self.udp_select.addItems(["J5", "J7"])
        udp_layout.addWidget(self.udp_select)

        udp_box = QGroupBox("UDP Stream")
        udp_box.setLayout(udp_layout)
        self.layout.addWidget(udp_box)

        # ---------- Redraw Timer ----------
        self.redraw_timer = QTimer(self)
        self.redraw_timer.setInterval(40)
        self.redraw_timer.timeout.connect(self._redraw_if_pending)
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

    # ----------------- UDP -----------------
    # NOTE: Only UDP stream containing 1 board is supported at the moment
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
        
        # Disable offline part of the viewer
        
        # Disable open file buttons
        self.open_btn.setEnabled(False)

        # Disable tree and event navigation
        self.tree_combo.setEnabled(False)
        self.event_selector.setEnabled(False)
        self.prev_btn.setEnabled(False)
        self.next_btn.setEnabled(False)
        self.event_selector_label.setEnabled(False)
        
        # Disable zoom controls
        self.xmin_input.setEnabled(False)
        self.xmax_input.setEnabled(False)
        self.ymin_input.setEnabled(False)
        self.ymax_input.setEnabled(False)
        self.zoom_btn.setEnabled(False)
        self.reset_zoom_btn.setEnabled(False)
        
        self.udp_running = True
        self.udp_stop_event.clear()
        self.udp_thread = threading.Thread(target=self.udp_loop, daemon=True)
        self.udp_thread.start()

    def stop_udp(self):
        if not self.udp_running:
            return
        
        # Re-enable offline part of the viewer

        # Enable open file buttons
        self.open_btn.setEnabled(True)

        # Enable tree and event navigation
        self.tree_combo.setEnabled(True)
        self.event_selector.setEnabled(True)
        self.prev_btn.setEnabled(True)
        self.next_btn.setEnabled(True)
        self.event_selector_label.setEnabled(True)

        # Enable zoom controls
        self.xmin_input.setEnabled(True)
        self.xmax_input.setEnabled(True)
        self.ymin_input.setEnabled(True)
        self.ymax_input.setEnabled(True)
        self.zoom_btn.setEnabled(True)        
        self.reset_zoom_btn.setEnabled(True)
        
        self.udp_stop_event.set()
        self.udp_running = False
        if self.udp_thread:
            self.udp_thread.join(timeout=1)
            self.udp_thread = None

    def udp_loop(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.settimeout(0.2)

        in_event = False
        in_board = False
        board_words = []

        while not self.udp_stop_event.is_set():
            try:
                data, _ = sock.recvfrom(BUF_SIZE)
            except socket.timeout:
                continue

            n = len(data) // 4
            for i in range(n):
                w = int.from_bytes(data[4 * i:4 * i + 4], "little")

                if w == EVENT_START:
                    in_event = True
                    in_board = False
                    board_words.clear()
                    continue

                if not in_event:
                    continue

                if w == BOARD_START:
                    in_board = True
                    board_words.clear()
                    continue

                if w == BOARD_END and in_board:
                    channels = reorder(decode_board(board_words[8:]))
                    ch0 = np.array(channels[:640], dtype=np.int16)
                    ch1 = np.array(channels[640:1280], dtype=np.int16)

                    self.udp_event_id += 1
                    self.udp_ch0 = ch0
                    self.udp_ch1 = ch1
                    self.udp_pending = True

                    in_event = False
                    in_board = False
                    board_words.clear()
                    continue

                if in_board:
                    board_words.append(w)

        sock.close()

    def _redraw_if_pending(self):
        if not self.udp_pending:
            return

        ax = self.fig.axes[0]
        selection = self.udp_select.currentText()

        if self.udp_line0 is None or self.udp_line1 is None:
            x = np.arange(640)
            self.udp_line0, = ax.plot(x, np.zeros_like(x), label="J7")
            self.udp_line1, = ax.plot(x, np.zeros_like(x), label="J5")
            ax.set_xlabel("Channel")
            ax.set_ylabel("ADC count")
                        
            ax.set_xticks(np.arange(0, 640, 64))
            ax.set_xticklabels(np.arange(0, 640, 64))
            
            ax.grid(True, alpha=0.2)
            
        x = np.arange(640)

        if selection == "J7":
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 0]["pedestal"].to_numpy()
                if len(pedestal_values) == len(self.udp_ch0):
                    self.udp_ch0 = self.udp_ch0 - pedestal_values

            self.udp_line0.set_data(x, self.udp_ch0)
            self.udp_line1.set_data(x, np.zeros_like(x))
            self.udp_line0.set_visible(True)
            self.udp_line1.set_visible(False)
            ax.set_title(f"UDP Event {self.udp_event_id} (J7)")

        else:  # J5 only
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 1]["pedestal"].to_numpy()
                if len(pedestal_values) == len(self.udp_ch1):
                    self.udp_ch1 = self.udp_ch1 - pedestal_values

            self.udp_line0.set_data(x, np.zeros_like(x))
            self.udp_line1.set_data(x, self.udp_ch1)
            self.udp_line0.set_visible(False)
            self.udp_line1.set_visible(True)
            ax.set_title(f"UDP Event {self.udp_event_id} (J5)")


        # Set axes limits
        if self.xmin_input.text() == "" or self.xmax_input.text() == "":
            ax.set_xlim(0, 639)
        else:
            ax.set_xlim(float(self.xmin_input.text()), float(self.xmax_input.text()))

        if self.ymin_input.text() == "" or self.ymax_input.text() == "":
            ax.set_ylim(-200, 500)
        else:
            ax.set_ylim(float(self.ymin_input.text()), float(self.ymax_input.text()))

        self.canvas.draw_idle()
        self.udp_pending = False


    # ----------------- File & Tree -----------------
    def open_file(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open ROOT file", "", "ROOT Files (*.root)")
        if not file_path:
            return

        self.current_file = file_path
        self.file_label.setText(f"Opened file: {self.current_file}")

        root_file = ROOT.TFile(file_path)
        tree_keys = [key for key in root_file.GetListOfKeys() if key.GetClassName() == "TTree"]

        self.trees = {}
        for key in tree_keys:
            tree = root_file.Get(key.GetName())
            if tree.GetBranch("RAW Event J7"):
                self.trees[key.GetName()] = [np.array(event.__getattr__("RAW Event J7"), dtype=np.uint32) for event in tree]
            else:
                self.trees[key.GetName()] = [np.array(event.__getattr__("RAW Event J5"), dtype=np.uint32) for event in tree]

        self.tree_combo.blockSignals(True)
        self.tree_combo.clear()
        self.tree_combo.addItems(list(self.trees.keys()))
        self.tree_combo.blockSignals(False)
        self.tree_combo.setEnabled(bool(self.trees))
        self.prev_btn.setEnabled(bool(self.trees))
        self.next_btn.setEnabled(bool(self.trees))
        self.save_btn.setEnabled(bool(self.trees))

        if self.trees:
            self.current_tree = list(self.trees.keys())[0]
            self.events = self.trees[self.current_tree]
            self.index = 0
            self.plot_event()

    def open_calib_file(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open calibration file", "", "Calibration Files (*.cal)")
        if not file_path:
            return

        self.current_calib_file = file_path
        self.calib_label.setText(f"Opened calibration file: {self.current_calib_file}")

        self.calib_df = self.read_calibration_file(file_path)
        self.subtract_pedestal.setEnabled(True)

    def change_tree(self, tree_name):
        if tree_name not in self.trees:
            return
        self.current_tree = tree_name
        self.events = self.trees[self.current_tree]
        self.index = 0
        self.plot_event()

    def change_event(self, text):
        if not text.isdigit():
            return
        idx = int(text) - 1
        max_idx = len(self.events) - 1
        if max_idx < 0:
            return
        if idx < 0:
            idx = 0
        elif idx > max_idx:
            idx = max_idx
        if idx == self.index:
            return
        self.index = idx
        self.event_selector.blockSignals(True)
        self.event_selector.setText(str(self.index + 1))
        self.event_selector.blockSignals(False)
        self.plot_event()

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

    # ----------------- Plotting -----------------
    def plot_event(self):
        ax = self.fig.axes[0]
        ax.clear()

        data = self.events[self.index]
        if self.calib_df is not None and self.subtract_pedestal.isChecked():
            current_tree_idx = self.tree_combo.currentIndex()
            pedestal_values = self.calib_df[self.calib_df["name"] == current_tree_idx]["pedestal"].to_numpy()
            if len(pedestal_values) == len(data):
                data = data - pedestal_values
            ax.set_ylabel("ADC count (pedestal subtracted)")
            title_suffix = " (Pedestal Subtracted)"
        else:
            ax.set_ylabel("ADC count")
            title_suffix = ""

        ax.plot(data, marker='o', linestyle='-')
        ax.set_xlabel("Channel")
        ax.set_title(f"{self.current_tree} - Event {self.index+1} / {len(self.events)}{title_suffix}")
        ax.grid(True, alpha=0.2)

        self.update_xticks(ax, data)

        self.canvas.draw_idle()
        self.event_selector.blockSignals(True)
        self.event_selector.setText(str(self.index + 1))
        self.event_selector.blockSignals(False)

    def update_xticks(self, ax, data):
        n_channels = len(data)
        if n_channels > 64:
            major_ticks = list(range(0, n_channels, 64))
            major_ticks.append(n_channels - 1)
        else:
            major_ticks = [0, 32, 64]
        ax.set_xticks(major_ticks)
        ax.set_xlim(0, n_channels - 1)

    # ----------------- Zoom -----------------
    def apply_zoom(self):
        ax = self.fig.axes[0]
        data = self.events[self.index]
        try:
            xmin = float(self.xmin_input.text()) if self.xmin_input.text() else None
            xmax = float(self.xmax_input.text()) if self.xmax_input.text() else None
            ymin = float(self.ymin_input.text()) if self.ymin_input.text() else None
            ymax = float(self.ymax_input.text()) if self.ymax_input.text() else None
        except ValueError:
            return
        ax.set_xlim(left=xmin, right=xmax)
        ax.set_ylim(bottom=ymin, top=ymax)
        self.update_xticks(ax, data)
        self.canvas.draw_idle()

    def reset_zoom(self):
        ax = self.fig.axes[0]
        data = self.events[self.index]
        ax.set_xlim(0, len(data) - 1)
        ax.set_ylim(min(data) - 10, max(data) + 10)
        self.update_xticks(ax, data)
        self.canvas.draw_idle()

    # ----------------- Screenshot -----------------
    def save_screenshot(self):
        filename = "screenshot_" + self.current_file.split("/")[-1].split(".")[0] + "_" + self.current_tree + "_" + str(self.index+1) + ".png"
        self.fig.savefig(filename)

    # ----------------- Navigation -----------------
    def prev_event(self):
        if self.index > 0:
            self.index -= 1
            self.plot_event()

    def next_event(self):
        if self.index < len(self.events) - 1:
            self.index += 1
            self.plot_event()

    def closeEvent(self, event):
        self.stop_udp()
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    viewer = EventViewer()
    viewer.show()
    sys.exit(app.exec())
