#include "kws_infer.h"

#include <string.h>

#include "kws_kernels.h"

/* Two arenas, ping ponged between layers. Each must hold the largest
   activation tensor in whichever precision that layer runs, so they are sized
   for float even though most layers use int8. */
static float arena_a[KWS_MAX_TENSOR_ELEMS];
static float arena_b[KWS_MAX_TENSOR_ELEMS];

__attribute__((weak)) uint32_t kws_cycle_counter(void)
{
    return 0;
}

__attribute__((weak)) const void *kws_weights_acquire(const kws_layer_t *layer,
                                                      uint32_t precision,
                                                      uint32_t *bytes)
{
    if (bytes != NULL) {
        *bytes = 0;
    }
    return precision == KWS_PRECISION_FP32 ? (const void *)layer->weight_fp32
                                           : (const void *)layer->weight_int8;
}

__attribute__((weak)) void kws_weights_release(const void *block)
{
    (void)block;
}

static uint32_t tensor_elems(const kws_layer_t *layer)
{
    return (uint32_t)layer->out_h * layer->out_w * layer->out_c;
}

void kws_infer(const int8_t *feature_grid, uint32_t precision_mask,
               uint32_t deadline_cycles, kws_result_t *result)
{
    memset(result, 0, sizeof(*result));

    void *current = arena_a;
    void *spare   = arena_b;

    /* Layer 0 always reads the quantised feature grid. */
    uint32_t input_elems = (uint32_t)KWS_INPUT_FRAMES * KWS_INPUT_MFCC;
    memcpy(current, feature_grid, input_elems);
    uint32_t mode = KWS_PRECISION_INT8;

    uint32_t started_total = kws_cycle_counter();
    uint32_t spent = 0;

    for (uint32_t index = 0; index < KWS_NUM_LAYERS; index++) {
        const kws_layer_t *layer = &kws_layers[index];
        uint32_t want = (precision_mask >> index) & 1u ? KWS_PRECISION_FP32
                                                       : KWS_PRECISION_INT8;

        /* Size of the tensor actually held right now, which for the classifier
           is still the previous layer's output because the pool has not run. */
        uint32_t in_elems;
        if (index == 0) {
            in_elems = input_elems;
        } else {
            const kws_layer_t *prev = &kws_layers[index - 1];
            in_elems = (uint32_t)prev->out_h * prev->out_w * prev->out_c;
        }

        /* Convert at the boundary when the precision changes. The scale is
           fixed at export, so the numbers keep their meaning. */
        if (want != mode) {
            if (want == KWS_PRECISION_FP32) {
                kws_dequantise((const int8_t *)current, (float *)spare, in_elems,
                               layer->input_scale, -layer->input_offset);
            } else {
                kws_quantise((const float *)current, (int8_t *)spare, in_elems,
                             layer->input_scale, -layer->input_offset);
            }
            void *swap = current;
            current = spare;
            spare = swap;
            mode = want;
        }

        /* The classifier is preceded by a global average pool over the tensor
           the previous layer produced, which is why its shape is taken from
           there rather than from this layer's declared input. */
        if (layer->kind == KWS_LAYER_FULLY_CONNECTED && index > 0) {
            const kws_layer_t *prev = &kws_layers[index - 1];
            if (mode == KWS_PRECISION_INT8) {
                kws_avgpool_int8((const int8_t *)current, (int8_t *)spare,
                                 prev->out_h, prev->out_w, prev->out_c);
            } else {
                kws_avgpool_fp32((const float *)current, (float *)spare,
                                 prev->out_h, prev->out_w, prev->out_c);
            }
            void *swap = current;
            current = spare;
            spare = swap;
        }

        uint32_t weight_bytes = 0;
        const void *weights = kws_weights_acquire(layer, want, &weight_bytes);

        uint32_t started = kws_cycle_counter();
        if (mode == KWS_PRECISION_INT8) {
            const int8_t *w = (const int8_t *)weights;
            switch (layer->kind) {
            case KWS_LAYER_DEPTHWISE:
                kws_depthwise_int8(layer, (const int8_t *)current, w,
                                   layer->bias_int32, (int8_t *)spare);
                break;
            case KWS_LAYER_FULLY_CONNECTED:
                kws_fully_connected_int8(layer, (const int8_t *)current, w,
                                         layer->bias_int32, (int8_t *)spare);
                break;
            default:
                kws_conv_int8(layer, (const int8_t *)current, w,
                              layer->bias_int32, (int8_t *)spare);
                break;
            }
        } else {
            const float *w = (const float *)weights;
            switch (layer->kind) {
            case KWS_LAYER_DEPTHWISE:
                kws_depthwise_fp32(layer, (const float *)current, w,
                                   layer->bias_fp32, (float *)spare);
                break;
            case KWS_LAYER_FULLY_CONNECTED:
                kws_fully_connected_fp32(layer, (const float *)current, w,
                                         layer->bias_fp32, (float *)spare);
                break;
            default:
                kws_conv_fp32(layer, (const float *)current, w,
                              layer->bias_fp32, (float *)spare);
                break;
            }
        }
        uint32_t elapsed = kws_cycle_counter() - started;

        kws_weights_release(weights);

        void *swap = current;
        current = spare;
        spare = swap;

        result->layer_cycles[index] = elapsed;
        result->layer_precision[index] = (uint8_t)want;

        /* The share is computed from the budget left BEFORE this layer ran,
           divided across this layer and the ones still to come. Subtracting the
           layer's own cost first would compare it against a budget it had
           already spent, so a layer that exactly met its share would be
           reported as overrunning. A share of zero means nothing was left,
           which is an overrun rather than a reason to stop checking. */
        if (deadline_cycles > 0) {
            uint32_t remaining_layers = KWS_NUM_LAYERS - index;
            uint32_t remaining_budget =
                deadline_cycles > spent ? deadline_cycles - spent : 0;
            uint32_t fair_share = remaining_budget / remaining_layers;
            result->layer_budget[index] = fair_share;
            if (!result->overran && elapsed > fair_share) {
                result->overran = 1;
                result->overran_layer = (uint8_t)index;
            }
        }
        spent += elapsed;
        (void)tensor_elems;
    }

    result->total_cycles = kws_cycle_counter() - started_total;

    /* Final logits, dequantised when the classifier ran in int8. */
    const kws_layer_t *last = &kws_layers[KWS_NUM_LAYERS - 1];
    if (mode == KWS_PRECISION_INT8) {
        kws_dequantise((const int8_t *)current, result->logits, KWS_NUM_CLASSES,
                       last->output_scale, last->output_offset);
    } else {
        memcpy(result->logits, current, sizeof(float) * KWS_NUM_CLASSES);
    }

    int8_t best = 0;
    for (int8_t c = 1; c < (int8_t)KWS_NUM_CLASSES; c++) {
        if (result->logits[c] > result->logits[best]) {
            best = c;
        }
    }
    result->top_class = best;
}
