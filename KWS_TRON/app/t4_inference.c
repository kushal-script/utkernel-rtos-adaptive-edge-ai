#include "t4_inference.h"

#include <stdbool.h>
#include <string.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "dwt_logger.h"
#include "ipc_objects.h"
#include "signal_source.h"
#include "t3_features.h"

t4_stats_t t4_stats;
t4_cost_table_t t4_cost_table;

/* Calibration runs the first two inferences at the two pure precisions so the
   controller starts from measurement rather than from an assumption. Two
   inferences is about a third of a second and happens once at startup. */
#define CALIBRATION_INFERENCES 2
static uint32_t calibration_step;

int32_t t4_layer_saving(uint32_t index)
{
    return (int32_t)t4_cost_table.fp32_cycles[index] -
           (int32_t)t4_cost_table.int8_cycles[index];
}

uint32_t t4_estimate_cycles(uint32_t mask)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
        total += ((mask >> i) & 1u) ? t4_cost_table.fp32_cycles[i]
                                    : t4_cost_table.int8_cycles[i];
    }
    return total;
}

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

        /* While calibrating, force the pure precisions and enforce no deadline,
           so the two probe inferences are never mistaken for overruns. */
        bool calibrating = calibration_step < CALIBRATION_INFERENCES;
        uint32_t all_fp32 = (KWS_NUM_LAYERS >= 32)
                                ? 0xFFFFFFFFu
                                : ((1u << KWS_NUM_LAYERS) - 1u);
        uint32_t mask = calibrating ? (calibration_step == 0 ? all_fp32 : 0u)
                                    : adapt_state.precision_mask;
        uint32_t deadline = calibrating ? 0u : adapt_state.deadline_cycles;

        /* One read of the pointer fixes both the tensor and the audio it came
           from. A publication landing during this inference swaps the pointer
           for the next tensor, it never touches this one. */
        const int8_t *grid = t3_feature_grid;
        kws_infer(grid, mask, deadline, &t4_stats.last);

        if (calibrating) {
            uint32_t *slot = (calibration_step == 0) ? t4_cost_table.fp32_cycles
                                                     : t4_cost_table.int8_cycles;
            for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
                slot[i] = t4_stats.last.layer_cycles[i];
            }
            calibration_step++;
            if (calibration_step >= CALIBRATION_INFERENCES) {
                t4_cost_table.valid = 1;
            }
            continue;
        }

        t4_stats.inferences++;
        if (t4_stats.last.total_cycles > t4_stats.worst_cycles) {
            t4_stats.worst_cycles = t4_stats.last.total_cycles;
        }

        /* Score against the clip this tensor holds, not the clip the DMA is
           staging now, which is up to a second ahead of it, and not the tensor
           published since, which under a slow configuration is the usual case. */
        int label = signal_source_label_for_span(t3_grid_corpus_end(grid),
                                                 KWS_GRID_SPAN_SAMPLES);
        t4_stats.last_label = label;
        if (label >= 0) {
            t4_stats.scored++;
            if (label == (int)t4_stats.last.top_class) {
                t4_stats.correct++;
            }
        }

        /* Both bits in one call. The controller outranks this task, so two
           calls would let it wake between them and count one late inference
           as two decisions and two misses. */
        UINT done = FLG_INFERENCE_DONE;
        if (t4_stats.last.overran) {
            t4_stats.overruns++;
            done |= FLG_BUDGET_EXCEEDED;
        }
        tk_set_flg(flgid_control, done);
    }
}
