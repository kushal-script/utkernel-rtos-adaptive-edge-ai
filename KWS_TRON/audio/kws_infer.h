#pragma once

#include <stdint.h>

#include "kws_layer.h"
#include "kws_model.h"

/* The inference core, deliberately free of any kernel dependency so it can be
   compiled and checked against the NumPy golden reference on a host before it
   ever runs on the board. T4 wraps it with the RTOS concerns, the budget flag,
   the priority change, and the weight streaming. */

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

/* Precision is a bit mask, bit n set means layer n runs FP32. The remaining
   layers run INT8. Boundary conversions are inserted automatically wherever
   consecutive layers disagree. */
void kws_infer(const int8_t *feature_grid, uint32_t precision_mask,
               uint32_t deadline_cycles, kws_result_t *result);

/* Cycle source. The firmware provides the DWT counter, a host test can leave
   the default, which reports zero and disables budget enforcement. */
uint32_t kws_cycle_counter(void);

/* Weight access, so the RTOS build can stream a layer's weights into a memory
   pool block while a plain build reads them straight out of flash. */
const void *kws_weights_acquire(const kws_layer_t *layer, uint32_t precision,
                                uint32_t *bytes);
void kws_weights_release(const void *block);
