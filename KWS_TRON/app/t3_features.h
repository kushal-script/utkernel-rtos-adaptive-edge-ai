#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "app_config.h"

/* T3, the feature stage. Turns the sample stream into the quantised MFCC grid
   the model consumes, and is the stage the feature gate can skip entirely.

   The grid is held in the layout the inference core reads directly, so no
   copy or rearrangement happens between the two stages. Rows beyond the
   controller's active frame count are left at the value that represents zero
   in the model's input space, which is the operating point the model was
   trained on. See docs/adaptation.md. */

typedef struct {
    uint32_t frames_computed;
    uint32_t frames_skipped;   /* skipped by the gate  */
    uint32_t grid_writes;      /* rows written         */
    uint32_t inferences_queued;
    uint32_t last_frame_cycles;
    uint32_t resyncs;          /* times the producer lapped the frame history */
    uint32_t grid_corpus_end;  /* corpus index the newest grid row ends at    */
} t3_stats_t;

/* Samples of audio the grid spans, the newest row's end back to the oldest
   row's start. Used to score a classification against the right clip. */
#define KWS_GRID_SPAN_SAMPLES \
    ((KWS_FRAMES - 1) * FRAME_STRIDE_SAMPLES + FRAME_SIZE_SAMPLES)

extern t3_stats_t t3_stats;

/* Row major, KWS_FRAMES by KWS_COEFFS, already quantised. Oldest row first. */
extern int8_t t3_feature_grid[KWS_FRAMES * KWS_COEFFS];

void t3_features_task(INT stacd, void *exinf);

/* Rewrites the grid so only `active` rows hold data, the rest read as zero in
   the model's input space. Called when the controller changes the window. */
void t3_apply_active_frames(uint32_t active);
