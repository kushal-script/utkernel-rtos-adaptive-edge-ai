#pragma once

#include <stdint.h>

#include "kws_layer.h"

/* Layer kernels for the inference core, one INT8 and one FP32 form each.

   Tensors are NHWC and weights are OHWI, except depthwise weights which are
   1HWC, matching the layout the exporter emits. The INT8 path reproduces the
   arithmetic in model/kws/convert.py exactly, including the requantisation
   rounding, so the NumPy reference there is the golden model the device is
   checked against. */

int32_t kws_requantise(int32_t accumulator, int32_t multiplier, int32_t shift);

/* The INT8 kernels take a folded accumulator table alongside the raw bias,
   folded[oc] = bias[oc] + input_offset * sum(weights[oc]). For outputs whose
   whole kernel window is inside the input the offset add then vanishes from
   the inner loop, which is what lets the loop run as packed pairs. Outputs
   touching the padding fall back to the original per element arithmetic, so
   the result is bit identical to the unfolded form everywhere. Pass NULL to
   use the original path throughout. */
void kws_conv_int8(const kws_layer_t *layer, const int8_t *input,
                   const int8_t *weights, const int32_t *bias,
                   const int32_t *folded, int8_t *output);
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

/* Global average pool over height and width, channels preserved. Dimensions
   are passed explicitly because the pool runs on the tensor produced by the
   previous layer, not on the shape declared by the layer that consumes it. */
void kws_avgpool_int8(const int8_t *input, int8_t *output,
                      int32_t height, int32_t width, int32_t channels);
void kws_avgpool_fp32(const float *input, float *output,
                      int32_t height, int32_t width, int32_t channels);

/* Boundary conversions used when consecutive layers run at different
   precision. The scale is fixed at export, so a switch preserves meaning. */
void kws_dequantise(const int8_t *input, float *output, uint32_t count,
                    float scale, int32_t zero_point);
void kws_quantise(const float *input, int8_t *output, uint32_t count,
                  float scale, int32_t zero_point);
