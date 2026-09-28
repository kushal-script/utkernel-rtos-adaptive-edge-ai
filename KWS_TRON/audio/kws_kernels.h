#pragma once

#include <stdint.h>

#include "kws_layer.h"

/* Layer kernels, INT8 and FP32; NHWC tensors, OHWI weights, 1HWC depthwise, as model/kws/convert.py. */

int32_t kws_requantise(int32_t accumulator, int32_t multiplier, int32_t shift);

/* INT8 kernels take folded and row sum tables, NULL for either uses the plain path, all bit identical. */
void kws_conv_int8(const kws_layer_t *layer, const int8_t *input,
                   const int8_t *weights, const int32_t *bias,
                   const int32_t *folded, const int32_t *rowsum,
                   int8_t *output);
void kws_depthwise_int8(const kws_layer_t *layer, const int8_t *input,
                        const int8_t *weights, const int32_t *bias, int8_t *output);
void kws_fully_connected_int8(const kws_layer_t *layer, const int8_t *input,
                              const int8_t *weights, const int32_t *bias,
                              const int32_t *folded, int8_t *output);

void kws_conv_fp32(const kws_layer_t *layer, const float *input,
                   const float *weights, const float *bias, float *output);
void kws_depthwise_fp32(const kws_layer_t *layer, const float *input,
                        const float *weights, const float *bias, float *output);
void kws_fully_connected_fp32(const kws_layer_t *layer, const float *input,
                              const float *weights, const float *bias, float *output);

/* Global average pool, dimensions passed because it runs on the previous layer's tensor. */
void kws_avgpool_int8(const int8_t *input, int8_t *output,
                      int32_t height, int32_t width, int32_t channels);
void kws_avgpool_fp32(const float *input, float *output,
                      int32_t height, int32_t width, int32_t channels);

/* Boundary conversions, the scale is fixed at export so a switch preserves meaning. */
void kws_dequantise(const int8_t *input, float *output, uint32_t count,
                    float scale, int32_t zero_point);
void kws_quantise(const float *input, int8_t *output, uint32_t count,
                  float scale, int32_t zero_point);
