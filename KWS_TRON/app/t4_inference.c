#include "t4_inference.h"

#include <string.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "dwt_logger.h"
#include "ipc_objects.h"
#include "signal_source.h"
#include "t3_features.h"

t4_stats_t t4_stats;

/* Overrides the weak stub in the core, so the timing the core records is the
   hardware cycle counter rather than zero. */
uint32_t kws_cycle_counter(void)
{
    return dwt_read();
}

uint32_t t4_layer_weight_bytes(const kws_layer_t *layer, uint32_t precision)
{
    uint32_t elements;
    switch (layer->kind) {
    case KWS_LAYER_DEPTHWISE:
        elements = (uint32_t)layer->kernel_h * layer->kernel_w * layer->in_c;
        break;
    case KWS_LAYER_FULLY_CONNECTED:
        elements = (uint32_t)layer->out_c * layer->in_c;
        break;
    default:
        elements = (uint32_t)layer->out_c * layer->kernel_h * layer->kernel_w *
                   layer->in_c;
        break;
    }
    return precision == KWS_PRECISION_FP32 ? elements * sizeof(float) : elements;
}

/* Overrides the weak stub in the core. Each layer's weights are copied from
   flash into a pool block for the duration of that layer and released
   immediately after, so peak SRAM holds one layer rather than the whole model.
   With a flash resident model this demonstrates the mechanism and gives the
   number to report, see docs/inference_core.md for what it buys and when. */
const void *kws_weights_acquire(const kws_layer_t *layer, uint32_t precision,
                                uint32_t *bytes)
{
    uint32_t needed = t4_layer_weight_bytes(layer, precision);
    if (bytes != NULL) {
        *bytes = needed;
    }

    void *block = NULL;
    if (tk_get_mpl(mplid_layer, (SZ)needed, &block, TMO_POL) != E_OK ||
        block == NULL) {
        /* Pool exhausted, fall back to reading straight from flash so an
           inference is never dropped. The benchmark records the event. */
        return precision == KWS_PRECISION_FP32 ? (const void *)layer->weight_fp32
                                               : (const void *)layer->weight_int8;
    }

    if (needed > t4_stats.peak_pool_bytes) {
        t4_stats.peak_pool_bytes = needed;
    }

    const void *source = precision == KWS_PRECISION_FP32
                             ? (const void *)layer->weight_fp32
                             : (const void *)layer->weight_int8;
    memcpy(block, source, needed);
    return block;
}

void kws_weights_release(const void *block)
{
    /* Only pool blocks are released, a flash fallback pointer is not ours. */
    if (block == NULL) {
        return;
    }
    for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
        if (block == (const void *)kws_layers[i].weight_int8 ||
            block == (const void *)kws_layers[i].weight_fp32) {
            return;
        }
    }
    tk_rel_mpl(mplid_layer, (void *)block);
}

void t4_inference_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    for (;;) {
        UINT pattern = 0;
        ER err = tk_wai_flg(flgid_inference, FLG_FEATURES_READY,
                            TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        if (err < E_OK) {
            continue;
        }

        uint32_t mask = adapt_state.precision_mask;
        uint32_t deadline = adapt_state.deadline_cycles;

        kws_infer(t3_feature_grid, mask, deadline, &t4_stats.last);

        t4_stats.inferences++;
        if (t4_stats.last.total_cycles > t4_stats.worst_cycles) {
            t4_stats.worst_cycles = t4_stats.last.total_cycles;
        }

        /* Score against the clip the grid holds, not the clip the DMA is
           staging now, which is up to a second ahead of it. */
        int label = signal_source_label_for_span(t3_stats.grid_corpus_end,
                                                 KWS_GRID_SPAN_SAMPLES);
        t4_stats.last_label = label;
        if (label >= 0) {
            t4_stats.scored++;
            if (label == (int)t4_stats.last.top_class) {
                t4_stats.correct++;
            }
        }

        if (t4_stats.last.overran) {
            t4_stats.overruns++;
            tk_set_flg(flgid_control, FLG_BUDGET_EXCEEDED);
        }

        tk_set_flg(flgid_control, FLG_INFERENCE_DONE);
    }
}
