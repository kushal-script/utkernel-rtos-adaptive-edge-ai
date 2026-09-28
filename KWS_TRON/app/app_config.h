#pragma once

#include "mfcc_config.h"

/* Central tunables for the adaptive pipeline, rationale in docs/adaptation.md. */

/* Signal source */
/* REPLAY streams a flash corpus through GPDMA, I2S reads an INMP441, see docs/signal_source.md. */
#define KWS_SOURCE_REPLAY    0
#define KWS_SOURCE_I2S       1
#define KWS_SIGNAL_SOURCE    KWS_SOURCE_REPLAY

/* Capture */
/* The window is the DMA half block, adaptive within these bounds; the buffer is sized for the maximum. */
#define T1_WINDOW_MIN        WINDOW_MIN_SAMPLES
#define T1_WINDOW_MAX        WINDOW_MAX_SAMPLES
#define T1_WINDOW_STEP       WINDOW_STEP
#define T1_WINDOW_DEFAULT    T1_WINDOW_MAX

#define T1_CAPTURE_HALVES    2
#define T1_CAPTURE_SAMPLES   (T1_WINDOW_MAX * T1_CAPTURE_HALVES)

/* One analysis frame plus a hop, so a frame is never torn across a refill. */
#define T3_SAMPLE_RING       1024

/* Feature grid */
#define KWS_FRAMES           MFCC_FRAMES
#define KWS_COEFFS           MFCC_COEFFS

/* Six frames at a 20 ms hop is one classification every 120 ms, the period the deadline derives from. */
#define T4_INFERENCE_STRIDE  6

/* The active frame floor sits where the context curve is still flat, see docs/adaptation.md. */
#define T3_ACTIVE_FRAMES_MIN 32
#define T3_ACTIVE_FRAMES_MAX KWS_FRAMES

/* Voice activity gate */
/* Seeds only, T5 places the gate a margin above the learned noise floor. */
#define T2_VAD_MARGIN_DB     9.0f
#define T2_HANGOVER_BLOCKS   6        /* keep the pipeline awake after speech */

/* Seed the floor from the minimum energy first, or the gate and the floor deadlock. */
#define T2_FLOOR_SEED_BLOCKS 64

/* Timing budget */
/* The deadline is the classification period, derived from the stride rather than the measured costs. */
#define SYSTEM_CLOCK_HZ      250000000u
#define T4_CYCLES_PER_US     (SYSTEM_CLOCK_HZ / 1000000u)
#define T4_DEADLINE_US       ((uint32_t)T4_INFERENCE_STRIDE * FRAME_STRIDE_SAMPLES \
                              * 1000000u / SAMPLE_RATE_HZ)
#define T4_DEADLINE_CYCLES   ((uint32_t)T4_DEADLINE_US * T4_CYCLES_PER_US)

/* Decisions with no lever change before the controller declares convergence. */
#define T5_CONVERGE_DECISIONS 5

/* Clean decisions before the window shortens again, it widens on the first dropped block. */
#define T5_WINDOW_SHRINK_AFTER 8

/* Share of the grid one clip must cover before a classification is scored against its label. */
#define REPLAY_SCORE_MAJORITY_PCT 70

/* Layer streaming pool */
/* Sized for the largest FP32 layer, the 64 by 64 pointwise at 16 KB, plus bias and pool overhead. */
#define KWS_LAYER_POOL_BYTES 20480

/* Task priorities, lower value is more urgent in uT-Kernel */
/* Capture outranks inference at every tier, so a late inference never starves the DMA drain. */
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

/* Instrumentation */
/* The harness collects into RAM and prints afterwards, never inside a timed region. */
#define BENCH_ENABLE         1
#define BENCH_HISTORY        64     /* controller decisions retained */

/* Longest wait for convergence before measuring anyway. */
#define BENCH_SETTLE_MAX_MS  60000u

/* Length of each live end to end measurement window. */
#define BENCH_LIVE_MS        30000u

/* Sleep in the kernel idle hook, see docs/power.md. */
#define KWS_IDLE_SLEEP       1

/* Bring up probe for a live microphone, off for normal builds. */
#define KWS_AUDIO_PROBE      0
#define PROBE_SNAP_FRAMES    8192
