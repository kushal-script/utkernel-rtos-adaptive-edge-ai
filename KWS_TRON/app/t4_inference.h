#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "kws_infer.h"

/* T4, the inference engine. The arithmetic lives in the kernel free core in
   audio/kws_infer.c, this task adds the parts that only make sense under an
   RTOS: it reads the precision the controller selected, times every layer with
   the cycle counter, raises the budget flag the moment a layer overruns, and
   streams each layer's weights through a memory pool so only the working layer
   occupies SRAM. */

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

/* What each layer actually costs in each precision on this part, measured on
   the first two inferences rather than assumed. The controller ranks layers by
   the measured delta, which matters because the assumption that INT8 is always
   the cheaper choice is false here: the depthwise kernels are scalar and lose
   to the floating point unit. See docs/adaptation.md. */
typedef struct {
    uint32_t fp32_cycles[KWS_NUM_LAYERS];
    uint32_t int8_cycles[KWS_NUM_LAYERS];
    uint8_t  valid;
} t4_cost_table_t;

extern t4_cost_table_t t4_cost_table;

/* Cycles saved by running layer `index` in INT8 instead of FP32. Negative when
   INT8 is the slower choice for that layer. */
int32_t t4_layer_saving(uint32_t index);

/* Projected cost of a whole precision mask from the measured table. Excludes
   boundary conversions, so it ranks candidates rather than predicting latency. */
uint32_t t4_estimate_cycles(uint32_t mask);

void t4_inference_task(INT stacd, void *exinf);

/* Weight bytes a layer needs in the given precision, used for pool sizing and
   for reporting peak SRAM with and without streaming. */
uint32_t t4_layer_weight_bytes(const kws_layer_t *layer, uint32_t precision);
