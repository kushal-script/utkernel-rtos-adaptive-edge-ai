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
counter at 250 MHz. The deadline is the classification period itself, 120 ms,
derived from the six frame inference stride at a 20 ms hop rather than chosen
with knowledge of the measured costs:

| Configuration | Latency | 120 ms deadline | Core accuracy | Core idle |
| :-- | --: | :-- | --: | --: |
| FP32 static | 126.7 ms | missed | 94.0 percent | 23.6 percent |
| INT8 static | 93.4 ms | met | 94.0 percent | 42.5 percent |
| **Adaptive** | **86.1 ms** | **met** | **94.0 percent** | **49.8 percent** |

The adaptive point is faster than the best static compile, not a compromise
between the two. It is a mixed precision mask no single precision build can
express, and the controller finds it from measurements it takes itself.

The controller calibrates on its first two inferences, measuring what every
layer costs in each precision on this silicon, then ranks layers by that
measurement rather than by index. On this part the four depthwise layers are
together 40 percent slower in INT8, between 33 and 45 percent depending on the
layer, because their kernels are scalar while every
other layer uses packed multiply accumulate and is 44 percent faster, so the
cost optimum is mixed. From
full FP32 the controller reaches it in six cost reducing demotions; restarted
from all INT8 it climbs back to the same mask in four promotions. Converging to
the same operating point from both extremes is what makes it a property of the
silicon rather than of where the search began.

Two accuracy figures are reported and they are not the same measurement. The
94.0 percent above is **core accuracy on pre computed feature grids**, which
exercises the inference core and nothing upstream of it. End to end accuracy,
through capture, the gate, features, and inference, is scored separately on
device over the live thirty second windows: 77 to 91 classifications per
configuration, **98.9 percent for static FP32, 98.8 for static INT8 and 100
adaptive**. It is higher than the core figure because the replay corpus holds
three keywords and silence, an easier task than the twelve class evaluation
set, so what it establishes is that nothing is lost between capture and
classification, not that the pipeline beats its model. An earlier build
reported 47 to 57 percent here; two defects in how the feature tensor and its
ground truth were handed to the classifier accounted for the whole gap, see
[docs/benchmarking.md](docs/benchmarking.md).

Power now has a mechanism as well as a number. The kernel idle hook shipped as
an empty function, so the idle task spun at 250 MHz and no amount of gating work
upstream could ever show up as power. It now sleeps, and with all three
configurations driving the whole pipeline over identical thirty second windows
the core is asleep 49.8 percent of the time adaptive against 23.6 percent for
static FP32, a factor of **2.11**, and above static INT8's 42.5 percent. That ratio is the claim; it is deliberately
not converted to milliwatts, and [docs/power.md](docs/power.md) explains why.

The capture chain drops nothing. Earlier reports of thousands of dropped blocks
were an artefact of the benchmark suspending the consumer while the DMA kept
producing; with the producer paused too, the live phase overrun count is zero.

| Piece | State |
| :-- | :-- |
| Boot, clock, kernel, cycle counter | Working on hardware |
| Signal source, timer paced DMA replay | Verified on hardware, blocks at the expected cadence |
| Feature extraction | Matches the host front end to 6e-6, sparse mel projection |
| Inference core | 94 percent core accuracy, identical to host under every precision mix |
| INT8 kernels | Packed SMLAD with folded offsets, 1.22 times faster than FP32 |
| Adaptation controller | Converges to the same mask from both extremes, beats every static build |
| Trained model | 92.8 percent on twelve class Speech Commands, 23,180 parameters |
| Power | Idle sleep implemented, adaptive leaves the core asleep 2.11 times as long as FP32 |
| Capture | Zero dropped blocks over a live run, block rate matches the timer pacing |

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
| `desktop/` | The same five tasks on macOS, Linux and Windows, no board needed |
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
* [Applications](docs/applications.md), where this transfers, tested rather than
  asserted, and where it does not
* [Hardware](docs/hardware.md), board, clock tree, memory
* [Benchmarking](docs/benchmarking.md), how the numbers are produced
* [Power](docs/power.md), the mechanism, the measurement, and what is not claimed
* [Operation manual](docs/operation_manual.md), build, flash, and reproduce
* [Desktop program](desktop/README.md), the pipeline without a board, and the
  limits of what a host run can claim
* [Roadmap](docs/roadmap.md), milestones and what each still owes
* [Introduction slides](docs/TRON2026_intro_slides.pptx), the contest deck, with
  its content and rebuild steps in [slides_outline.md](docs/slides_outline.md)
* [Demo video](docs/demo_converge.mp4), the controller converging, rebuilt from
  a timed capture of a real run by `tools/capture_demo_run.py` and
  `tools/make_demo_video.py`

## Build and run

No external hardware is needed. The audio the pipeline classifies travels with
the firmware, so the board on its own reproduces every number here.

```
cmake -S KWS_TRON -B build/Release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/Release
STM32_Programmer_CLI -c port=SWD -w build/Release/KWS_TRON.hex -v -rst
python tools/parse_bench.py /dev/tty.usbmodemXXXX
```

Full procedure, including what every telemetry line means and how to check the
software without a board, is in
[docs/operation_manual.md](docs/operation_manual.md).

## Run it without a board at all

The same five tasks run on macOS, Linux and Windows, from the same sources, on a
host implementation of the µT-Kernel primitives:

```
cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop
./build/desktop/kws-desktop converge
```

The controller settles on the same mixed precision mask it settles on in
hardware, from both extremes, because it ranks its moves against the per layer
costs recorded on the board rather than anything this machine can time. What a
desktop run may and may not claim is set out in [desktop/README.md](desktop/README.md),
including why no power figure is produced there.

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

The project's own code is MIT, see [LICENSE](LICENSE). The vendored µT-Kernel
3.0 under `KWS_TRON/mtk3/` is not: the TRON Forum distributes it under the
T-License 2.1 and 2.2, stated in each file's header. Every third party
component and its licence is listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Intended
as a reusable template for real time edge AI on constrained hardware.
