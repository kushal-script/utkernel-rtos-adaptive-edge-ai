#pragma once

/* Feature geometry, mirrors FeatureConfig in model/kws/features.py; change both and regenerate. */

#define SAMPLE_RATE_HZ      16000
#define CLIP_SAMPLES        16000

#define FRAME_SIZE_SAMPLES  480     /* 30 ms analysis frame */
#define FRAME_STRIDE_SAMPLES 320    /* 20 ms hop */
#define FFT_SIZE            512     /* smallest power of two holding a frame */
#define FFT_BINS            (FFT_SIZE / 2 + 1)

#define MEL_BINS            40
#define MFCC_COEFFS         10
#define MFCC_FRAMES         49      /* 1 + (CLIP_SAMPLES - FRAME) / STRIDE */

#define MEL_LOW_HZ          20.0f
#define MEL_HIGH_HZ         4000.0f
#define MEL_LOG_FLOOR       1e-6f

/* Adaptive capture window bounds from the program plan, in samples. */
#define WINDOW_MIN_SAMPLES  64
#define WINDOW_MAX_SAMPLES  256
#define WINDOW_STEP         16
