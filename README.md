# RTOS-Coupled Adaptive Edge AI

Keyword spotting on the STM32H533RE (Cortex-M33, 250 MHz) where µT-Kernel 3.0
actively co-optimises inference at runtime instead of merely scheduling a static
model. Every cycle the kernel reads the DWT cycle counter and live signal
statistics, then reshapes how the model runs: capture window size, feature
gating, task priority, and per layer numeric precision. The kernel and the model
form a closed feedback loop.

TRON Programming Contest 2026, RTOS Application (Students). Board: NUCLEO-H533RE.

## Status

The complete five task pipeline is implemented and builds, using 113 KB of the
272 KB SRAM and 396 KB of the 512 KB flash.

| Piece | State |
| :-- | :-- |
| Boot, clock, kernel, cycle counter | Working on hardware |
| Signal source, timer paced DMA replay | Builds, not yet run on hardware |
| Feature extraction | Transform matches the host reference to 4e-6 |
| Inference core | 94 percent on the evaluation set, matches the golden reference to 5e-8 |
| Adaptation controller | Implemented, thresholds learned online |
| Benchmark harness | Implemented, host side parser and plots working |
| Trained model | 92.8 percent on twelve class Speech Commands, 23,180 parameters |

Nothing has run on the board yet. Every milestone in
[docs/roadmap.md](docs/roadmap.md) states what must be measured on hardware
before it is called done.

## The signal source, and why there is no microphone in the loop

The contribution is the coupling between the kernel and the model, not the
transducer. Accuracy cannot be scored against a live microphone because there is
no ground truth, so evaluation needs labelled audio replayed through the capture
path regardless.

The default source therefore streams a labelled corpus from flash through GPDMA,
paced by a timer at the sample rate. It is not a simulation of the capture path,
it is the capture path: same DMA channel behaviour, same interrupt per filled
block, same event flag into the same task. Only the origin of the bytes differs,
and the corpus carries labels so the device can score itself. An INMP441 sits
behind the same interface for when a live demonstration is wanted. See
[docs/signal_source.md](docs/signal_source.md).

## Repository layout

| Path | Purpose |
| :-- | :-- |
| `KWS_TRON/app/` | The five tasks, kernel objects, signal source |
| `KWS_TRON/audio/` | Transform, layer kernels, inference core, generated model |
| `KWS_TRON/benchmark/` | Cycle counter and the on device benchmark |
| `KWS_TRON/mtk3/` | Vendored µT-Kernel 3.0 BSP2 |
| `model/kws/` | Training, quantisation, export, and the golden reference |
| `docs/` | Design and rationale |
| `experiments/` | Timestamped runs, every number traces back to one |
| `tools/` | Host side verification and plotting |

## Documentation

* [Architecture](docs/architecture.md), the task graph and the IPC map
* [Adaptation](docs/adaptation.md), the runtime knobs and what each one costs
* [Signal source](docs/signal_source.md), how samples reach the pipeline
* [Inference core](docs/inference_core.md), quantisation and precision switching
* [Novelty](docs/novelty.md), the research claim
* [Hardware](docs/hardware.md), board, clock tree, memory
* [Benchmarking](docs/benchmarking.md), how the numbers are produced
* [Roadmap](docs/roadmap.md), milestones and what each still owes

## Build

```
source setup_env.sh
cmake -S KWS_TRON -B build/Debug -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/Debug
flash
```

## Train and export the model

```
bash model/fetch_dataset.sh
cd model && python -m kws.train --epochs 30
python -m kws.export --checkpoint ../experiments/<run>/checkpoint.pt
```

Training writes a timestamped folder under `experiments/` with the checkpoint,
the accuracy curves, and the accuracy against active frames curve the controller
trades along. Export regenerates the model, the tables, the evaluation set, and
the replay clips under `KWS_TRON/audio/`, and verifies the quantised graph
against the float model before emitting anything.

## License

Open source release planned after the contest submission. Intended as a reusable
template for real time edge AI on constrained hardware.
