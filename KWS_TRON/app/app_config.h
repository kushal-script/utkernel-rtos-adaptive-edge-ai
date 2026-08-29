#pragma once

#include "mfcc_config.h"

/* Central tunables for the adaptive pipeline. Anything a run can be varied by
   lives here, so an experiment is described by this file plus the controller
   state it converged to. Rationale for the values is in docs/adaptation.md. */

/* ── Signal source ────────────────────────────────────────────────────────── */
/* KWS_SOURCE_REPLAY streams a flash resident corpus through GPDMA, which
   exercises the same DMA, half transfer interrupt, and event flag path a live
   sensor would. KWS_SOURCE_I2S reads the INMP441 when one is attached.
   See docs/signal_source.md. */
#define KWS_SOURCE_REPLAY    0
#define KWS_SOURCE_I2S       1
#define KWS_SIGNAL_SOURCE    KWS_SOURCE_REPLAY

/* ── Capture ──────────────────────────────────────────────────────────────── */
/* The capture window is the DMA half block, the unit the pipeline is woken on.
   It is adaptive between the bounds below, which are the range the program
   plan specifies. The physical buffer is always sized for the maximum. */
#define T1_WINDOW_MIN        WINDOW_MIN_SAMPLES
#define T1_WINDOW_MAX        WINDOW_MAX_SAMPLES
#define T1_WINDOW_STEP       WINDOW_STEP
#define T1_WINDOW_DEFAULT    T1_WINDOW_MAX

#define T1_CAPTURE_HALVES    2
#define T1_CAPTURE_SAMPLES   (T1_WINDOW_MAX * T1_CAPTURE_HALVES)

/* Samples buffered for the feature stage, must hold one analysis frame plus a
   hop so a frame is never torn across a refill. */
#define T3_SAMPLE_RING       1024

/* ── Feature grid ─────────────────────────────────────────────────────────── */
#define KWS_FRAMES           MFCC_FRAMES
#define KWS_COEFFS           MFCC_COEFFS

/* Frames of new audio between inferences. 8 frames at a 20 ms hop is one
   classification every 160 ms, which keeps the pipeline responsive without
   running the model on every hop. */
#define T4_INFERENCE_STRIDE  8

/* Active frames the controller may select, the rest of the grid stays zero.
   The model is trained across this whole range, see docs/adaptation.md. */
#define T3_ACTIVE_FRAMES_MIN 16
#define T3_ACTIVE_FRAMES_MAX KWS_FRAMES

/* ── Voice activity gate ──────────────────────────────────────────────────── */
/* Starting threshold only. T5 tracks the observed noise floor and places the
   gate a margin above it, so these are seeds rather than fixed constants. */
#define T2_VAD_MARGIN_DB     9.0f
#define T2_HANGOVER_BLOCKS   6        /* keep the pipeline awake after speech */

/* Blocks spent seeding the noise floor before the gate is trusted. Without a
   seeding phase the gate and the floor deadlock: the floor is only updated on
   blocks the gate calls quiet, and the gate calls nothing quiet until it has a
   threshold, which needs a floor. During seeding the floor tracks the minimum
   observed energy, which finds the true floor even if speech is present. */
#define T2_FLOOR_SEED_BLOCKS 64

/* ── Timing budget ────────────────────────────────────────────────────────── */
/* Deadline for one classification, set between the measured cost of the two
   pure configurations on this silicon, 102.6 ms for INT8 and 125.1 ms for
   FP32. Full FP32 therefore overruns and the controller demotes layer by
   layer until the deadline holds, which lands on a mixed precision point that
   keeps the most FP32 the budget allows. The margin over the settled cost
   covers the precision boundary conversions, which are real work the pure
   configurations never pay. See docs/adaptation.md. */
#define SYSTEM_CLOCK_HZ      250000000u
#define T4_CYCLES_PER_US     (SYSTEM_CLOCK_HZ / 1000000u)
#define T4_DEADLINE_US       115000u
#define T4_DEADLINE_CYCLES   ((uint32_t)T4_DEADLINE_US * T4_CYCLES_PER_US)

/* Hysteresis on the promotion side, as a percentage of the deadline that a
   promotion must leave free. Without it the controller promotes right up to the
   deadline and demotes again on the next inference. */
#define T5_PROMOTE_MARGIN_PCT 5

/* Consecutive decisions with no lever change before the controller declares
   itself converged. The benchmark reports the latched mask, so the figure
   describes a settled system rather than whatever was being tried. */
#define T5_CONVERGE_DECISIONS 5

/* Consecutive decisions the capture chain must survive without dropping a block
   before the controller shortens the window again. Shortening wakes the
   pipeline sooner but multiplies the interrupt rate, so the window is widened
   the moment a block is dropped and shortened only after sustained evidence
   that the chain is keeping up. The result is the shortest window this
   hardware actually sustains, found by measurement. */
#define T5_WINDOW_SHRINK_AFTER 8

/* Share of the feature grid one replay clip must cover before a classification
   is scored against that clip's label. Below this the span is too evenly split
   for any label to be correct, and the result is left unscored rather than
   guessed. */
#define REPLAY_SCORE_MAJORITY_PCT 70

/* ── Layer streaming pool ─────────────────────────────────────────────────── */
/* Only the working layer's weights occupy SRAM, which is what lets a model
   larger than SRAM run at all. Sized for the largest single layer in FP32, the
   64 by 64 pointwise convolution at 16 KB, plus bias and pool overhead. */
#define KWS_LAYER_POOL_BYTES 20480

/* ── Task priorities, lower value is more urgent in uT-Kernel ─────────────── */
/* The capture chain outranks inference at every priority inference can hold,
   including the urgent tier. Raising inference above capture would let a late
   inference starve the task that drains the DMA buffer, so a deadline recovery
   would drop audio, which is a worse failure than the deadline it was trying
   to save. */
#define PRI_T5_CONTROLLER    2
#define PRI_T1_INGEST        3
#define PRI_T2_VARIANCE      4
#define PRI_T3_FEATURES      5
#define PRI_T4_URGENT        6      /* raised by T5 when a deadline is at risk */
#define PRI_T4_INFERENCE     8
#define PRI_BENCH            11
#define PRI_HEARTBEAT        12

#define STACK_SMALL          1024
#define STACK_MEDIUM         2048
#define STACK_LARGE          4096

/* ── Instrumentation ──────────────────────────────────────────────────────── */
/* Reporting over UART costs cycles, so the benchmark harness collects into RAM
   during a run and prints afterwards. Never print from inside a timed region. */
#define BENCH_ENABLE         1
#define BENCH_HISTORY        64     /* controller decisions retained */

/* Longest the benchmark waits for the controller to converge before measuring
   anyway. Waiting for convergence rather than a fixed delay is what keeps the
   reported operating point a settled one. */
#define BENCH_SETTLE_MAX_MS  60000u

/* Bring up probe for a live microphone, off for normal builds. */
#define KWS_AUDIO_PROBE      0
#define PROBE_SNAP_FRAMES    8192
