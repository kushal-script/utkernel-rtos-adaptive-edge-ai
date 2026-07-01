# Experiments

Every experiment and model run that produces numbers lives here in its own folder. The folder name is a timestamp and a short slug, so runs sort chronologically and stay self describing.

## Folder naming

```
YYYY-MM-DD_HHMMSS_short-slug
```

Example, `2026-07-02_143010_baseline-int8-latency`.

## Required contents

| File | Purpose |
| :-- | :-- |
| `config.json` | Every parameter that defines the run, model, window, thresholds, budget, input set |
| `notes.md` | What was tested, why, and what changed from the previous run |
| `results.md` | The numbers, and the conclusion in one or two sentences |
| `data/` | Raw captures, cycle logs, current traces |
| `plots/` | Generated figures, with the script or command that made them |

## Rules

A run is reproducible from its own folder. Anyone should be able to read `config.json` and `notes.md` and repeat it. Do not overwrite a run, create a new timestamped folder. Plot whatever can be plotted, waveforms, cycle histograms, power traces, accuracy against precision, and keep the plotting command next to the figure.

Training runs follow the same convention. Model artifacts that are promoted for use go into `model/`, the run that produced them stays here.
