#include "t3_features.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <tm/tmonitor.h>

#include "dwt_logger.h"
#include "ipc_objects.h"
#include "kws_features.h"
#include "kws_model.h"
#include "mfcc_tables.h"
#include "signal_source.h"
#include "t2_variance.h"

t3_stats_t t3_stats;

static int16_t raw_frame[FRAME_SIZE_SAMPLES];

/* The sliding history is private. Rows are written oldest first and rotated
   once full, so the newest frame is always the last row. The tensor the
   inference core reads is published from it at inference time, never mutated
   frame by frame, so a running inference is never read out from under. */
static int8_t   history[KWS_FRAMES * KWS_COEFFS];
static uint32_t rows_filled;

/* Two published tensors. A publication fills the one the inference core is
   not reading, then swaps the pointer it reads, so an inference that outlasts
   the stride, which static FP32 does on every classification, still reads a
   tensor nothing writes to. Clobbering it would take two further publications
   inside one inference, an inference of over two strides, which no
   configuration approaches. The core reads the pointer once, when it wakes
   on the ready flag that is raised after the swap. */
typedef struct {
    int8_t   grid[KWS_FRAMES * KWS_COEFFS];
    uint32_t corpus_end;   /* where in the corpus this tensor's audio ends */
} published_t;

/* The corpus position travels inside the buffer so that the one pointer read
   the inference core makes yields the tensor and its ground truth together.
   Read separately, a publication landing during a long inference would pair
   the audio the core saw with the label of the audio that replaced it. */
static published_t published[2];
static uint32_t    publish_index;
const int8_t      *t3_feature_grid = published[0].grid;

_Static_assert(offsetof(published_t, grid) == 0,
               "the grid must lead the buffer, the core's pointer is cast back");

uint32_t t3_grid_corpus_end(const int8_t *grid)
{
    return ((const published_t *)grid)->corpus_end;
}

static void push_row(const float *mfcc)
{
    if (rows_filled >= KWS_FRAMES) {
        memmove(history, history + KWS_COEFFS,
                (size_t)(KWS_FRAMES - 1) * KWS_COEFFS);
        rows_filled = KWS_FRAMES - 1;
    }

    int8_t *row = &history[rows_filled * KWS_COEFFS];
    for (uint32_t c = 0; c < KWS_COEFFS; c++) {
        row[c] = kws_feature_quantise(mfcc[c], c);
    }
    rows_filled++;
    t3_stats.grid_writes++;
}

void t3_apply_active_frames(uint32_t active)
{
    if (active > KWS_FRAMES) {
        active = KWS_FRAMES;
    }
    /* The newest `active` rows become rows 0 to active minus 1 of the tensor
       and the rest read as zero in the model's input space, which is the
       input zero point rather than the byte zero because the activation
       quantisation is asymmetric. That is the shape training produced: a clip
       whose leading frames hold the audio and whose trailing frames are zero.
       On a sliding history the leading frames must be the most recent ones,
       otherwise a shortened context discards the word just spoken and keeps
       the second before it. */
    published_t *next = &published[publish_index ^ 1u];
    size_t       keep = (size_t)active * KWS_COEFFS;
    memcpy(next->grid, history + (KWS_FRAMES - active) * KWS_COEFFS, keep);
    memset(next->grid + keep, (int)kws_input_zero_point, sizeof(next->grid) - keep);
    next->corpus_end = signal_source_completed_corpus();
    t3_stats.grid_corpus_end = next->corpus_end;
    publish_index ^= 1u;
    t3_feature_grid = next->grid;
}

void t3_features_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    float mfcc[MFCC_COEFFS];
    uint32_t consumed = 0;
    uint32_t since_inference = 0;

    memset(history, (int)kws_input_zero_point, sizeof(history));
    memset(published, (int)kws_input_zero_point, sizeof(published));
    published[0].corpus_end = 0;
    published[1].corpus_end = 0;
    publish_index = 0;
    t3_feature_grid = published[0].grid;
    rows_filled = 0;

    for (;;) {
        UINT pattern = 0;
        ER err = tk_wai_flg(flgid_features, FLG_VOICE_ACTIVE,
                            TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        if (err < E_OK) {
            continue;
        }

        /* Non blocking peek at the gate, the controller can suppress the
           feature stage without ever blocking this task. */
        T_RFLG gate;
        if (tk_ref_flg(flgid_gate, &gate) == E_OK &&
            (gate.flgptn & FLG_SKIP_FEATURES) != 0) {
            t3_stats.frames_skipped++;
            /* Frames are not being computed, so the grid is about to have a
               hole in it. Start it again rather than stitching audio from
               either side of a silence into one tensor, which would present
               the model with a clip that never existed and score it against a
               label that cannot describe it. */
            if (rows_filled > 0) {
                rows_filled = 0;
                since_inference = 0;
                t3_stats.grid_restarts++;
            }
            continue;
        }

        /* Emit every frame whose full analysis window has arrived. */
        while (sample_ring_count(&t2_ring) >= consumed + FRAME_SIZE_SAMPLES) {
            uint32_t behind = sample_ring_count(&t2_ring) - consumed;
            if (!sample_ring_peek(&t2_ring, behind - FRAME_SIZE_SAMPLES,
                                  raw_frame, FRAME_SIZE_SAMPLES)) {
                /* The producer has lapped the history this frame needed. Skip
                   to the newest complete window rather than retrying the same
                   unreachable one forever, and count the loss so the benchmark
                   reports it instead of hiding a silent stall. */
                uint32_t available = sample_ring_count(&t2_ring);
                consumed = available > FRAME_SIZE_SAMPLES
                               ? available - FRAME_SIZE_SAMPLES
                               : 0;
                t3_stats.resyncs++;
                break;
            }

            uint32_t started = dwt_read();
            kws_feature_frame(raw_frame, mfcc);
            t3_stats.last_frame_cycles = dwt_read() - started;
            t3_stats.frames_computed++;

            push_row(mfcc);
            consumed += FRAME_STRIDE_SAMPLES;
            since_inference++;

            if (since_inference >= T4_INFERENCE_STRIDE && rows_filled >= KWS_FRAMES) {
                since_inference = 0;
                t3_apply_active_frames(adapt_state.active_frames);
                t3_stats.inferences_queued++;
                tk_set_flg(flgid_inference, FLG_FEATURES_READY);
            }
        }

        /* Release the samples the frames no longer need. */
        if (consumed > FRAME_SIZE_SAMPLES) {
            uint32_t release = consumed - FRAME_SIZE_SAMPLES;
            sample_ring_advance(&t2_ring, release);
            consumed -= release;
        }
    }
}
