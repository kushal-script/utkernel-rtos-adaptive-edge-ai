# Roadmap

Milestones are capability based, not date based. Each one produces a working
system that adds a layer of intelligence over the previous. Every milestone ends
with a commit, and any run that produces numbers writes its results into
`experiments/`.

## M0, boot and kernel, done

Boot chain, 250 MHz clock at VOS0, DWT cycle counter, µT-Kernel 3.0 with the
STM32H533 BSP, and a heartbeat task.

## M1, capture path, done in software

The signal source is behind one interface. The default streams a labelled
corpus from flash through GPDMA paced by a timer, which drives the same
circular buffer, the same interrupt per block, and the same event flag a sensor
would. The microphone path is kept behind the same interface for when one is
attached. See [signal_source.md](signal_source.md).

Exit, on hardware: blocks arrive at the window length divided by 16 kHz, the
capture buffer holds the corpus rather than noise, and the overrun count stays
at zero.

## M2, feature extraction, done in software

T3 computes MFCC frames with the in tree transform and the exported tables, and
maintains the quantised 49 by 10 grid the model reads directly. The transform
agrees with the NumPy reference to four parts in a million and the host front
end is the single source of truth for the tables.

Exit, on hardware: device features match the host reference for the same
recorded input within tolerance.

## M3, baseline inference, done in software

T4 runs the hand written per layer core. On the host the core reaches 94 percent
on the on device evaluation set, identically under full INT8, full FP32, and
alternating per layer precision, and reproduces the golden reference logits to
five parts in a hundred million. The trained model is 92.8 percent on the full
twelve class test set with 23,180 parameters.

Exit, on hardware: the same accuracy on the same evaluation set, with the per
inference and per layer cycle counts recorded in `experiments/`.

## M4, first adaptation, done in software

T2 gates the pipeline on silence so T3 and T4 do not run at all, and the
controller varies the capture window and the active frame count. The accuracy
cost of every active frame setting is measured and plotted by the training run.

Exit, on hardware: a measured reduction in average work on quiet input with no
loss of keyword recall.

## M5, closed loop, centerpiece, done in software

T5 reads the per layer timing and the signal state and drives precision,
window, active frames, and task priority together. Its thresholds are learned,
the gate is placed relative to an online noise floor and the budget relative to
the observed cost.

Exit, on hardware: the adaptive pipeline holds the per inference deadline while
cutting average work at a stated accuracy cost against the static baseline, and
the learned thresholds are shown to converge and to beat the best fixed
thresholds found by sweep.

The sweep half of that exit was descoped. The convergence evidence delivered is
the precision mask reaching the same operating point from both extremes, and the
gate threshold is validated by the gate closing and reopening correctly on the
stratified corpus, not against a swept fixed threshold baseline. The sweep
remains the right next experiment for the threshold half of the self tuning
claim, see the self tuning note in [adaptation.md](adaptation.md).

## M6, benchmark harness, partly done

The on device harness scores the evaluation set under static and adaptive
configurations and reports latency, accuracy, and the per layer profile outside
every timed region. `tools/parse_bench.py` turns that report into an experiment
folder with figures.

What remains is a current measurement at the IDD jumper. The mechanism and the
idle residency are done, see [power.md](power.md).

Exit: one reproducible figure that shows the tradeoff the project claims.

## M7, stretch

TrustZone secure world weight isolation. It strengthens the model protection
story but is separable from the co-optimisation thesis and can be cut without
weakening it. See [novelty.md](novelty.md).

## Current position

The pipeline runs on the board and the headline result is measured: FP32 126.0
ms, INT8 99.3 ms, adaptive 95.9 ms, the adaptive point faster than either pure
build because the cost optimum on this silicon is mixed. The controller reaches
the same mask from both extremes, six demotions from FP32 and four promotions
from INT8. The gate closes and reopens on a corpus that contains silence, the
window is regulated against measured capture overruns, and the idle hook
sleeps: with the whole pipeline live the core is asleep 43.3 percent of wall
time in the adaptive configuration against 23.5 for static FP32, a factor of
1.84.

The introduction slides are built, see
[TRON2026_intro_slides.pptx](TRON2026_intro_slides.pptx) and the content in
[slides_outline.md](slides_outline.md), both regenerated from the raw captures
by `tools/make_slide_figures.py` and `tools/build_slides.js`.

What remains is a current measurement at the IDD jumper for the power axis and
the fixed threshold sweep descoped from M5. End to end accuracy now matches the
core figure, see [benchmarking.md](benchmarking.md). TrustZone is cut, see the rationale in [novelty.md](novelty.md).
