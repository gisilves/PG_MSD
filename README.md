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

- analyzeRun.sh
	- Batch convert, calibrate (if needed), and analyze run(s).
	- Calibration policy: use the most recent previous CAL run’s `.cal`; if none, use the nearest later CAL; if the current run is CAL, use its own `.cal`.

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
./scripts/analyzeRun.sh -f 275 -j json/ev-settings.json
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

Backward compatibility: if the parameters file is missing, the analyzer will fall back to `planeOffsetsMm` in `json/ev-settings.json` if present.
