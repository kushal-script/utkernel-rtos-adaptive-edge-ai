#include "kws_fft.h"

#include <math.h>

/* Decimation in time radix 2, iterative, with a precomputed twiddle table and
   a bit reversal permutation table. Both tables are built once at startup.

   The transform is complex of length FFT_SIZE with the imaginary part of the
   input set to zero. That is simpler and easier to verify than a packed real
   transform of half the length, and the cost is measured by the benchmark
   rather than assumed, so the honest number is what gets reported. */

static float twiddle_re[FFT_SIZE / 2];
static float twiddle_im[FFT_SIZE / 2];
static uint16_t bit_reverse[FFT_SIZE];
static float scratch_im[FFT_SIZE];

static uint16_t reverse_bits(uint16_t value, uint16_t bits)
{
    uint16_t result = 0;
    for (uint16_t i = 0; i < bits; i++) {
        result = (uint16_t)((result << 1) | (value & 1u));
        value >>= 1;
    }
    return result;
}

void kws_fft_init(void)
{
    uint16_t bits = 0;
    while ((1u << bits) < FFT_SIZE) {
        bits++;
    }

    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        bit_reverse[i] = reverse_bits(i, bits);
    }

    for (uint16_t i = 0; i < FFT_SIZE / 2; i++) {
        float angle = -2.0f * (float)M_PI * (float)i / (float)FFT_SIZE;
        twiddle_re[i] = cosf(angle);
        twiddle_im[i] = sinf(angle);
    }
}

void kws_fft_power(float *time_domain, float *power_out)
{
    float *re = time_domain;
    float *im = scratch_im;

    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        uint16_t j = bit_reverse[i];
        if (j > i) {
            float tmp = re[i];
            re[i] = re[j];
            re[j] = tmp;
        }
        im[i] = 0.0f;
    }

    for (uint16_t span = 1; span < FFT_SIZE; span <<= 1) {
        uint16_t step = (uint16_t)(FFT_SIZE / (span * 2));
        for (uint16_t start = 0; start < FFT_SIZE; start += span * 2) {
            uint16_t angle = 0;
            for (uint16_t offset = 0; offset < span; offset++) {
                uint16_t a = (uint16_t)(start + offset);
                uint16_t b = (uint16_t)(a + span);

                float wr = twiddle_re[angle];
                float wi = twiddle_im[angle];
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] = re[a] + tr;
                im[a] = im[a] + ti;

                angle = (uint16_t)(angle + step);
            }
        }
    }

    for (uint16_t i = 0; i < FFT_BINS; i++) {
        power_out[i] = re[i] * re[i] + im[i] * im[i];
    }
}
