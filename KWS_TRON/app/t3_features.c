#include "t3_features.h"

#include <math.h>
#include <string.h>

#include <tm/tmonitor.h>

#include "dwt_logger.h"
#include "ipc_objects.h"
#include "kws_fft.h"
#include "kws_model.h"
#include "mfcc_tables.h"
#include "t2_variance.h"

t3_stats_t t3_stats;
int8_t t3_feature_grid[KWS_FRAMES * KWS_COEFFS];

static float frame_buffer[FFT_SIZE];
static float power[FFT_BINS];
static float mel_energy[MEL_BINS];
static int16_t raw_frame[FRAME_SIZE_SAMPLES];

/* Rows are written oldest first and the grid is rotated once it is full, so
   the inference core always reads a contiguous, time ordered tensor. */
static uint32_t rows_filled;

static int8_t quantise_feature(float value)
{
    float q = value / kws_input_scale + (float)kws_input_zero_point;
    int32_t rounded = (int32_t)lrintf(q);
    if (rounded < -128) {
        rounded = -128;
    } else if (rounded > 127) {
        rounded = 127;
    }
    return (int8_t)rounded;
}

void t3_compute_frame(const int16_t *samples, float *mfcc_out)
{
    for (uint32_t i = 0; i < FRAME_SIZE_SAMPLES; i++) {
        frame_buffer[i] = ((float)samples[i] / 32768.0f) * mfcc_window[i];
    }
    for (uint32_t i = FRAME_SIZE_SAMPLES; i < FFT_SIZE; i++) {
        frame_buffer[i] = 0.0f;
    }

    kws_fft_power(frame_buffer, power);

    for (uint32_t m = 0; m < MEL_BINS; m++) {
        const float *row = &mfcc_filterbank[m * FFT_BINS];
        float sum = 0.0f;
        for (uint32_t b = 0; b < FFT_BINS; b++) {
            sum += row[b] * power[b];
        }
        mel_energy[m] = logf(sum > MEL_LOG_FLOOR ? sum : MEL_LOG_FLOOR);
    }

    for (uint32_t c = 0; c < MFCC_COEFFS; c++) {
        const float *row = &mfcc_dct[c * MEL_BINS];
        float sum = 0.0f;
        for (uint32_t m = 0; m < MEL_BINS; m++) {
            sum += row[m] * mel_energy[m];
        }
        mfcc_out[c] = sum;
    }
}

static void push_row(const float *mfcc)
{
    if (rows_filled >= KWS_FRAMES) {
        memmove(t3_feature_grid, t3_feature_grid + KWS_COEFFS,
                (size_t)(KWS_FRAMES - 1) * KWS_COEFFS);
        rows_filled = KWS_FRAMES - 1;
    }

    int8_t *row = &t3_feature_grid[rows_filled * KWS_COEFFS];
    for (uint32_t c = 0; c < KWS_COEFFS; c++) {
        float standardised = (mfcc[c] - kws_feature_mean[c]) / kws_feature_std[c];
        row[c] = quantise_feature(standardised);
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
            continue;
        }

        /* Emit every frame whose full analysis window has arrived. */
        while (sample_ring_count(&t2_ring) >= consumed + FRAME_SIZE_SAMPLES) {
            uint32_t behind = sample_ring_count(&t2_ring) - consumed;
            if (!sample_ring_peek(&t2_ring, behind - FRAME_SIZE_SAMPLES,
                                  raw_frame, FRAME_SIZE_SAMPLES)) {
                break;
            }

            uint32_t started = dwt_read();
            t3_compute_frame(raw_frame, mfcc);
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
