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
the idle residency is measured on device at 82.3 percent of wall time. Average
current itself still needs a meter. See [power.md](power.md).

### Accuracy

Two different measurements share the word accuracy and they must not be
conflated.

**Core accuracy** is scored on the labelled grids in `eval_set.c`, which are
pre computed features. It exercises the inference core and nothing upstream, so
it is the right number for a claim about quantisation and precision switching,
and the wrong number for a claim about the pipeline. It is 94.0 percent, and
identical across every precision configuration.

**End to end accuracy** is scored on device from replayed audio through capture,
the gate, feature extraction, and inference. It is now measured, having
previously been impossible because the scoring guard demanded the feature grid
sit entirely inside one replay clip, which a grid spanning nearly a whole clip
essentially never does. On the current six clip corpus it lands on roughly ten
scored classifications per run and varies widely between runs, so it is
reported as a raw count and **not** stated as a percentage: the sample is far
too small, and there is a known limitation behind it, the feature grid can
stitch across a gate closure and so contain frames from either side of a
silence. Widening that sample is future work, and the honest position until
then is that the end to end path works and its accuracy is not yet
characterised.

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
