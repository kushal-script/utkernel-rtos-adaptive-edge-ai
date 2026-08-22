#pragma once

#include <stdint.h>

#include "mfcc_config.h"

/* Radix 2 real input FFT sized for the feature front end.

   Kept in the project rather than pulled from a library so the transform can
   be instrumented per call by the cycle counter and so the build has no
   external dependency. It is validated against the NumPy reference in
   model/kws/features.py by tools/verify_fft.py, which is the same reference
   the trained model was built on. */

void kws_fft_init(void);

/* Real input of FFT_SIZE samples to the power spectrum of FFT_BINS values.
   The input is consumed, the caller keeps no ordering assumptions about it. */
void kws_fft_power(float *time_domain, float *power_out);
