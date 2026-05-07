"""Microstrip Raw Data Viewer with UDP Streaming Support"""

import sys
import numpy as np
import pandas as pd
import socket
import threading
import itertools

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QPushButton, QLabel,
    QHBoxLayout, QComboBox, QSizePolicy, QFileDialog, QCheckBox,
    QGroupBox, QTabWidget
)

import pyqtgraph as pg
pg.setConfigOptions(antialias=False, useOpenGL=True)


UDP_IP   = "127.0.0.1"
UDP_PORT = 8890
BUF_SIZE = 65535

EVENT_START = 0xfa4af1ca
BOARD_START = 0xbaba1a9a
BOARD_END   = 0x0bedface

def reorder(v):
    reordered = [0] * len(v)
    j = 0
    order = [1, 0, 3, 2, 5, 4, 7, 6, 9, 8]
    for ch in range(128):
        for adc in order:
            reordered[adc * 128 + ch] = v[j]
            j += 1
    return reordered

def decode_board(words):
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

        self.udp_1d_pending = False
        self.udp_ch0 = None
        self.udp_ch1 = None

        self.udp_line0 = None
        self.udp_line1 = None

        # Accumulation state
        self.udp_accum_sum0 = None
        self.udp_accum_sum1 = None
        self.udp_accum_count = 0

        # 2D scatter — staging buffer written by UDP thread, drained by GUI thread
        self.udp_2d_pending = False
        self._2d_staging = []
        self._2d_staging_lock = threading.Lock()
        self.scatter_x = np.empty(0, dtype=np.float32)
        self.scatter_y = np.empty(0, dtype=np.float32)

        self.COLUMNS = [
            "channel", "va_id", "va_channel", "pedestal",
            "sigma_raw", "sigma", "flag", "extra",
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

        # ---------- Tab Widget ----------
        self.tabs = QTabWidget()
        self.layout.addWidget(self.tabs)

        # ---- Tab 1: Single Board ----
        self.single_board_tab = QWidget()
        sb_layout = QVBoxLayout(self.single_board_tab)
        sb_layout.setContentsMargins(0, 6, 0, 0)
        sb_layout.setSpacing(8)

        # PyQtGraph plot replacing matplotlib
        self.pg_1d = pg.PlotWidget()
        self.pg_1d.setBackground('w')
        self.pg_1d.setLabel('bottom', 'Channel')
        self.pg_1d.setLabel('left', 'ADC count')
        self.pg_1d.showGrid(x=True, y=True, alpha=0.2)
        self.pg_1d.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        # Pre-create two curve items (one per connector); only one is shown at a time
        x_init = np.arange(640)
        y_init = np.zeros(640)
        self.udp_line0 = self.pg_1d.plot(x_init, y_init,
                                          pen=pg.mkPen('#1e64c8', width=1),
                                          name='J7')
        self.udp_line1 = self.pg_1d.plot(x_init, y_init,
                                          pen=pg.mkPen('#e05c1a', width=1),
                                          name='J5')
        self.udp_line1.hide()

        # X ticks every 64 channels
        x_ticks = [(i, str(i)) for i in range(0, 640, 64)]
        self.pg_1d.getAxis('bottom').setTicks([x_ticks])

        sb_layout.addWidget(self.pg_1d)
        
        self.tabs.addTab(self.single_board_tab, "Single board")

        # ---- Tab 2: 2D ----
        self.tab_2d = QWidget()
        layout_2d = QVBoxLayout(self.tab_2d)
        layout_2d.setContentsMargins(4, 4, 4, 4)
        layout_2d.setSpacing(4)

        clear_btn = QPushButton("Clear")
        clear_btn.setFixedWidth(80)
        clear_btn.clicked.connect(self.clear_2d)
        layout_2d.addWidget(clear_btn, alignment=Qt.AlignmentFlag.AlignLeft)

        self.pg_plot = pg.PlotWidget()
        self.pg_plot.setBackground('w')
        self.pg_plot.setLabel('bottom', 'J7 max (ADC)')
        self.pg_plot.setLabel('left', 'J5 max (ADC)')
        self.pg_plot.setTitle('Board 0 — J7 max vs J5 max')
        self.pg_plot.showGrid(x=True, y=True, alpha=0.2)
        self.pg_scatter = pg.ScatterPlotItem(
            size=4, pen=None, brush=pg.mkBrush(30, 100, 200, 150)
        )
        self.pg_plot.addItem(self.pg_scatter)
        layout_2d.addWidget(self.pg_plot)

        self.tabs.addTab(self.tab_2d, "2D")
        
        udp_layout = QHBoxLayout()
        self.udp_btn = QPushButton("Start UDP")
        self.udp_btn.clicked.connect(self.toggle_udp)
        udp_layout.addWidget(self.udp_btn)

        self.udp_select_board_label = QLabel("Board:")
        self.udp_select_board_label.setFixedWidth(50)
        self.udp_select_board_label.setEnabled(False)
        udp_layout.addWidget(self.udp_select_board_label)

        self.udp_select_board = QComboBox()
        for i in range(12):
            self.udp_select_board.addItem(f"{i}")
        self.udp_select_board.setCurrentIndex(0)
        self.udp_select_board.currentIndexChanged.connect(self._on_board_change)
        self.udp_select_board.setEnabled(False)
        udp_layout.addWidget(self.udp_select_board)

        self.udp_select_side = QComboBox()
        self.udp_select_side.addItems(["J5", "J7"])
        self.udp_select_side.setEnabled(False)
        udp_layout.addWidget(self.udp_select_side)

        self.accumulate_checkbox = QCheckBox("Accumulate")
        self.accumulate_checkbox.setChecked(False)
        self.accumulate_checkbox.setStyleSheet("QCheckBox { margin-left: auto; }")
        self.accumulate_checkbox.stateChanged.connect(self._on_accumulate_toggled)
        self.udp_select_side.setEnabled(False)
        udp_layout.addWidget(self.accumulate_checkbox)

        udp_box = QGroupBox("UDP Stream")
        udp_box.setLayout(udp_layout)
        self.layout.addWidget(udp_box)

        # ---------- Redraw Timer ----------
        self.redraw_timer = QTimer(self)
        self.redraw_timer.setInterval(40)
        self.redraw_timer.timeout.connect(self._redraw)
        self.redraw_timer.start()

        # ---------- Global Styles ----------
        app.setStyleSheet("""
        QWidget { background-color: #f6f7f8; color: #1f2933; font-size: 10pt; }
        QGroupBox { border: 1px solid #d1d5db; border-radius: 6px; margin-top: 8px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; font-weight: 600; }
        QPushButton { background-color: #e5e7eb; border: 1px solid #cbd5e1; padding: 4px 10px; border-radius: 4px; }
        QPushButton:hover { background-color: #dbeafe; }
        QLineEdit, QComboBox { background-color: white; border: 1px solid #cbd5e1; border-radius: 4px; padding: 2px 6px; }
        QTabWidget::pane { border: 1px solid #d1d5db; border-radius: 6px; }
        QTabBar::tab { background-color: #e5e7eb; border: 1px solid #cbd5e1; border-bottom: none;
            border-top-left-radius: 4px; border-top-right-radius: 4px; padding: 4px 14px; margin-right: 2px; font-weight: 600; }
        QTabBar::tab:selected { background-color: #f6f7f8; border-bottom: 1px solid #f6f7f8; }
        QTabBar::tab:hover:!selected { background-color: #dbeafe; }
        """)

    # ----------------- Board change -----------------
    def _on_board_change(self):
        self._reset_accumulation()
        self.udp_1d_pending = True
        self.udp_2d_pending = True
        self.clear_2d()
        _current_board = self.udp_select_board.currentText()
        self.pg_plot.setTitle("Board " + _current_board + " — J7 max vs J5 max")
        
    def _on_accumulate_toggled(self, state):
        if state == 0:
            self._reset_accumulation()
            self.udp_1d_pending = True

    def _reset_accumulation(self):
        self.udp_accum_sum0 = None
        self.udp_accum_sum1 = None
        self.udp_accum_count = 0

    # ----------------- 2D clear -----------------
    def clear_2d(self):
        self.scatter_x = np.empty(0, dtype=np.float32)
        self.scatter_y = np.empty(0, dtype=np.float32)
        with self._2d_staging_lock:
            self._2d_staging.clear()
        self.pg_scatter.setData([])

    # ----------------- UDP -----------------
    def toggle_udp(self):
        if self.udp_running:
            self.stop_udp()
            self.udp_btn.setText("Start UDP")
            self.udp_select_side.setEnabled(False)
            self.udp_select_board.setEnabled(False)
            self.accumulate_checkbox.setEnabled(False)
        else:
            self.start_udp()
            self.udp_btn.setText("Stop UDP")
            self.udp_select_side.setEnabled(True)
            self.udp_select_board.setEnabled(True)
            self.accumulate_checkbox.setEnabled(True)

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
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        sock.settimeout(0.2)

        in_event = False
        in_board = False
        words_read = 0
        num_boards = 0
        board_read = 0
        board_words = []
        all_boarddata = []

        while not self.udp_stop_event.is_set():
            try:
                data, _ = sock.recvfrom(BUF_SIZE)
            except socket.timeout:
                continue

            n = len(data) // 4
            for i in range(n):
                w = int.from_bytes(data[4 * i:4 * i + 4], "little")
                words_read += 1

                if w == EVENT_START:
                    in_event = True
                    in_board = False
                    board_read = 0
                    board_words.clear()
                    all_boarddata.clear()
                    continue

                if not in_event:
                    continue

                if in_event and words_read == 4:
                    num_boards = w & 0xFFF

                if w == BOARD_START:
                    board_read += 1
                    selected_board = self.udp_select_board.currentText()
                    if board_read == int(selected_board) + 1:
                        in_board = True
                        board_words.clear()
                        continue

                if w == BOARD_END and in_board and board_read == int(selected_board) + 1:
                    payload = board_words[8:]
                    if len(payload) != 640:
                        print(f"Board {board_read} skipped: expected 640 words, got {len(payload)}")
                        in_event = False
                        in_board = False
                        board_words.clear()
                        continue
                    channels = reorder(decode_board(payload))
                    ch0 = np.array(channels[:640], dtype=np.int32)
                    ch1 = np.array(channels[640:1280], dtype=np.int32)
                    all_boarddata.append((board_read, ch0, ch1))
                    
                    # Channels with value over threshold
                    _x_over = np.flatnonzero(ch0 > 328)
                    _y_over = np.flatnonzero(ch1 > 1000)
                    if len(_x_over)  == 0:
                        _x_over = [-1]
                    if len(_y_over)  == 0:
                        _y_over = [-1]

                    self.udp_event_id += 1
                    self.udp_ch0 = all_boarddata[-1][1].copy()
                    self.udp_ch1 = all_boarddata[-1][2].copy()
                    self.udp_1d_pending = True
                    
                    
                    with self._2d_staging_lock:
                        # Loop over all combinations of X and Y channels
                        for x, y in itertools.product(_x_over, _y_over):
                            self._2d_staging.append((float(x), float(y)))
                    self.udp_2d_pending = True

                    in_event = False
                    in_board = False
                    board_words.clear()
                    continue

                if in_board and board_read == int(selected_board) + 1:
                    board_words.append(w)

        sock.close()

    # ----------------- Redraw dispatcher -----------------
    def _redraw(self):
        if self.udp_1d_pending:
            self.redraw_1d_if_pending()
        if self.udp_2d_pending:
            self.redraw_2d_if_pending()

    # ----------------- Single board redraw -----------------
    def redraw_1d_if_pending(self):
        if not self.udp_1d_pending:
            return

        # Only render if the Single board tab is visible
        if self.tabs.currentWidget() is not self.single_board_tab:
            return

        selection = self.udp_select_side.currentText()
        board = self.udp_select_board.currentText()
        x = np.arange(640)
        accumulating = self.accumulate_checkbox.isChecked()

        if selection == "J7":
            ch0 = self.udp_ch0.copy()
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 2 * int(board)]["pedestal"].to_numpy()
                if len(pedestal_values) == len(ch0):
                    ch0 = ch0 - pedestal_values
            if accumulating:
                if self.udp_accum_sum0 is None:
                    self.udp_accum_sum0 = np.zeros(640, dtype=np.float64)
                self.udp_accum_sum0 += ch0
                self.udp_accum_count += 1
                plot_data = self.udp_accum_sum0 / self.udp_accum_count
                title = f"UDP Accumulated {self.udp_accum_count} events (J7)"
            else:
                plot_data = ch0
                title = f"UDP Event {self.udp_event_id} (J7)"
            self.udp_line0.setData(x, plot_data)
            self.udp_line0.show()
            self.udp_line1.hide()

        else:  # J5
            ch1 = self.udp_ch1.copy()
            if self.calib_df is not None and self.subtract_pedestal.isChecked():
                pedestal_values = self.calib_df[self.calib_df["name"] == 2 * int(board) + 1]["pedestal"].to_numpy()
                if len(pedestal_values) == len(ch1):
                    ch1 = ch1 - pedestal_values
            if accumulating:
                if self.udp_accum_sum1 is None:
                    self.udp_accum_sum1 = np.zeros(640, dtype=np.float64)
                self.udp_accum_sum1 += ch1
                self.udp_accum_count += 1
                plot_data = self.udp_accum_sum1 / self.udp_accum_count
                title = f"UDP Accumulated {self.udp_accum_count} events (J5)"
            else:
                plot_data = ch1
                title = f"UDP Event {self.udp_event_id} (J5)"
            self.udp_line1.setData(x, plot_data)
            self.udp_line1.show()
            self.udp_line0.hide()

        self.pg_1d.setTitle(title)
        self.pg_1d.setXRange(0, 639, padding=0)

        self.udp_1d_pending = False

    # ----------------- 2D scatter redraw -----------------
    def redraw_2d_if_pending(self):
        if not self.udp_2d_pending:
            return
        self.udp_2d_pending = False

        # Drain staging buffer (written by UDP thread)
        with self._2d_staging_lock:
            batch = self._2d_staging[:]
            self._2d_staging.clear()

        if not batch:
            return

        new_x = np.array([p[0] for p in batch], dtype=np.float32)
        new_y = np.array([p[1] for p in batch], dtype=np.float32)
        self.scatter_x = np.concatenate([self.scatter_x, new_x])
        self.scatter_y = np.concatenate([self.scatter_y, new_y])

        # Only render if 2D tab is visible
        if self.tabs.currentWidget() is not self.tab_2d:
            return

        self.pg_scatter.setData(
            x=self.scatter_x,
            y=self.scatter_y,
        )

    # ----------------- Calibration file -----------------
    def open_calib_file(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open calibration file", "", "Calibration Files (*.cal)")
        if not file_path:
            return
        self.current_calib_file = file_path
        self.calib_label.setText(f"Opened calibration file: {self.current_calib_file}")
        self.calib_df = self.read_calibration_file(file_path)
        self.subtract_pedestal.setEnabled(True)

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