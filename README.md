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

Settings are read from a JSON (e.g., `json/ev-settings.json`) with `inputDirectory` and `outputDirectory`.

On lxplus (recommended), all dependencies are available when sourcing the environment via `scripts/init.sh`, which is invoked by the other scripts; you usually don't need to run it manually.

- analyze.sh
	- Batch convert, calibrate (if needed), and analyze run(s).
	- Calibration policy: use the most recent previous CAL run’s `.cal`; if none, use the nearest later CAL; if the current run is CAL, use its own `.cal`.
  - Thin wrapper delegating to `analyzeRun.sh`.

- analyzeRun.sh
	- Full implementation used by `analyze.sh`.

- bmRawToRootConverter.sh
	- Convert `.dat` to ROOT only.

- evtDisplay.sh
	- Build and launch the interactive ROOT event display.

- hitsVsSigma.sh
	- Generate a Hits vs Sigma plot for a run. Similar flags to `analyzeRun.sh`.

## Apps

- PAPERO_convert: raw `.dat` → ROOT
- calibration: produce channel baseline/sigma/mask `.cal`
- dataAnalyzer: main analysis producing per-run PDF and ROOT outputs
- event_display: GUI to browse events; Prev/Next, larger UI; 3 detectors (A,B,C)
- hits_vs_sigma: counts total hits across events for a sweep of sigma thresholds, with calibration-bad and edge channel masking; saves a PDF

## Usage examples

Analyze a single run by number:
```bash
./scripts/analyze.sh -f 275 -j json/ev-settings.json
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
