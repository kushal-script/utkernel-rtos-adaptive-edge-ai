# Operation manual

Everything a reviewer needs to build the firmware, put it on a NUCLEO-H533RE,
and reproduce every number this project reports. No external hardware is
required: the board alone is enough, because the audio the pipeline classifies
travels with the firmware.

## What you need

| | |
| :-- | :-- |
| Board | NUCLEO-H533RE, USB cable |
| Toolchain | `arm-none-eabi-gcc` (tested with 16.1.0), CMake 3.22 or newer |
| Flashing | STM32CubeProgrammer, or the fallback in the appendix |
| Host tools | Python 3.11 or newer, `pip install -r requirements.txt` |

Nothing needs to be wired to the board. No microphone, no sensor, no jumper
changes.

## 1. Build

```bash
git clone https://github.com/kushal-script/utkernel-rtos-adaptive-kws.git
cd utkernel-rtos-adaptive-kws
cmake -S KWS_TRON -B build/Release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/Release
```

The build is self contained. The model, the feature tables, the evaluation set,
and the replay audio are all committed as generated C, so no dataset download
and no training run is needed to build or to reproduce the device results.

Expect roughly 460 KB of flash and 117 KB of RAM.

## 2. Flash

```bash
STM32_Programmer_CLI -c port=SWD -w build/Release/KWS_TRON.hex -v -rst
```

Any ST-LINK capable tool works; the firmware is an ordinary image at
`0x08000000`. Option bytes are never touched, and nothing in this project
requires them to be changed from the factory default.

## 3. Watch it run

Open the ST-LINK virtual COM port at **115200 8N1**. The board boots, the
pipeline starts, and the benchmark reports automatically. A full report takes
about two and a half minutes: the controller converges, then each configuration
runs live for thirty seconds, then the inference core is measured in isolation.

```bash
python tools/parse_bench.py /dev/tty.usbmodemXXXX
```

That writes a timestamped folder under `experiments/` containing the raw
capture, the parsed numbers, and the figures.

## 4. What the report says

| Line | Meaning |
| :-- | :-- |
| `BENCH_LIVE` | One configuration, whole pipeline live: end to end accuracy and core idle residency |
| `BENCH` | One configuration, inference core in isolation: latency and core accuracy |
| `BENCH_COST` | The per layer cost table the controller measured on this silicon |
| `BENCH_TRACE` | Every controller decision, so convergence can be plotted |
| `BENCH_CONVERGE` | The settled mask and how many deadline misses occurred while converging |
| `BENCH_CAPTURE` | Capture block rate against the expected rate, and dropped blocks |
| `BENCH_POWER` | Idle residency |

## 5. Check the software without a board

```bash
python tools/verify_device_core.py
```

Compiles the real device C with the host compiler and checks the transform, the
MFCC frame path, and the inference core against the NumPy reference the model
was trained against. Run it after touching anything in `KWS_TRON/audio/`.

## 6. Retrain and regenerate, optional

Only needed to change the model or the corpus.

```bash
bash model/fetch_dataset.sh                     # Speech Commands, about 2.4 GB
cd model
python -m kws.train --epochs 30                 # writes a run under experiments/
python -m kws.make_replay --clips 6             # stratified replay corpus
python -m kws.export --checkpoint ../experiments/<run>/checkpoint.pt
```

Export verifies the quantised graph against the float model before it emits
anything, and refuses to be trusted if they disagree.

## Appendix, flashing when ST-LINK SWD is unavailable

On one development machine the ST-LINK's USB link could not sustain the bulk
transfers SWD flashing needs, while the virtual COM port worked perfectly. The
workaround, kept because it is useful on any host with a marginal link, is to
use SWD only briefly to jump the core into the on chip ROM bootloader, then
write the image over the serial port with the standard UART bootloader protocol.
The scripts are in `tools/`. This is a host side workaround and says nothing
about the board.
