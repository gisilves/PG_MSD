# pDUNE Oca Data Analyzer

Tools to convert, calibrate, analyze, and visualize NP02 beam plug tracker data.
DAQ repo: https://github.com/emanuele-villa/oca-pDUNE-DAQ

Data location: `/eos/user/e/evilla/dune/np02-beam-monitor`. Request access via emanuele.villa@cern.ch. Ask Emanuele for the web interface URL if preferred.

## Install

After cloning, initialize submodules and compile:

```bash
git clone https://github.com/emanuele-villa/oca-pDUNE-dataAnalyzer.git
cd oca-pDUNE-dataAnalyzer
./scripts/manage-submodules.sh --up
source scripts/compile.sh
```

Requirements:
- ROOT installed at `/usr/local/`
- `json.hpp` header available
- CMake >= 3.17, GCC >= 11 (or recent clang)

## Scripts overview

### Individual run analysis

- **runAna.sh**
	- Main script: convert, calibrate (if needed), and analyze run(s).
	- Calibration policy: use the most recent previous CAL run's `.cal`; if none, search forward; if current run is CAL, use its own `.cal`.
	- Handles BEAM and CAL run types automatically.
	- Usage: `./scripts/runAna.sh -f <run_number> -j json/ev-settings.json [-s <sigma_threshold>]`

- **bmRawToRootConverter.sh**
	- Convert `.dat` to ROOT only (raw data).

- **evtDisplay.sh**
	- Build and launch the interactive ROOT event display.

- **hitsVsSigma.sh**
	- Generate a Hits vs Sigma plot for a run.

- **runCalibration.sh**
	- Extract calibration from CAL runs (produces `.cal` file).
	- Usage: `./scripts/runCalibration.sh -f <first_run> [-l <last_run>] -j json/ev-settings.json`

### Beam run aggregation

- **beamReports.sh**
	- Aggregates BEAM runs by energy condition into grouped ROOT files.
	- Calls `groupBeam` to read all `*_clusters.root` files and group by timestamp and beam energy from `parameters/beam_settings.dat`.
	- Outputs `*GeV.root` files (one per unique energy condition).
	- Generates SPS-mode reports for each grouped beam file.
	- Usage: `./scripts/beamReports.sh -j json/ev-settings.json`

## Apps

- PAPERO_convert: raw `.dat` → ROOT
- calibration: produce channel baseline/sigma/mask `.cal`
- dataAnalyzer: main analysis producing per-run PDF and ROOT outputs
- event_display: GUI to browse events; Prev/Next, larger UI; 3 detectors (A,B,C)
- hits_vs_sigma: counts total hits across events for a sweep of sigma thresholds, with calibration-bad and edge channel masking; saves a PDF

## Usage examples

Analyze a single run by number:
```bash
./scripts/runAna.sh -f 275 -j json/ev-settings.json
```

Convert a run by number:
```bash
./scripts/bmRawToRootConverter.sh -j json/ev-settings.json -r 275
```

Open event display on a run (auto-build; sigma via CLI):
```bash
./scripts/evtDisplay.sh -j json/ev-settings.json -r 275 -s 7
```

Hits vs Sigma (sweep 1..15):
```bash
./scripts/hitsVsSigma.sh -r 275 -j json/ev-settings.json --smin 1 --smax 15 --sstep 1
```

### Beam run aggregation workflow

Once individual BEAM runs have been analyzed with `runAna.sh`, aggregate them by beam energy:

```bash
./scripts/beamReports.sh -j json/ev-settings.json
```

This:
1. Groups all `*_BEAM_*_clusters.root` files by energy condition (from `parameters/beam_settings.dat`).
2. Outputs one ROOT file per energy (e.g., `+1.0GeV.root`, `+3.0GeV.root`).
3. Generates SPS-mode reports for each grouped beam file.

## Notes

- Detectors: active 3 (A,B,C). D is ignored.
- Channel count: 384; edge channels (first/last of each 64-chan ASIC) are masked in viewers/plots.
- `findRun.sh` centralizes run finding (pattern: `SCD_RUN<5d>_*.dat`).

## Report summary changes

This tool now produces a cleaner, more compact multi‑page PDF with consistent pagination, clearer annotations, and improved tracking visuals.

What’s new (Sept 2025):
- Page numbers printed bottom‑right on every kept page.
- CEST date/time shown directly under the title; filename timestamp is robustly parsed and shifted +2h.
- Summary tables: two non‑overlapping blocks with averages per spill (10 s windows) and per event (histogram means). Removed deprecated “total clusters per spill”.
- 2D tracking heatmaps (reconstructed centers):
	- Axis ranges standardized to X ∈ [−80, 60] mm and Y ∈ [−60, 70] mm.
	- Active area polygon (all 3 detectors) drawn in black; legend refined and placed within the plot.
	- Stats box repositioned to lie within x ∈ [30, 60] mm, away from the color palette and above the active area; its height is increased by 3× for readability.
- Timestamps page (2×2) restored and moved earlier in the document; Δt between consecutive events moved into this page (bottom‑right), fitted with an exponential to extract τ, displayed on‑plot.
- Clusters‑per‑event plot x‑axis capped at 0..6 with percentage labels above bars.
- Baseline and Sigma merged into a concise two‑panel page with clearer legends.
- Removed obsolete pages (hits‑per‑event, amplitude overlays); the final page is now the firing channels summary.

Additional notes:
- Scatter‑only reconstructed‑center pages for exactly‑3 and 2‑or‑3 clusters remain available; empty graphs are safely skipped.
- Geometry plane offsets come from `parameters/geometry.json` and can be tuned as needed.

## Data processing pipeline

### Run types

Runs are classified by filename:

- **BEAM runs** (contain `_BEAM_` in filename): Physics data with beam passing through detector. Analysis:
  - Convert raw `.dat` to ROOT (calibration applied from previous CAL run).
  - Cluster hits above threshold.
  - Generate per-run PDF reports.
  - Output: `*_converted.root`, `*_formatted.root`, `*_clusters.root`, `*_report.pdf`.

- **CAL runs** (contain `CAL` in filename): Calibration runs. Analysis:
  - Convert raw `.dat` to ROOT via `PAPERO_convert`.
  - Extract per-channel baseline and noise (sigma) via `calibration` app.
  - Output: `*.cal` file (used for subsequent BEAM runs).
  - Can be done manually via `runCalibration.sh` or automatically by `runAna.sh` when processing CAL runs.

### Processing steps for BEAM runs

When running `./scripts/runAna.sh -f <run_number> -j settings.json`:

1. **Conversion**: `flat_convert` converts raw `.dat` to ROOT with channels reorganized into detector planes.
   - Input: raw `.dat` file
   - Calibration: uses `.cal` file from preceding CAL run
   - Output: `*_converted.root`

2. **Formatting**: `formatting` applies calibration offsets and reformats for clustering.
   - Input: `*_converted.root`, `.cal` file
   - Output: `*_formatted.root`

3. **Clustering**: `clustering` groups adjacent hits above threshold (sigma).
   - Input: `*_formatted.root`, sigma threshold (default 5σ, configurable)
   - Output: `*_clusters.root` (cluster metadata + waveform data)

4. **Report**: `runReport` generates PDF summary with histograms, heatmaps, tracking plots.
   - Input: `*_clusters.root`
   - Output: `*_report.pdf`

### Beam aggregation (SPS mode)

Once individual BEAM runs are analyzed, `./scripts/beamReports.sh` aggregates them:

1. **Grouping** (`groupBeam`):
   - Reads all `*_BEAM_*_clusters.root` files.
   - Matches events by timestamp to `parameters/beam_settings.dat`.
   - Groups events into buckets by beam energy (e.g., all +1.0 GeV runs together).
   - Filters out problematic runs (hardcoded blacklist in code).
   - Validates cluster data (rejects oversized or NaN values).
   - Output: `+1.0GeV.root`, `+3.0GeV.root`, etc. (one file per energy condition).

2. **SPS Reports** (`runReport --sps-run`):
   - Runs per grouped file to produce aggregated energy-specific PDFs.
   - Output: `*GeV_report.pdf`

### Beam settings configuration

`parameters/beam_settings.dat` maps time windows to beam energies. Format (pipe-delimited):

```
Date       | Time  | Energy | Target   | Cherenkov | Collimator | Details
2025-08-11 | 16:24 | +1.0   | -        | -         | -          | -
```

The `groupBeam` app uses this to classify events by energy even if filename doesn't distinguish.


## Configuration

Runtime settings live in `json/ev-settings.json`.

Geometry plane offsets are stored in `parameters/geometry.json`.
Example `parameters/geometry.json`:

```
{
	"planeOffsetsMm": [
		[ -40.0, 0.0 ],
		[ -40.0, 0.0 ],
		[ -40.0, 0.0 ]
	]
}
```
