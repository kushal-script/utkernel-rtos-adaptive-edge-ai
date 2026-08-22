#pragma once

#include <stdint.h>

#include "mfcc_config.h"

/* One MFCC frame, free of any kernel dependency so it can be compiled and
   checked against the NumPy front end on a host before it runs on the board.
   T3 owns the scheduling and the grid, this owns only the arithmetic. */

void kws_features_init(void);

/* FRAME_SIZE_SAMPLES of int16 audio in, MFCC_COEFFS coefficients out. */
void kws_feature_frame(const int16_t *samples, float *mfcc_out);

/* Standardises and quantises one coefficient into the model's input space. */
int8_t kws_feature_quantise(float value, uint32_t coefficient);
