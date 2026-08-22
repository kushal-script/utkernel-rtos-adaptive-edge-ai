# Model

Training, quantisation, and export for the keyword spotting model. Everything
the firmware compiles under `KWS_TRON/audio/` that is marked generated comes
from here.

## Flow

```
fetch_dataset.sh  ──►  kws.train  ──►  checkpoint.pt  ──►  kws.export  ──►  C sources
                         │                                    │
                    experiments/<run>/                   verifies against
                    curves and metrics                   the float model first
```

```
bash model/fetch_dataset.sh
cd model
python -m kws.train --epochs 30
python -m kws.export --checkpoint ../experiments/<run>/checkpoint.pt
python ../tools/verify_device_core.py
```

## What each module does

| Module | Role |
| :-- | :-- |
| `features.py` | The MFCC front end, the single source of truth for feature geometry |
| `dataset.py` | Speech Commands loading, the official split, silence and unknown |
| `model.py` | The DS-CNN, and the trailing frame masking the adaptive window needs |
| `train.py` | Training, and the accuracy against active frames curve |
| `quantize.py` | Per channel weights, asymmetric activations, multiplier and shift |
| `convert.py` | Batch norm folding, layout conversion, and the golden reference |
| `export.py` | Emits the model, tables, evaluation set, and replay clips as C |

## The golden reference

`convert.py` contains a NumPy implementation of exactly the arithmetic the
device performs, including the requantisation rounding. Export checks it against
the trained float model before emitting anything, and refuses to be trusted if
they disagree. `tools/verify_device_core.py` then checks the compiled C against
the same reference.

This is what makes a device result diagnosable: the maths is settled on the host
first, so a disagreement on hardware is a hardware or toolchain question rather
than an open one.

## Feature geometry

A 30 ms frame with a 20 ms hop gives 49 frames over one second, and ten MFCC
coefficients per frame, matching the input shape used by the published keyword
spotting networks this project is compared against. `mfcc_config.h` mirrors
`FeatureConfig` and the two must be changed together, followed by a regeneration
and a retrain.

The window, mel filterbank, and DCT matrix are generated here and emitted as C
tables rather than recomputed on device, which guarantees the two agree and
saves the device the work.

## Artifacts

Promoted models live here with a version tag and a note stating accuracy, size,
input shape, and the experiment folder that produced them. The raw training run
stays under `experiments/`, only the artifact the firmware uses is promoted.

```
model/
  kws/          the pipeline
  runs/         working exports before promotion
  datasets/     corpora, never committed
```
