# Adaptation

The three runtime knobs the controller turns, what each one costs, and how the
proposal's description maps onto a keyword spotting pipeline with a fixed input
model. Read [architecture.md](architecture.md) first for the task graph.

## The knobs

| Knob | Range | Owner | Set by | Effect |
| :-- | :-- | :-- | :-- | :-- |
| Capture window | 64 to 256 samples, step 16 | T1 | mailbox from T5 | Samples per DMA half block, so how often the pipeline is woken |
| Active frames | 32 to 49 | T3 | mailbox message from T5, published by T1 | How much context the model is given, the newest rows lead the tensor and the rest read as zero |
| Layer precision | INT8 or FP32, per layer | T4 | T5, a cost ranked hill climb over the measured per layer gain, considering both directions on every decision | Cycles and energy per layer against numeric accuracy |

Task priority is a fourth lever. T5 raises T4 with `tk_chg_pri` when a deadline
is at risk and lowers it again when slack returns.

## Why a fixed input model still has an adaptive window

The model input is a fixed 49 by 10 MFCC grid. A smaller window cannot simply
produce a smaller tensor, the shape is compiled into the weights. Two things
make the window genuinely adaptive anyway.

The capture window is the DMA half block length. It sets the interrupt cadence
and therefore the pipeline's reaction latency and its per sample overhead. A
short window wakes the pipeline often, which lowers the delay between a sound
arriving and the classifier seeing it, at the cost of more interrupts and more
context switches. A long window is the opposite. That is a real time trade the
kernel makes, and it is the trade the mailbox message carries.

The active frame count is how much context the model is given: the newest
`active` of the 49 rows lead the tensor and the rest read as zero. It does not
reduce the feature work or the inference cost, every frame is still computed
so the history is intact when the context grows back, and the convolution runs
over the full tensor either way. What it trades is accuracy for a shorter,
fresher view of the signal, and it is only legitimate because the model is
trained for it: training masks a random number of trailing frames on every
batch, so every value the controller can select is an operating point the model
has seen, not an input distribution it was never shown. The accuracy cost of
each setting is measured, not assumed, and the curve is written to
`plots/context_curve.png` by every training run. The floor is 32 frames, where
that curve is still flat; 16 is in the training range but costs more than half
the accuracy, and an earlier build that descended to it while erasing the
wrong end of the history was one of the two defects behind the gap between
core and end to end accuracy, see [benchmarking.md](benchmarking.md).

Which end of the history the tensor keeps matters. Training masks the trailing
frames of a clip, so the model expects the audio in the leading rows. On a
sliding history the leading rows must therefore be the most recent audio; an
earlier build zeroed the newest rows in place instead, which discarded the word
just spoken, kept the second before it, and corrupted the history for the next
inferences as well.

## Deviations from the program plan

The plan was written before implementation. The points below needed adjusting,
and the reasoning is recorded here rather than silently changed.

Section 6.1 describes shrinking the window on a flat signal to reduce inference
frequency. Shrinking the DMA block on its own raises the interrupt rate rather
than lowering it, so the power saving comes from the voice activity gate: on a
quiet signal the gate closes, T3 computes nothing,
and the pipeline stops before the model runs. The active frame count is not a
power lever, it shortens the context and nothing else. The stated
goal of the section, less work and less power on flat input, is what the
implementation delivers.

Section 6.2 describes skipping a 256 point FFT. The front end uses a 512 point
FFT because a 30 ms frame at 16 kHz is 480 samples, which does not fit a 256
point transform. The saving is measured with the cycle counter rather than
quoted.

Section 6.3 explains the INT8 speedup as one cycle per multiply accumulate
against four for FP32. That is not the mechanism. The gain comes from packed
SIMD multiply accumulate through the DSP extension, from four times less
weight traffic, and from avoiding transfers to the floating point register
file. The honest claim, and the one the benchmark reports, is a measured
speedup on this silicon rather than a figure derived from an assumed cycle
count. See [novelty.md](novelty.md).

The development environment moved as well. The plan named STM32CubeIDE, Edge
Impulse for training, CMSIS-DSP for the FFT, and SWO trace for cycle logging.
Delivered instead: a CMake build with `arm-none-eabi-gcc` so a reviewer needs
no IDE, a PyTorch training pipeline committed under `model/` so the model
reproduces from a command rather than a web service, an in tree FFT so every
cycle the benchmark reports belongs to code in this repository, and telemetry
over the ST-LINK virtual COM port so reading the board needs a serial terminal
and nothing else. Each substitution trades a named tool for a self contained,
reproducible equivalent.

## The voice activity gate

T2 keeps a rolling estimate of frame energy and variance. Below threshold the
frame is classified as quiescent and the pipeline stops there, so T3 and T4 do
not run at all. This saves the whole feature and inference cost on silence,
which is a far larger saving than skipping only the transform, and it needs no
special model behaviour because the classification is made before the model is
reached.

Because the gate stops the pipeline at T2, before T3 is ever woken, the
`skipped` counter in `BENCH_PIPELINE` legitimately reads zero: frames the gate
suppresses are never scheduled in the first place, so there is nothing for T3
to skip. The `tk_ref_flg` peek in T3 is a guard for the gate closing while a
grid is mid fill, and in the recorded runs it never fires, because the
controller runs at a higher priority and reopens the gate before T3 next
wakes, so the `BENCH_GRID` restart count reads zero as well. The gate's effect
shows up in the telemetry as idle residency and the block counts in
`BENCH_STATE`, not as skipped frames.

## Self tuning

Fixed thresholds only work in the acoustic environment they were tuned in. T5
therefore estimates the quiescent noise floor online and places the gate
threshold a fixed margin above it, and it adapts the cycle budget to the
latency it actually observes. The controller is shown to converge: the
precision mask reaches the same operating point from both extremes, six
demotions from full FP32 and four promotions from all INT8. The planned
comparison against the best fixed thresholds found by sweep was not performed,
and is recorded as a descope in [roadmap.md](roadmap.md) rather than quietly
dropped: the convergence evidence stands on its own, and the sweep remains the
right next experiment for the threshold half of the claim.
