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

void t4_inference_task(INT stacd, void *exinf);

/* Weight bytes a layer needs in the given precision, used for pool sizing and
   for reporting peak SRAM with and without streaming. */
uint32_t t4_layer_weight_bytes(const kws_layer_t *layer, uint32_t precision);
