import sys
import numpy as np
import ROOT
import pandas as pd

from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QPushButton, QLabel,
    QHBoxLayout, QComboBox, QSizePolicy, QFileDialog, QLineEdit, QCheckBox, QGroupBox
)

from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


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

        # Initial radioactive symbol plot
        ax = self.fig.add_subplot(111)
        x_arr = np.array([215.73, 219.69, 224.68, 228.48, 233.54, 237.34, 236.52, 237.76, 239.23, 241.1,
                        242.4, 243.9, 246.2, 247.04, 250.21, 251.12, 253.07, 255.16, 255.82, 256.69,
                        259.05, 261.17, 260.93, 261.21, 262.03, 264.96, 269.82, 270.24, 276.35, 278.68,
                        279.1, 287.54, 287.96, 296.4, 296.82, 305.26, 305.68, 314.12, 314.54, 322.98,
                        323.4, 328.79, 331.14, 332.26, 334.17, 335.64, 337.32, 338.28, 339.06, 340.4,
                        339.64, 341.12, 346.18, 349.98, 355.04, 358.84, 363.9, 367.7, 372.76, 376.56,
                        381.62, 385.42, 390.48, 394.62, 399.34, 403.14, 408.2, 410.44, 416.52, 417.54,
                        420.85, 423.32, 423.66, 424.81, 424.68, 425.28, 425.47])
        y_arr = np.array([434.64, 402.27, 455.17, 393.55, 460.92, 385.68, 172.06, 130.98, 216.79, 100.46,
                        471.23, 245.8, 379.93, 69.58, 271.81, 477.74, 44.57, 523.15, 375.05, 438.09,
                        297.44, 549.03, 408.59, 27.61, 358.92, 330.34, 576.98, 9.87, 485.86, 591.58,
                        -4.11, 598.82, -14.52, 600.04, -22.12, 597.93, -27.01, 590.53, -29.62, 576.15,
                        -30.55, 342.74, 549.92, -29.39, 385.7, 524.8, 317.81, 416.4, 495.34, 462.78,
                        438.88, -26.91, 312.98, -23.01, 312.79, -17.36, 314.93, -9.33, 319.96, 1.57,
                        327.19, 15.95, 337.01, 34.82, 349.47, 54.65, 364.1, 84.69, 118.56, 369.98,
                        151.17, 183.57, 341.11, 212.44, 300.71, 271.46, 241.77])

        ax.plot(x_arr, y_arr, marker='', linestyle='None', color='red')
        for xi, yi in zip(x_arr, y_arr):
            ax.text(xi, yi, '☢', fontsize=12, ha='right')

        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
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
        self.fig.clear()
        ax = self.fig.add_subplot(111)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        ax.grid(True, alpha=0.2)
        ax.set_facecolor("#ffffff")

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

        ax.plot(data, marker='o', linestyle='-', color='red')
        ax.set_xlabel("Channel")
        ax.set_title(f"{self.current_tree} - Event {self.index+1} / {len(self.events)}{title_suffix}")
        self.update_xticks(ax, data)
        self.fig.tight_layout()
        self.canvas.draw()
        self.event_selector.blockSignals(True)
        self.event_selector.setText(str(self.index + 1))
        self.event_selector.blockSignals(False)
        
        self.apply_zoom()

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
        self.canvas.draw()

    def reset_zoom(self):
        ax = self.fig.axes[0]
        data = self.events[self.index]
        ax.set_xlim(0, len(data) - 1)
        ax.set_ylim(min(data) - 10, max(data) + 10)
        self.update_xticks(ax, data)
        self.canvas.draw()

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


if __name__ == "__main__":
    app = QApplication(sys.argv)
    viewer = EventViewer()
    viewer.show()
    sys.exit(app.exec())
