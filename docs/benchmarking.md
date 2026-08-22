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

Measure current through the on board SMPS. The adaptive pipeline spends more
time in the kernel idle task on quiet input through the gate, which shows up as
lower average current. Report average power over a fixed workload. This axis
needs the board and has not been measured.

### Accuracy

Classification accuracy on the labelled evaluation set that travels in flash,
reported for each configuration. Adaptation trades a stated and small accuracy
cost for latency and power headroom, and the cost must be measured rather than
assumed.

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
