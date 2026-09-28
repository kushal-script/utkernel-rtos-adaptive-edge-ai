#pragma once

#include <stdint.h>

#include "mfcc_config.h"

/* Radix 2 FFT for the feature front end, checked against model/kws/features.py by tools/verify_device_core.py. */

void kws_fft_init(void);

/* Power spectrum of FFT_BINS values from FFT_SIZE real samples; the input is consumed. */
void kws_fft_power(float *time_domain, float *power_out);
