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
#define T2_NOISE_ALPHA       0.02f    /* noise floor leak per quiet block */
#define T2_HANGOVER_BLOCKS   6        /* keep the pipeline awake after speech */

/* ── Timing budget ────────────────────────────────────────────────────────── */
/* Deadline for one classification. At a 160 ms cadence this leaves generous
   headroom, the point is that the bound is enforced and provable. */
#define SYSTEM_CLOCK_HZ      250000000u
#define T4_CYCLES_PER_US     (SYSTEM_CLOCK_HZ / 1000000u)
#define T4_DEADLINE_US       40000u
#define T4_DEADLINE_CYCLES   ((uint32_t)T4_DEADLINE_US * T4_CYCLES_PER_US)

/* Fraction of the remaining budget a layer may take before the controller
   drops the following layer to INT8, in percent. */
#define T4_LAYER_BUDGET_PCT  120

/* ── Layer streaming pool ─────────────────────────────────────────────────── */
/* Only the working layer's weights occupy SRAM, which is what lets a model
   larger than SRAM run at all. Sized for the largest single layer in FP32, the
   64 by 64 pointwise convolution at 16 KB, plus bias and pool overhead. */
#define KWS_LAYER_POOL_BYTES 20480

/* ── Task priorities, lower value is more urgent in uT-Kernel ─────────────── */
#define PRI_T1_INGEST        5
#define PRI_T2_VARIANCE      6
#define PRI_T3_FEATURES      7
#define PRI_T4_INFERENCE     8
#define PRI_T4_URGENT        4      /* raised by T5 when a deadline is at risk */
#define PRI_T5_CONTROLLER    3
#define PRI_HEARTBEAT        12
#define PRI_BENCH            11

#define STACK_SMALL          1024
#define STACK_MEDIUM         2048
#define STACK_LARGE          4096

/* ── Instrumentation ──────────────────────────────────────────────────────── */
/* Reporting over UART costs cycles, so the benchmark harness collects into RAM
   during a run and prints afterwards. Never print from inside a timed region. */
#define BENCH_ENABLE         1
#define BENCH_HISTORY        64     /* inferences retained per run */

/* Bring up probe for a live microphone, off for normal builds. */
#define KWS_AUDIO_PROBE      0
#define PROBE_SNAP_FRAMES    8192
