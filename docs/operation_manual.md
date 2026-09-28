# Operation manual

Everything a reviewer needs to build the firmware, put it on a NUCLEO-H533RE, and reproduce every number this project reports. No external hardware is required: the board alone is enough, because the audio the pipeline classifies travels with the firmware.

## What you need

| | |
| :-- | :-- |
| Board | NUCLEO-H533RE, USB cable |
| Toolchain | Arm GNU Toolchain 14.2.Rel1, the bare metal `arm-none-eabi` release with newlib, on `PATH` or with `ARM_TOOLCHAIN_DIR` set to its `bin` directory. CMake 3.22 or newer |
| Flashing | STM32CubeProgrammer, or the fallback in the appendix |
| Host tools | Python 3.11 or newer, `pip install -r requirements.txt` |

Nothing needs to be wired to the board. No microphone, no sensor, no jumper changes.

The shipped image was built with 14.2.Rel1 and every published latency is tied to it, because a different compiler regenerates different code. A gcc without newlib, which is what the Homebrew `arm-none-eabi-gcc` formula is on its own, fails at `stdint.h`; install `arm-none-eabi-newlib` alongside it or use the Arm release from developer.arm.com.

## 1. Build

```bash
git clone https://github.com/kushal-script/utkernel-rtos-adaptive-edge-ai.git
cd utkernel-rtos-adaptive-edge-ai
cmake -S KWS_TRON -B build/Release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/Release
```

The build is self contained. The model, the feature tables, the evaluation set, and the replay audio are all committed as generated C, so no dataset download and no training run is needed to build or to reproduce the device results.

Expect roughly 460 KB of flash and 117 KB of RAM.

## 2. Flash

```bash
STM32_Programmer_CLI -c port=SWD -w build/Release/KWS_TRON.hex -v -rst
```

Any ST-LINK capable tool works; the firmware is an ordinary image at `0x08000000`. Option bytes are never touched, and nothing in this project requires them to be changed from the factory default.

## 3. Watch it run

Open the ST-LINK virtual COM port at **115200 8N1**. The board boots, the pipeline starts, and the benchmark reports automatically. A full report takes about two and a half minutes: the controller converges, then each configuration runs live for thirty seconds, then the inference core is measured in isolation.

```bash
python tools/parse_bench.py /dev/tty.usbmodemXXXX
```

That writes a timestamped folder under `experiments/` containing the raw capture, the parsed numbers, and the figures.

## 4. What the report says

| Line | Meaning |
| :-- | :-- |
| `BENCH_LIVE` | One configuration, whole pipeline live: end to end accuracy and core idle residency |
| `BENCH` | One configuration, inference core in isolation: latency and core accuracy |
| `BENCH_LAYER` | Measured cycles for one layer under one precision, from the isolated benchmark runs |
| `BENCH_COST` | The per layer cost table the controller measured on this silicon |
| `BENCH_ESTIMATE` | The controller's calibrated whole inference estimate per precision and the settled mask |
| `BENCH_TRACE` | Every controller decision, so convergence can be plotted |
| `BENCH_CONVERGE` | The settled mask and how many deadline misses occurred while converging |
| `BENCH_STATE` | The learned gate state: VAD threshold, noise floor, blocks seen, active blocks |
| `BENCH_CONTROL` | Lever usage counts: decisions, demotions, promotions, window resizes sent by mailbox, priority raises. Promotions counts every one in the session, the four of the convergence probe and the four climbing back after the pinned INT8 live window, so `promote=8` is the expected reading |
| `BENCH_PIPELINE` | Whole pipeline counters: inferences, scored, correct, overruns, frames, skipped, resyncs, capture overruns |
| `BENCH_GRID` | Feature grid restarts, how often the gate closed while a grid was filling |
| `BENCH_MEMORY` | Peak layer pool use against pool capacity, the `tk_get_mpl` streaming evidence |
| `BENCH_CAPTURE` | Capture block rate against the expected rate, and dropped blocks |
| `BENCH_POWER` | Idle residency |

`BENCH_CONTROL`, `BENCH_MEMORY`, and `BENCH_GRID` are the direct evidence that the kernel primitives are exercised, not decorative: window resizes travel by `tk_snd_mbx`, priority raises by `tk_chg_pri`, and the pool peak comes from `tk_get_mpl` streaming one layer at a time.

## 5. Check the software without a board

```bash
python tools/verify_device_core.py
```

Compiles the real device C with the host compiler and checks the transform, the MFCC frame path, and the inference core against the NumPy reference the model was trained against. Run it after touching anything in `KWS_TRON/audio/`.

## 6. Run the whole pipeline without a board

The five tasks also run on macOS, Linux and Windows, from the same sources, against a host implementation of the µT-Kernel primitives:

```bash
cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop
./build/desktop/kws-desktop verify
./build/desktop/kws-desktop converge
./build/desktop/kws-desktop run --seconds 30
```

`verify` scores the inference core at 94.0 percent under every precision configuration, the same figure the board reports. `converge` shows the controller reaching the same mixed precision mask from both extremes. `run` exercises capture, the gate, features, inference and the controller together and reports the kernel primitives each one used.

Per layer cycle costs there are replayed from a recorded device capture, because the asymmetry the controller exploits is a property of the Cortex-M33 that no desktop reproduces, and no power figure is produced at all. Both limits, and the rest of what a host run may and may not claim, are stated in [desktop/README.md](../desktop/README.md).

## 7. Retrain and regenerate, optional

Only needed to change the model or the corpus.

```bash
bash model/fetch_dataset.sh                     # Speech Commands, about 2.4 GB
cd model
python -m kws.train --epochs 30                 # writes a run under experiments/
python -m kws.make_replay --clips 6             # stratified replay corpus
python -m kws.export --checkpoint ../experiments/<run>/checkpoint.pt
```

Export verifies the quantised graph against the float model before it emits anything, and refuses to be trusted if they disagree.

Both steps are deterministic, so running them unchanged against the same checkpoint regenerates every file under `KWS_TRON/audio` byte for byte. If a regeneration you did not intend shows a diff, something upstream moved: check the corpus first, see [signal_source.md](signal_source.md).

## Appendix, flashing when ST-LINK SWD is unavailable

On one development machine the ST-LINK's USB link could not sustain the bulk transfers SWD flashing needs, while the virtual COM port worked perfectly. The workaround, kept because it is useful on any host with a marginal link, is to use SWD only briefly to jump the core into the on chip ROM bootloader, then write the image over the serial port with the standard UART bootloader protocol. The scripts are in `tools/`. This is a host side workaround and says nothing about the board.
