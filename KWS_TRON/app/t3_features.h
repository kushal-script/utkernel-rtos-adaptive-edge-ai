#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "app_config.h"

/* T3, the feature stage. Turns the sample stream into the quantised MFCC grid
   the model consumes, and is the stage the feature gate can skip entirely.

   The tensor the inference core reads is published from a private sliding
   history at inference time, in the layout the core consumes directly. When
   the controller shortens the context, the newest active rows lead the tensor
   and the rest hold the value that represents zero in the model's input
   space, which is the operating point the model was trained on. See
   docs/adaptation.md. */

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

/* Samples of audio the grid spans, the newest row's end back to the oldest
   row's start. Used to score a classification against the right clip. */
#define KWS_GRID_SPAN_SAMPLES \
    ((KWS_FRAMES - 1) * FRAME_STRIDE_SAMPLES + FRAME_SIZE_SAMPLES)

extern t3_stats_t t3_stats;

/* The tensor the inference core reads: row major, KWS_FRAMES by KWS_COEFFS,
   already quantised, oldest row first. It points at one of two buffers and
   is swapped on publication, so the buffer an inference is reading is never
   the one being written. */
extern const int8_t *t3_feature_grid;

/* The corpus position the given published tensor's audio ends at. Takes the
   pointer the core read, so the answer always belongs to that tensor. */
uint32_t t3_grid_corpus_end(const int8_t *grid);

void t3_features_task(INT stacd, void *exinf);

/* Publishes the tensor: the newest `active` rows of the history lead it and
   the rest read as zero in the model's input space. Called once per
   inference, immediately before the ready flag is raised. */
void t3_apply_active_frames(uint32_t active);
