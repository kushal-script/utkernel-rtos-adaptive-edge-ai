#include "kws_kernels.h"

#include <math.h>

int32_t kws_requantise(int32_t accumulator, int32_t multiplier, int32_t shift)
{
    int32_t left  = shift > 0 ? shift : 0;
    int32_t right = shift < 0 ? -shift : 0;

    int64_t value = (int64_t)accumulator << left;
    value = (value * (int64_t)multiplier + ((int64_t)1 << 30)) >> 31;

    if (right > 0) {
        int64_t rounding = (int64_t)1 << (right - 1);
        value = (value + rounding) >> right;
    }
    return (int32_t)value;
}

static inline int32_t clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

void kws_conv_int8(const kws_layer_t *layer, const int8_t *input,
                   const int8_t *weights, const int32_t *bias, int8_t *output)
{
    const int32_t in_h = layer->in_h, in_w = layer->in_w, in_c = layer->in_c;
    const int32_t out_h = layer->out_h, out_w = layer->out_w, out_c = layer->out_c;
    const int32_t kh_n = layer->kernel_h, kw_n = layer->kernel_w;

    for (int32_t oh = 0; oh < out_h; oh++) {
        for (int32_t ow = 0; ow < out_w; ow++) {
            for (int32_t oc = 0; oc < out_c; oc++) {
                int32_t acc = bias[oc];
                for (int32_t kh = 0; kh < kh_n; kh++) {
                    int32_t ih = oh * layer->stride_h - layer->pad_h + kh;
                    if (ih < 0 || ih >= in_h) {
                        continue;
                    }
                    for (int32_t kw = 0; kw < kw_n; kw++) {
                        int32_t iw = ow * layer->stride_w - layer->pad_w + kw;
                        if (iw < 0 || iw >= in_w) {
                            continue;
                        }
                        const int8_t *in_px = &input[(ih * in_w + iw) * in_c];
                        const int8_t *wt = &weights[((oc * kh_n + kh) * kw_n + kw) * in_c];
                        for (int32_t ic = 0; ic < in_c; ic++) {
                            acc += ((int32_t)in_px[ic] + layer->input_offset) *
                                   (int32_t)wt[ic];
                        }
                    }
                }
                int32_t value = kws_requantise(acc, layer->multiplier[oc],
                                               layer->shift[oc]);
                value += layer->output_offset;
                output[(oh * out_w + ow) * out_c + oc] =
                    (int8_t)clamp(value, layer->activation_min,
                                  layer->activation_max);
            }
        }
    }
}

void kws_depthwise_int8(const kws_layer_t *layer, const int8_t *input,
                        const int8_t *weights, const int32_t *bias, int8_t *output)
{
    const int32_t in_h = layer->in_h, in_w = layer->in_w, channels = layer->in_c;
    const int32_t out_h = layer->out_h, out_w = layer->out_w;
    const int32_t kh_n = layer->kernel_h, kw_n = layer->kernel_w;

    for (int32_t oh = 0; oh < out_h; oh++) {
        for (int32_t ow = 0; ow < out_w; ow++) {
            for (int32_t c = 0; c < channels; c++) {
                int32_t acc = bias[c];
                for (int32_t kh = 0; kh < kh_n; kh++) {
                    int32_t ih = oh * layer->stride_h - layer->pad_h + kh;
                    if (ih < 0 || ih >= in_h) {
                        continue;
                    }
                    for (int32_t kw = 0; kw < kw_n; kw++) {
                        int32_t iw = ow * layer->stride_w - layer->pad_w + kw;
                        if (iw < 0 || iw >= in_w) {
                            continue;
                        }
                        int32_t sample = (int32_t)input[(ih * in_w + iw) * channels + c];
                        int32_t weight = (int32_t)weights[(kh * kw_n + kw) * channels + c];
                        acc += (sample + layer->input_offset) * weight;
                    }
                }
                int32_t value = kws_requantise(acc, layer->multiplier[c],
                                               layer->shift[c]);
                value += layer->output_offset;
                output[(oh * out_w + ow) * channels + c] =
                    (int8_t)clamp(value, layer->activation_min,
                                  layer->activation_max);
            }
        }
    }
}

void kws_fully_connected_int8(const kws_layer_t *layer, const int8_t *input,
                              const int8_t *weights, const int32_t *bias,
                              int8_t *output)
{
    const int32_t in_c = layer->in_c, out_c = layer->out_c;

    for (int32_t oc = 0; oc < out_c; oc++) {
        int32_t acc = bias[oc];
        const int8_t *row = &weights[oc * in_c];
        for (int32_t ic = 0; ic < in_c; ic++) {
            acc += ((int32_t)input[ic] + layer->input_offset) * (int32_t)row[ic];
        }
        int32_t value = kws_requantise(acc, layer->multiplier[oc], layer->shift[oc]);
        value += layer->output_offset;
        output[oc] = (int8_t)clamp(value, layer->activation_min,
                                   layer->activation_max);
    }
}

void kws_conv_fp32(const kws_layer_t *layer, const float *input,
                   const float *weights, const float *bias, float *output)
{
    const int32_t in_h = layer->in_h, in_w = layer->in_w, in_c = layer->in_c;
    const int32_t out_h = layer->out_h, out_w = layer->out_w, out_c = layer->out_c;
    const int32_t kh_n = layer->kernel_h, kw_n = layer->kernel_w;

    for (int32_t oh = 0; oh < out_h; oh++) {
        for (int32_t ow = 0; ow < out_w; ow++) {
            for (int32_t oc = 0; oc < out_c; oc++) {
                float acc = bias[oc];
                for (int32_t kh = 0; kh < kh_n; kh++) {
                    int32_t ih = oh * layer->stride_h - layer->pad_h + kh;
                    if (ih < 0 || ih >= in_h) {
                        continue;
                    }
                    for (int32_t kw = 0; kw < kw_n; kw++) {
                        int32_t iw = ow * layer->stride_w - layer->pad_w + kw;
                        if (iw < 0 || iw >= in_w) {
                            continue;
                        }
                        const float *in_px = &input[(ih * in_w + iw) * in_c];
                        const float *wt = &weights[((oc * kh_n + kh) * kw_n + kw) * in_c];
                        for (int32_t ic = 0; ic < in_c; ic++) {
                            acc += in_px[ic] * wt[ic];
                        }
                    }
                }
                output[(oh * out_w + ow) * out_c + oc] =
                    layer->relu ? (acc > 0.0f ? acc : 0.0f) : acc;
            }
        }
    }
}

void kws_depthwise_fp32(const kws_layer_t *layer, const float *input,
                        const float *weights, const float *bias, float *output)
{
    const int32_t in_h = layer->in_h, in_w = layer->in_w, channels = layer->in_c;
    const int32_t out_h = layer->out_h, out_w = layer->out_w;
    const int32_t kh_n = layer->kernel_h, kw_n = layer->kernel_w;

    for (int32_t oh = 0; oh < out_h; oh++) {
        for (int32_t ow = 0; ow < out_w; ow++) {
            for (int32_t c = 0; c < channels; c++) {
                float acc = bias[c];
                for (int32_t kh = 0; kh < kh_n; kh++) {
                    int32_t ih = oh * layer->stride_h - layer->pad_h + kh;
                    if (ih < 0 || ih >= in_h) {
                        continue;
                    }
                    for (int32_t kw = 0; kw < kw_n; kw++) {
                        int32_t iw = ow * layer->stride_w - layer->pad_w + kw;
                        if (iw < 0 || iw >= in_w) {
                            continue;
                        }
                        acc += input[(ih * in_w + iw) * channels + c] *
                               weights[(kh * kw_n + kw) * channels + c];
                    }
                }
                output[(oh * out_w + ow) * channels + c] =
                    layer->relu ? (acc > 0.0f ? acc : 0.0f) : acc;
            }
        }
    }
}

void kws_fully_connected_fp32(const kws_layer_t *layer, const float *input,
                              const float *weights, const float *bias, float *output)
{
    for (int32_t oc = 0; oc < layer->out_c; oc++) {
        float acc = bias[oc];
        const float *row = &weights[oc * layer->in_c];
        for (int32_t ic = 0; ic < layer->in_c; ic++) {
            acc += input[ic] * row[ic];
        }
        output[oc] = layer->relu ? (acc > 0.0f ? acc : 0.0f) : acc;
    }
}

void kws_avgpool_int8(const int8_t *input, int8_t *output,
                      int32_t height, int32_t width, int32_t channels)
{
    const int32_t count = height * width;

    for (int32_t c = 0; c < channels; c++) {
        int32_t sum = 0;
        for (int32_t i = 0; i < count; i++) {
            sum += input[i * channels + c];
        }
        float mean = (float)sum / (float)count;
        int32_t rounded = (int32_t)lrintf(mean);
        output[c] = (int8_t)clamp(rounded, -128, 127);
    }
}

void kws_avgpool_fp32(const float *input, float *output,
                      int32_t height, int32_t width, int32_t channels)
{
    const int32_t count = height * width;

    for (int32_t c = 0; c < channels; c++) {
        float sum = 0.0f;
        for (int32_t i = 0; i < count; i++) {
            sum += input[i * channels + c];
        }
        output[c] = sum / (float)count;
    }
}

void kws_dequantise(const int8_t *input, float *output, uint32_t count,
                    float scale, int32_t zero_point)
{
    for (uint32_t i = 0; i < count; i++) {
        output[i] = ((float)input[i] - (float)zero_point) * scale;
    }
}

void kws_quantise(const float *input, int8_t *output, uint32_t count,
                  float scale, int32_t zero_point)
{
    for (uint32_t i = 0; i < count; i++) {
        int32_t q = (int32_t)lrintf(input[i] / scale) + zero_point;
        output[i] = (int8_t)clamp(q, -128, 127);
    }
}
