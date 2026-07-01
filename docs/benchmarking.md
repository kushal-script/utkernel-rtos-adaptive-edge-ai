# Benchmarking

The project stands on one comparison, the static baseline against the adaptive pipeline, measured on three axes. Every claim in the writeup traces back to a run stored under `experiments/`.

## Axes

### Latency, worst case bound

Read the DWT CYCCNT before and after each inference, and per layer where relevant. Report the worst case, not the average, because the real-time claim is that a hard per inference deadline is met even under load. A bounded and provable latency is the point, a statistical average is not.

### Power, average

Measure current through the on board SMPS on the NUCLEO-H533RE. The adaptive pipeline spends more time in the kernel idle task on quiet input through the VAD gate and reduced inference frequency, which shows up as lower average current. Report average power over a fixed workload.

### Accuracy

Classification accuracy on a fixed held out set, reported for each configuration. Adaptation trades a stated and small accuracy cost for latency and power headroom. The cost must be measured, not assumed.

## Protocol

1. Fix the input, a recorded stream or a repeatable test sequence, so every configuration sees identical data.
2. Run the static baseline, one model, fixed window, no adaptation. Record all three axes.
3. Run the adaptive pipeline on the same input. Record all three axes.
4. Report the deltas, and confirm the deadline held in every inference of the adaptive run.

## Where results go

Each run is a timestamped folder under `experiments/`. See [experiments/README.md](../experiments/README.md) for the required contents. The headline figure, baseline against adaptive across the three axes, is generated from those folders by a plotting script in `tools/` so it is reproducible from raw data rather than hand assembled.
