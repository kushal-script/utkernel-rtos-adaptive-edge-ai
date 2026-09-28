#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "kws_infer.h"

/* T4 wraps the kernel free core with per layer timing, the budget flag and pooled weight streaming. */

typedef struct {
    kws_result_t last;
    uint32_t inferences;
    uint32_t overruns;
    uint32_t worst_cycles;
    uint32_t peak_pool_bytes;
    int32_t  last_label;      /* ground truth when the source knows it */
    uint32_t correct;
    uint32_t scored;
} t4_stats_t;

extern t4_stats_t t4_stats;

/* Per layer cost in each precision, measured on the first two inferences, see docs/adaptation.md. */
typedef struct {
    uint32_t fp32_cycles[KWS_NUM_LAYERS];
    uint32_t int8_cycles[KWS_NUM_LAYERS];
    uint8_t  valid;
} t4_cost_table_t;

extern t4_cost_table_t t4_cost_table;

/* Cycles saved by running layer `index` in INT8, negative when INT8 is slower. */
int32_t t4_layer_saving(uint32_t index);

/* Projected cost of a mask from the measured table, for ranking, boundary conversions excluded. */
uint32_t t4_estimate_cycles(uint32_t mask);

void t4_inference_task(INT stacd, void *exinf);

/* Weight bytes a layer needs in the given precision. */
uint32_t t4_layer_weight_bytes(const kws_layer_t *layer, uint32_t precision);
