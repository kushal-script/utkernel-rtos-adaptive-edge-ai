#pragma once

#define SAMPLE_RATE_HZ      16000
#define FRAME_SIZE_SAMPLES  480     /* 30 ms at 16 kHz */
#define FRAME_STRIDE_MS     10
#define MEL_BINS            40
#define MFCC_COEFFS         10
#define FFT_SIZE            512
#define WINDOW_MIN_SAMPLES  64
#define WINDOW_MAX_SAMPLES  256
#define WINDOW_STEP         16
