#include "kws_features.h"

#include <math.h>

#include "kws_fft.h"
#include "kws_model.h"
#include "mfcc_tables.h"

static float frame_buffer[FFT_SIZE];
static float power[FFT_BINS];
static float mel_energy[MEL_BINS];

void kws_features_init(void)
{
    kws_fft_init();
}

void kws_feature_frame(const int16_t *samples, float *mfcc_out)
{
    for (uint32_t i = 0; i < FRAME_SIZE_SAMPLES; i++) {
        frame_buffer[i] = ((float)samples[i] / 32768.0f) * mfcc_window[i];
    }
    for (uint32_t i = FRAME_SIZE_SAMPLES; i < FFT_SIZE; i++) {
        frame_buffer[i] = 0.0f;
    }

    kws_fft_power(frame_buffer, power);

    /* Each mel band covers a contiguous run of bins, so only those are visited. */
    const float *weight = mfcc_band_weight;
    for (uint32_t m = 0; m < MEL_BINS; m++) {
        const float *bin = &power[mfcc_band_start[m]];
        uint32_t length = mfcc_band_length[m];
        float sum = 0.0f;
        for (uint32_t b = 0; b < length; b++) {
            sum += weight[b] * bin[b];
        }
        weight += length;
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

int8_t kws_feature_quantise(float value, uint32_t coefficient)
{
    float standardised =
        (value - kws_feature_mean[coefficient]) / kws_feature_std[coefficient];
    float q = standardised / kws_input_scale + (float)kws_input_zero_point;
    int32_t rounded = (int32_t)lrintf(q);
    if (rounded < -128) {
        rounded = -128;
    } else if (rounded > 127) {
        rounded = 127;
    }
    return (int8_t)rounded;
}
