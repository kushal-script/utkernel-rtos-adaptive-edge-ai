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

/* Private sliding history, newest frame last, published to the core at inference time. */
static int8_t   history[KWS_FRAMES * KWS_COEFFS];
static uint32_t rows_filled;

/* Two published tensors swapped on publication, so a long inference never reads one being written. */
typedef struct {
    int8_t   grid[KWS_FRAMES * KWS_COEFFS];
    uint32_t corpus_end;   /* where in the corpus this tensor's audio ends */
} published_t;

/* The corpus position travels in the buffer, so the tensor and its label are read together. */
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
    /* Newest rows lead, the rest hold the input zero point, the layout training produced. */
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

        /* Non blocking peek at the gate. */
        T_RFLG gate;
        if (tk_ref_flg(flgid_gate, &gate) == E_OK &&
            (gate.flgptn & FLG_SKIP_FEATURES) != 0) {
            t3_stats.frames_skipped++;
            /* The grid would have a hole, so start it again rather than stitch across silence. */
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
                /* The producer lapped this frame, skip to the newest window and count the loss. */
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
