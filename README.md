# RTOS-Coupled Adaptive Edge AI

Keyword spotting on the STM32H533RE (Cortex-M33, 250 MHz) where µT-Kernel 3.0
actively co-optimises inference at runtime instead of merely scheduling a static
model. Every cycle the kernel reads the DWT cycle counter and live signal
statistics, then reshapes how the model runs: capture window size, feature
gating, task priority, and per layer numeric precision. The kernel and the model
form a closed feedback loop.

TRON Programming Contest 2026, RTOS Application (Students). Board: NUCLEO-H533RE.

## Status

The complete five task pipeline runs on the board, measured with the DWT cycle
counter at 250 MHz. The headline result, one classification every cycle under a
hard 115 ms deadline:

| Configuration | Mean latency | Worst case | Deadline held | Accuracy |
| :-- | --: | --: | :-- | --: |
| FP32 static | 125.1 ms | 125.1 ms | no | 94.0 percent |
| INT8 static | 102.6 ms | 102.7 ms | yes | 94.0 percent |
| Adaptive | 112.0 ms | 112.1 ms | yes | 94.0 percent |

Starting from full FP32 the controller reads the cycle counter, demotes layers
one by one, and settles at a stable mixed precision point that holds the
deadline while keeping the stem, the most information rich layer, at FP32. No
thrash, nine demotions, zero promotions after convergence. On device accuracy
matches the host prediction exactly in every configuration, so the core is bit
faithful on silicon. The capture chain, the learned voice activity gate, window
resizing, and the priority lever all operated on hardware in the same runs. The
remaining unmeasured axis is power, which needs the SMPS measurement described
in [docs/benchmarking.md](docs/benchmarking.md).

| Piece | State |
| :-- | :-- |
| Boot, clock, kernel, cycle counter | Working on hardware |
| Signal source, timer paced DMA replay | Verified on hardware, blocks at the expected cadence |
| Feature extraction | Matches the host front end to 6e-6, sparse mel projection |
| Inference core | 94 percent on device, identical to host under every precision mix |
| INT8 kernels | Packed SMLAD with folded offsets, 1.22 times faster than FP32 |
| Adaptation controller | Converged on device, holds the deadline with a provable bound |
| Trained model | 92.8 percent on twelve class Speech Commands, 23,180 parameters |

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
