#include "kws_infer.h"

#include <string.h>

#include "kws_kernels.h"

/* Two arenas, ping ponged between layers. Each must hold the largest
   activation tensor in whichever precision that layer runs, so they are sized
   for float even though most layers use int8. */
static float arena_a[KWS_MAX_TENSOR_ELEMS];
static float arena_b[KWS_MAX_TENSOR_ELEMS];

/* Folded accumulator tables, folded[oc] = bias[oc] + input_offset * sum(w).
   Computed once from the layer table, they let the INT8 inner loops drop the
   per element offset add, see kws_kernels.h. Depthwise keeps the plain path,
   its window is small and its samples are not contiguous. */
#define KWS_FOLDED_SLOTS 640
static int32_t folded_store[KWS_FOLDED_SLOTS];
_Static_assert(KWS_FOLDED_SLOTS >= KWS_FOLDED_SLOTS_REQUIRED,
               "folded_store is too small for this model, raise KWS_FOLDED_SLOTS");

/* Per kernel row weight sums for padded convolutions, rowsum[oc][kh]. They
   let a window that overlaps the padding take the folded path too, by
   subtracting the weights of the taps that fall outside. Only the stem pads,
   so only the stem takes slots. */
#define KWS_ROWSUM_SLOTS 640
static int32_t rowsum_store[KWS_ROWSUM_SLOTS];
static const int32_t *layer_rowsum[KWS_NUM_LAYERS];
_Static_assert(KWS_ROWSUM_SLOTS >= KWS_ROWSUM_SLOTS_REQUIRED,
               "rowsum_store is too small for this model, raise KWS_ROWSUM_SLOTS");
static const int32_t *layer_folded[KWS_NUM_LAYERS];
static uint8_t folded_ready;

static void fold_bias_tables(void)
{
    int32_t *slot = folded_store;
    int32_t *rslot = rowsum_store;
    for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
        const kws_layer_t *layer = &kws_layers[i];
        layer_rowsum[i] = NULL;
        if (layer->kind == KWS_LAYER_DEPTHWISE) {
            layer_folded[i] = NULL;
            continue;
        }
        if (layer->kind == KWS_LAYER_CONV && (layer->pad_h || layer->pad_w)) {
            uint32_t row_len = (uint32_t)layer->kernel_w * layer->in_c;
            for (uint32_t oc = 0; oc < layer->out_c; oc++) {
                for (uint32_t kh = 0; kh < layer->kernel_h; kh++) {
                    const int8_t *w = &layer->weight_int8[(oc * layer->kernel_h + kh) * row_len];
                    int32_t sum = 0;
                    for (uint32_t k = 0; k < row_len; k++) {
                        sum += w[k];
                    }
                    rslot[oc * layer->kernel_h + kh] = sum;
                }
            }
            layer_rowsum[i] = rslot;
            rslot += (uint32_t)layer->out_c * layer->kernel_h;
        }
        uint32_t per_filter = layer->kind == KWS_LAYER_FULLY_CONNECTED
                                  ? layer->in_c
                                  : (uint32_t)layer->kernel_h * layer->kernel_w *
                                        layer->in_c;
        for (uint32_t oc = 0; oc < layer->out_c; oc++) {
            const int8_t *w = &layer->weight_int8[oc * per_filter];
            int32_t sum = 0;
            for (uint32_t k = 0; k < per_filter; k++) {
                sum += w[k];
            }
            slot[oc] = layer->bias_int32[oc] + layer->input_offset * sum;
        }
        layer_folded[i] = slot;
        slot += layer->out_c;
    }
    folded_ready = 1;
}

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

    if (!folded_ready) {
        fold_bias_tables();
    }

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
                                         layer->bias_int32, layer_folded[index],
                                         (int8_t *)spare);
                break;
            default:
                kws_conv_int8(layer, (const int8_t *)current, w,
                              layer->bias_int32, layer_folded[index],
                              layer_rowsum[index], (int8_t *)spare);
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

        /* The overrun signal is the cumulative spend crossing the deadline,
           recorded at the layer where it happened. A uniform per layer share
           cannot work here, the stem alone is over a fifth of the network, so
           it would exceed a tenth of any deadline at either precision and the
           controller would demote forever. The even split of the remaining
           budget is still recorded per layer as telemetry. */
        spent += elapsed;
        if (deadline_cycles > 0) {
            uint32_t remaining_layers = KWS_NUM_LAYERS - 1 - index;
            uint32_t remaining_budget =
                deadline_cycles > spent ? deadline_cycles - spent : 0;
            result->layer_budget[index] =
                remaining_layers ? remaining_budget / remaining_layers : remaining_budget;
            if (!result->overran && spent > deadline_cycles) {
                result->overran = 1;
                result->overran_layer = (uint8_t)index;
            }
        }
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
