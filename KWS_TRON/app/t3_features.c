#include "t3_features.h"

#include <math.h>
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
int8_t t3_feature_grid[KWS_FRAMES * KWS_COEFFS];

static int16_t raw_frame[FRAME_SIZE_SAMPLES];

/* Rows are written oldest first and the grid is rotated once it is full, so
   the inference core always reads a contiguous, time ordered tensor. */
static uint32_t rows_filled;

static void push_row(const float *mfcc)
{
    if (rows_filled >= KWS_FRAMES) {
        memmove(t3_feature_grid, t3_feature_grid + KWS_COEFFS,
                (size_t)(KWS_FRAMES - 1) * KWS_COEFFS);
        rows_filled = KWS_FRAMES - 1;
    }

    int8_t *row = &t3_feature_grid[rows_filled * KWS_COEFFS];
    for (uint32_t c = 0; c < KWS_COEFFS; c++) {
        row[c] = kws_feature_quantise(mfcc[c], c);
    }
    rows_filled++;
    t3_stats.grid_writes++;
}

void t3_apply_active_frames(uint32_t active)
{
    if (active >= KWS_FRAMES) {
        return;
    }
    /* Zero in the model's input space is the input zero point, not the byte
       zero, because the activation quantisation is asymmetric. */
    int8_t zero = (int8_t)kws_input_zero_point;
    for (uint32_t f = active; f < KWS_FRAMES; f++) {
        memset(&t3_feature_grid[f * KWS_COEFFS], zero, KWS_COEFFS);
    }
}

void t3_features_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    float mfcc[MFCC_COEFFS];
    uint32_t consumed = 0;
    uint32_t since_inference = 0;

    memset(t3_feature_grid, (int)kws_input_zero_point, sizeof(t3_feature_grid));
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
                /* Where in the corpus this grid came from, so the classification
                   is scored against the audio it actually saw. */
                t3_stats.grid_corpus_end = signal_source_completed_corpus();
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
