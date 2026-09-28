#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "app_config.h"

/* T3, the feature stage, publishes the quantised MFCC tensor, see docs/adaptation.md. */

typedef struct {
    uint32_t frames_computed;
    uint32_t frames_skipped;   /* skipped by the gate  */
    uint32_t grid_writes;      /* rows written         */
    uint32_t inferences_queued;
    uint32_t last_frame_cycles;
    uint32_t resyncs;          /* times the producer lapped the frame history */
    uint32_t grid_restarts;    /* grids abandoned because the gate closed mid fill */
    uint32_t grid_corpus_end;  /* corpus index the newest grid row ends at    */
} t3_stats_t;

/* Samples the grid spans, used to score against the right clip. */
#define KWS_GRID_SPAN_SAMPLES \
    ((KWS_FRAMES - 1) * FRAME_STRIDE_SAMPLES + FRAME_SIZE_SAMPLES)

extern t3_stats_t t3_stats;

/* Published tensor, row major, oldest row first, double buffered so a reader is never written. */
extern const int8_t *t3_feature_grid;

/* Corpus position the given published tensor ends at. */
uint32_t t3_grid_corpus_end(const int8_t *grid);

void t3_features_task(INT stacd, void *exinf);

/* Publishes the newest `active` rows leading the tensor, the rest at the input zero point. */
void t3_apply_active_frames(uint32_t active);
