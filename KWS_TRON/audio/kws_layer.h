#pragma once

#include <stdint.h>

/* Layer descriptor executed by the T4 inference core. The exporter fills one
   of these per layer with both an INT8 and an FP32 copy of the weights,
   because precision is selected per layer at runtime. The boundary scales are
   fixed at export time so switching a layer's precision never changes what the
   numbers crossing that boundary mean. See docs/inference_core.md. */

typedef enum {
    KWS_LAYER_CONV = 0,
    KWS_LAYER_DEPTHWISE,
    KWS_LAYER_POINTWISE,
    KWS_LAYER_FULLY_CONNECTED,
} kws_layer_kind_t;

typedef enum {
    KWS_PRECISION_INT8 = 0,
    KWS_PRECISION_FP32 = 1,
} kws_precision_t;

typedef struct {
    const char *name;
    uint8_t  kind;
    uint8_t  relu;

    uint16_t in_h,  in_w,  in_c;
    uint16_t out_h, out_w, out_c;
    uint8_t  kernel_h, kernel_w;
    uint8_t  stride_h, stride_w;
    uint8_t  pad_h,    pad_w;

    /* INT8 path. Weights are OHWI for convolution and pointwise, 1HWC for
       depthwise, matching the CMSIS-NN layout. Multiplier and shift are per
       output channel. */
    const int8_t  *weight_int8;
    const int32_t *bias_int32;
    const int32_t *multiplier;
    const int32_t *shift;
    int32_t input_offset;
    int32_t output_offset;
    int32_t activation_min;
    int32_t activation_max;

    /* FP32 path, same layout, dequantised weights. */
    const float *weight_fp32;
    const float *bias_fp32;

    /* Boundary quantisation, fixed at export. */
    float input_scale;
    float output_scale;
} kws_layer_t;
