# Benchmarking

The project stands on one comparison, the static baseline against the adaptive
pipeline, measured on three axes. Every claim in the writeup traces back to a
run stored under `experiments/`.

## Axes

### Latency, worst case bound

The DWT cycle counter is read around each inference and around each layer.
Report the worst case, not the average, because the real time claim is that a
hard per inference deadline is met even under load. A bounded and provable
latency is the point, a statistical average is not.

### Power, average

Measure current at the IDD jumper, which breaks the supply to the
microcontroller so a meter can sit in series. It covers the MCU only, so the
board still enumerates and telemetry still streams while measuring. The
STM32H533 has no SMPS, and earlier wording here that routed the measurement
through one was wrong.

The mechanism now exists: the kernel idle hook sleeps rather than spinning, and
with the whole pipeline live over identical thirty second windows the core is
asleep 43.3 percent of wall time in the adaptive configuration against 41.0 for
static INT8 and 23.5 for static FP32. Average current itself still needs a
meter. See [power.md](power.md).

### Accuracy

Two different measurements share the word accuracy and they must not be
conflated.

**Core accuracy** is scored on the labelled grids in `eval_set.c`, which are
pre computed features. It exercises the inference core and nothing upstream, so
it is the right number for a claim about quantisation and precision switching,
and the wrong number for a claim about the pipeline. It is 94.0 percent, and
identical across every precision configuration.

**End to end accuracy** is scored on device from replayed audio through capture,
the gate, feature extraction, and inference. The benchmark holds each
configuration for a thirty second window with the whole pipeline live, which
yields 79 to 84 scored classifications per configuration; T3 carries a guard
that restarts the feature grid if the gate closes mid fill, see the gate note
in [adaptation.md](adaptation.md). Measured in
`experiments/2026-09-22_092112_hardware-ground-truth-latched`: static FP32 83
of 84 at 98.8 percent, static INT8 79 of 79 at 100, adaptive 78 of 79 at 98.7.

That it exceeds the core figure is expected and not a contradiction. The core
figure is scored on 150 grids drawn across all twelve classes; the replay
corpus holds three keywords and silence. What the end to end figure supports
is that the pipeline loses nothing between capture and classification, which
is the property a real time claim needs. Core accuracy remains the right
measurement for the quantisation and precision switching claim.

Earlier builds reported 47 to 57 percent here and attributed the gap to window
misalignment. That attribution was wrong. Two defects accounted for it, both
in how the feature tensor reached the classifier: the active frame lever erased
the newest rows of the sliding history in place, so a shortened context was the
second before the word rather than the word, and the ground truth was read
from a global after the inference, so any configuration whose inference
outlasts the stride was scored against the audio published after the one it
saw. The three runs that isolated and fixed them are recorded under
`experiments/2026-09-22_*`, each with what it changed and what moved.

### Which run backs which figure

Latencies, the per layer cost table, idle residency and the converged mask are
quoted from `experiments/2026-09-06_220415_hardware-requantise-ub-fixed`, the
run the desktop program's built in cost table and the demo video derive from.
End to end accuracy is quoted from
`experiments/2026-09-22_092112_hardware-ground-truth-latched`. The later run
reproduces the earlier one's latencies within 0.1 percent, its idle residency
to the tenth of a percent, and its cost table within 0.4 percent per layer, so
the two are one measurement for every purpose except the one figure that
changed.

## Running it

The firmware runs the harness automatically when `BENCH_ENABLE` is set and
prints a block between `BENCH_BEGIN` and `BENCH_END`. Reporting never happens
inside a timed region, results are collected into RAM during a run and printed
afterwards, so the act of reporting does not perturb the measurement.

```
python tools/parse_bench.py /dev/tty.usbmodemXXXX
```

That writes a timestamped folder under `experiments/` with the raw capture, the
parsed numbers, and two figures: latency and accuracy per configuration, and the
per layer cycle profile.

## Before any number is trusted

Software correctness is already covered without a board:

```
python tools/verify_device_core.py
```

That compiles the real device core with the host compiler and checks the
transform against the NumPy front end and the inference core against the
evaluation set under every precision configuration. It should be run after any
change under `KWS_TRON/audio`.

What the host cannot check, and what the first hardware session must confirm:

| Check | Expected | If it fails |
| :-- | :-- | :-- |
| Capture cadence | Block interrupt every window divided by 16 kHz, 16 ms at 256 samples | The timer is not gating the DMA, see [signal_source.md](signal_source.md) |
| Capture content | The buffer holds the corpus, not zeros or noise | GPDMA may not reach flash on the configured port, stage the clip in SRAM |
| Overruns | `signal_source_overruns` stays at zero | The pipeline is not keeping up, lengthen the window |
| Device accuracy | Matches the host figure on the same evaluation set | A toolchain or floating point difference, not a maths error |
| Cycle counts | Non zero and stable across runs | The cycle counter is not enabled |

## Protocol

1. Fix the input so every configuration sees identical data. The replay source
   does this by construction, which is the main reason it is the default.
2. Run the static baseline, one precision, fixed window, no adaptation. Record
   all three axes.
3. Run the adaptive pipeline on the same input. Record all three axes.
4. Report the deltas, and confirm the deadline held in every inference of the
   adaptive run.

## Where results go

Each run is a timestamped folder under `experiments/`. See
[experiments/README.md](../experiments/README.md) for the required contents. The
headline figure is generated from those folders by `tools/parse_bench.py`, so it
is reproducible from raw data rather than hand assembled.
