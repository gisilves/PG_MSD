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

        self.setWindowTitle("Microstrip Online Event Viewer")
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
        
        self.udp_select_board = QComboBox()
        self.udp_select_board_label = QLabel("Board:")
        self.udp_select_board_label.setFixedWidth(50)
        udp_layout.addWidget(self.udp_select_board_label)
        
        for i in range(12):
            self.udp_select_board.addItem(f"{i}")
        self.udp_select_board.setCurrentIndex(0)
        udp_layout.addWidget(self.udp_select_board)
        
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
        # Open UDP socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.settimeout(0.2)

        in_event = False
        in_board = False
        words_read = 0
        num_boards = 0
        board_read = 0
        board_words = []

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
                    in_board = False
                    board_read = 0 # Reset board read counter
                    board_words.clear() # Clear board words buffer
                    continue

                if not in_event:
                    continue
                
                # Read number of boards in the event
                if in_event and words_read == 4:
                    num_boards = w & 0xFFF
                
                if w == BOARD_START:
                    board_read += 1
                    
                    # Check if we are at the correct board number based on the dropdown selection
                    selected_board = self.udp_select_board.currentText()
                    if board_read == int(selected_board) + 1:
                        in_board = True
                        board_words.clear()
                        continue

                if w == BOARD_END and in_board and board_read == int(selected_board) + 1:
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

                if in_board and board_read == int(selected_board) + 1:
                    board_words.append(w)

        sock.close()

    def _redraw_if_pending(self):
        if not self.udp_pending:
            return

        ax = self.fig.axes[0]
        selection = self.udp_select.currentText()
        board = self.udp_select_board.currentText()

        if self.udp_line0 is None or self.udp_line1 is None:
            x = np.arange(640)
            self.udp_line0, = ax.plot(x, np.zeros_like(x), label="Board" + board + " J7")
            self.udp_line1, = ax.plot(x, np.zeros_like(x), label="Board" + board + " J5")
            ax.set_xlabel("Channel")
            ax.set_ylabel("ADC count")
                        
            ax.set_xticks(np.arange(0, 640, 64))
            ax.set_xticklabels(np.arange(0, 640, 64))
            
            ax.grid(True, alpha=0.2)
            
        x = np.arange(640)

        if selection == "J7":
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 2 * int(board) ]["pedestal"].to_numpy()
                if len(pedestal_values) == len(self.udp_ch0):
                    self.udp_ch0 = self.udp_ch0 - pedestal_values

            self.udp_line0.set_data(x, self.udp_ch0)
            self.udp_line1.set_data(x, np.zeros_like(x))
            self.udp_line0.set_visible(True)
            self.udp_line1.set_visible(False)
            ax.set_title(f"UDP Event {self.udp_event_id} (J7)")

        else:  # J5 only
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 2 * int(board) + 1]["pedestal"].to_numpy()
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

    # ----------------- Screenshot -----------------
    def save_screenshot(self):
        filename = "screenshot_" + self.current_file.split("/")[-1].split(".")[0] + "_" + self.current_tree + "_" + str(self.index+1) + ".png"
        self.fig.savefig(filename)
        
if __name__ == "__main__":
    app = QApplication(sys.argv)
    viewer = EventViewer()
    viewer.show()
    sys.exit(app.exec())
