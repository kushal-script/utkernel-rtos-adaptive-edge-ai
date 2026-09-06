# The pipeline on a desktop

`kws-desktop` runs the same five µT-Kernel tasks the NUCLEO-H533RE runs, from
the same sources, on macOS, Linux and Windows. It exists so this project can be
evaluated without the board: the task graph, the gate, the feature stage, the
inference core and the adaptation controller are all live, and the controller
reaches the same operating point it reaches on hardware.

Nothing under `KWS_TRON/` is modified or reimplemented to make this work. The
five tasks, the IPC objects, the sample ring and the whole audio core are
compiled for the host exactly as they are compiled for the board. Three
firmware files are replaced, and only because each one touches hardware that is
not here:

| Replaced | By | Because |
| :-- | :-- | :-- |
| `app/signal_source.c` | `src/host_source.c` | GPDMA and TIM6 become a producer thread filling the same buffer at the same cadence |
| `app/app_main.c` | `src/main.c` | the board's entry point toggles a GPIO and never returns |
| `benchmark/dwt_logger.c` | `src/host_clock.c` | the DWT cycle counter becomes a virtual counter replaying the board's per layer costs |

Everything else the firmware expects is supplied by `src/ukernel.c`, a
µT-Kernel 3.0 work alike covering the eighteen kernel calls the pipeline makes.

## Build

Needs CMake 3.16 or newer and a C11 compiler. No third party libraries.

```bash
cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop
./build/desktop/kws-desktop verify
```

On Windows use clang or MinGW-w64 rather than MSVC. The inference core declares
its cycle counter and weight streaming hooks as weak symbols so the RTOS layer
can override them, MSVC has no equivalent, and working around it would mean
editing the very file this program exists to run unmodified. The build stops
with that explanation if MSVC is detected.

## Commands

```
kws-desktop run        Run the pipeline on replayed audio and report what happened
kws-desktop converge   Wait for the controller to settle and show both endpoints
kws-desktop costs      Print the replayed per layer cost table and what it implies
kws-desktop verify     Check the inference core against the evaluation set
```

`run` takes `--seconds N`, `--wav PATH` for a 16 kHz mono file instead of the
built in corpus, `--speed X` to pace faster or slower than real time, and
`--trace` to print controller decisions as they are made. Every command takes
`--capture PATH` to replay costs from a different device capture.

## What a desktop run may claim, and what it may not

This is the part that matters, because the project's argument rests on measured
numbers and a desktop cannot measure them.

**The cycle costs are replayed, not measured.** The board's mixed precision
optimum exists because its INT8 depthwise kernels are scalar while every other
layer runs packed multiply accumulate through the M33 DSP extension. No desktop
CPU reproduces that inversion. A controller timing itself on this machine would
find no inversion at all, settle on full FP32, and tell a story the hardware
does not support. So the virtual cycle counter in `src/host_clock.c` advances by
the per layer costs recorded on the board, which makes the calibration T4 runs
on its first two inferences reproduce the device cost table exactly, and every
decision after that follows from the numbers the board actually measured. The
capture the costs come from is named in the output of every command.

**No power figure is produced.** The board measures idle residency by counting
cycles spent in `WFI`. A desktop scheduler idle fraction has no relationship to
the published 23.5, 39.9 and 42.0 percent, so nothing is reported rather than
something a reader would take as comparable.

**The mask costs are rankings, not latencies.** The per layer table excludes the
conversions inserted where consecutive layers disagree on precision, so summing
it gives a figure below the end to end latency the board measures for the same
mask. The published latencies are the board's, and they are in the README and
`docs/benchmarking.md`, not here.

**Scheduling is faithful at kernel calls, not between them.** The board runs one
core with a strictly priority preemptive kernel, so only one task touches shared
state at a time and the shared adaptation state needs no lock. Real OS threads
would break that, so the shim keeps a single running token and hands it to the
highest priority ready task at every kernel call. What it does not reproduce is
preemption in the middle of a computation. The capture producer is the exception
and does run alongside the tasks, because the DMA it stands in for is also
outside the scheduler.

**Accuracy is genuinely reproduced.** The INT8 path is bit identical to the
device's: the packed kernels the board uses and the scalar fallback this build
selects differ only in the order of integer additions, and the accumulator
cannot overflow for this model. `verify` scores 141 of 150 under all INT8, all
FP32 and the converged mixed mask, the same 94.0 percent the board reports.
FP32 differs from the device in the last place, because the device's compiler
contracts a multiply and an add where this build does not. The build pins
`-ffp-contract=off` so that two different hosts agree with each other exactly,
which is what makes a result reproducible between machines.

**End to end accuracy over a short run is not the published figure.** The board
scores each configuration over a thirty second window with the whole pipeline
live. A short desktop run scores far fewer classifications and should not be
read as the end to end accuracy in `docs/benchmarking.md`.

## What it does reproduce

Run `converge` and the controller settles on mask `0x0AA`, four depthwise layers
in FP32 and everything else in INT8, in six cost reducing demotions from full
FP32. It then restarts itself from all INT8 and climbs back to the same mask in
four promotions. That is the project's central claim, path independence of the
operating point, and it holds here because it follows from the recorded cost
table rather than from anything about the machine it runs on.

A run also exercises the kernel primitives the program plan names: window
resizes travel by `tk_snd_mbx` and are counted, the controller raises T4's
priority with `tk_chg_pri` under deadline pressure, the feature gate is peeked
with `tk_ref_flg`, per layer weights are streamed through `tk_get_mpl` and
`tk_rel_mpl` with the peak pool use reported, and every wakeup between tasks is
an event flag.
