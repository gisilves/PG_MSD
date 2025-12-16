import sys
import numpy as np
import ROOT

from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QPushButton, QLabel,
    QHBoxLayout, QComboBox, QSizePolicy, QFileDialog, QLineEdit
)

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure


class EventViewer(QWidget):
    def __init__(self):
        super().__init__()
        self.trees = {}
        self.current_tree = None
        self.events = []
        self.index = 0
        self.current_file = ""

        self.setWindowTitle("Microstrip Event Viewer")
        self.layout = QVBoxLayout(self)

        # File label
        self.file_label = QLabel("No file opened")
        self.layout.addWidget(self.file_label)

        # Button to open ROOT file
        self.open_btn = QPushButton("Open ROOT file")
        self.open_btn.clicked.connect(self.open_file)
        self.layout.addWidget(self.open_btn)

        # Tree selector
        self.tree_combo = QComboBox()
        self.tree_combo.currentTextChanged.connect(self.change_tree)
        self.layout.addWidget(self.tree_combo)

        # Event label
        self.label = QLabel("Event: 0/0")
        self.layout.addWidget(self.label)

        # Matplotlib figure
        self.fig = Figure()
        self.canvas = FigureCanvas(self.fig)
        self.canvas.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.canvas.updateGeometry()
        self.layout.addWidget(self.canvas)
        
        # Initial plot
        x_arr = np.array([215.73, 219.69, 224.68, 228.48, 233.54, 237.34, 236.52, 237.76, 239.23, 241.1, 242.4, 243.9, 246.2, 247.04, 250.21, 251.12, 253.07, 255.16,
                      255.82, 256.69, 259.05, 261.17, 260.93, 261.21, 262.03, 264.96, 269.82, 270.24, 276.35, 278.68, 279.1, 287.54, 287.96, 296.4, 296.82, 305.26,
                      305.68, 314.12, 314.54, 322.98, 323.4, 328.79, 331.14, 332.26, 334.17, 335.64, 337.32, 338.28, 339.06, 340.4, 339.64, 341.12, 346.18, 349.98,
                      355.04, 358.84, 363.9, 367.7, 372.76, 376.56, 381.62, 385.42, 390.48, 394.62, 399.34, 403.14, 408.2, 410.44, 416.52, 417.54, 420.85, 423.32,
                      423.66, 424.81, 424.68, 425.28, 425.47])
        
        y_arr = np.array([434.64, 402.27, 455.17, 393.55, 460.92, 385.68, 172.06, 130.98, 216.79, 100.46, 471.23, 245.8, 379.93, 69.58, 271.81, 477.74, 44.57, 523.15,
                      375.05, 438.09, 297.44, 549.03, 408.59, 27.61, 358.92, 330.34, 576.98, 9.87, 485.86, 591.58, -4.11, 598.82, -14.52, 600.04, -22.12, 597.93,
                      -27.01, 590.53, -29.62, 576.15, -30.55, 342.74, 549.92, -29.39, 385.7, 524.8, 317.81, 416.4, 495.34, 462.78, 438.88, -26.91, 312.98, -23.01,
                      312.79, -17.36, 314.93, -9.33, 319.96, 1.57, 327.19, 15.95, 337.01, 34.82, 349.47, 54.65, 364.1, 84.69, 118.56, 369.98, 151.17, 183.57, 341.11,
                      212.44, 300.71, 271.46, 241.77])
        
        ax = self.fig.add_subplot(111)
        ax.plot(x_arr, y_arr, marker='o', color='red', linestyle='None')
       
        # Navigation buttons
        nav_layout = QHBoxLayout()
        self.prev_btn = QPushButton("Previous")
        self.next_btn = QPushButton("Next")
        self.save_btn = QPushButton("Save Screenshot")
        self.prev_btn.clicked.connect(self.prev_event)
        self.next_btn.clicked.connect(self.next_event)
        self.save_btn.clicked.connect(self.save_screenshot)
        nav_layout.addWidget(self.prev_btn)
        nav_layout.addWidget(self.next_btn)
        nav_layout.addWidget(self.save_btn)
        self.layout.addLayout(nav_layout)

        # Zoom controls
        zoom_layout = QHBoxLayout()
        self.xmin_input = QLineEdit()
        self.xmin_input.setPlaceholderText("X min")
        self.xmax_input = QLineEdit()
        self.xmax_input.setPlaceholderText("X max")
        self.ymin_input = QLineEdit()
        self.ymin_input.setPlaceholderText("Y min")
        self.ymax_input = QLineEdit()
        self.ymax_input.setPlaceholderText("Y max")
        self.zoom_btn = QPushButton("Apply Zoom")
        self.zoom_btn.clicked.connect(self.apply_zoom)
        self.reset_zoom_btn = QPushButton("Reset Zoom")
        self.reset_zoom_btn.clicked.connect(self.reset_zoom)

        zoom_layout.addWidget(self.xmin_input)
        zoom_layout.addWidget(self.xmax_input)
        zoom_layout.addWidget(self.ymin_input)
        zoom_layout.addWidget(self.ymax_input)
        zoom_layout.addWidget(self.zoom_btn)
        zoom_layout.addWidget(self.reset_zoom_btn)
        self.layout.addLayout(zoom_layout)

    def update_xticks(self, ax, data):
        n_channels = len(data)
        
        if n_channels > 64:
            major_ticks = list(range(0, n_channels, 64))
            major_ticks.append(n_channels)
        else:
            major_ticks = [0, 32, 64]

        ax.set_xticks(major_ticks)
        ax.set_xlim(0, n_channels - 1)
        ax.grid(True, which='major', linestyle='--', linewidth=0.5)

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
            
            # Branch can be RAW Event J5 or RAW Event J7
            if tree.GetBranch("RAW Event J7"):
                self.trees[key.GetName()] = [np.array(event.__getattr__("RAW Event J7"), dtype=np.uint32) for event in tree]
            else:
                self.trees[key.GetName()] = [np.array(event.__getattr__("RAW Event J5"), dtype=np.uint32) for event in tree]

        self.tree_combo.blockSignals(True)
        self.tree_combo.clear()
        self.tree_combo.addItems(list(self.trees.keys()))
        self.tree_combo.blockSignals(False)

        if self.trees:
            self.current_tree = list(self.trees.keys())[0]
            self.events = self.trees[self.current_tree]
            self.index = 0
            self.plot_event()

    def change_tree(self, tree_name):
        if tree_name not in self.trees:
            return
        self.current_tree = tree_name
        self.events = self.trees[self.current_tree]
        self.index = 0
        self.plot_event()

    # ----------------- Plotting -----------------
    def plot_event(self):
        self.fig.clear()
        ax = self.fig.add_subplot(111)
        data = self.events[self.index]
        ax.plot(data, marker='o')
        ax.set_xlabel("Channel")
        ax.set_ylabel("ADC count")
        ax.set_title(f"{self.current_tree} - Event {self.index+1}")

        ax.set_xlim(0, len(data) - 1)
        ax.set_ylim(min(data) - 10, max(data) + 10)

        self.update_xticks(ax, data)

        self.fig.tight_layout()
        self.canvas.draw()
        self.label.setText(f"Event: {self.index+1}/{len(self.events)}")

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
        
    def save_screenshot(self):
        # Filename for screenshot: opened file name (without extension) + tree name + event number
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
