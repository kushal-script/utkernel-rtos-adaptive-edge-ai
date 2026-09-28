#pragma once

#include <stdint.h>

#include "kws_layer.h"
#include "kws_model.h"

/* The inference core, kernel free so it can be checked against the NumPy reference on a host. */

typedef struct {
    uint32_t layer_cycles[KWS_NUM_LAYERS];
    uint32_t layer_budget[KWS_NUM_LAYERS];
    uint8_t  layer_precision[KWS_NUM_LAYERS];   /* kws_precision_t per layer */
    uint32_t total_cycles;
    uint8_t  overran_layer;                     /* first layer over budget   */
    uint8_t  overran;
    int8_t   top_class;
    float    logits[KWS_NUM_CLASSES];
} kws_result_t;

/* Bit n set means layer n runs FP32; conversions are inserted where neighbours disagree. */
void kws_infer(const int8_t *feature_grid, uint32_t precision_mask,
               uint32_t deadline_cycles, kws_result_t *result);

/* Cycle source, the host default returns zero and disables budget enforcement. */
uint32_t kws_cycle_counter(void);

/* Weight access, so the RTOS build can stream a layer's weights through a pool. */
const void *kws_weights_acquire(const kws_layer_t *layer, uint32_t precision,
                                uint32_t *bytes);
void kws_weights_release(const void *block);
