# Model

Training and export pipeline for the keyword spotting model, and the versioned artifacts the firmware consumes.

## Flow

1. Train, Edge Impulse or a local pipeline, on the target keyword set.
2. Export quantised variants. At minimum a full INT8 model. A higher precision or larger variant is needed for the runtime precision selection described in [../docs/novelty.md](../docs/novelty.md).
3. Convert to a C header the firmware includes as `KWS_TRON/audio/kws_model.h`.
4. Record the training run under `../experiments/` with its accuracy, size, and the data it was trained on.

## Artifacts

Promoted models live here with a version tag and a short note stating their accuracy, size in bytes, input shape, and the experiment folder that produced them. The raw training run stays in `experiments/`, only the artifact that the firmware uses is promoted here.

## Layout

```
model/
  runs/        working exports before promotion
  README.md    this file
```

The exported header that the firmware compiles against is `KWS_TRON/audio/kws_model.h`. Keep the input shape and quantisation parameters there in sync with `KWS_TRON/audio/mfcc_config.h`.
