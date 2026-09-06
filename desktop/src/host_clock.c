#include "host_clock.h"

#include <stdio.h>

#include "app_config.h"
#include "device_costs.h"
#include "dwt_logger.h"
#include "ipc_objects.h"
#include "power_probe.h"
#include "t4_inference.h"
#include "uthread.h"

/* The cycle counter the whole adaptation loop reads.

   A desktop cannot measure what this project measures. The board's mixed
   precision optimum exists because its INT8 depthwise kernels are scalar while
   the contiguous layers run packed multiply accumulate through the M33 DSP
   extension, an inversion no host CPU reproduces. A controller timing itself on
   this machine would therefore find no inversion, converge on full FP32, and
   tell a story the hardware does not support.

   So the counter is virtual: inside an inference it advances by the per layer
   cost recorded on the board, which makes the calibration T4 performs on its
   first two inferences reproduce the device cost table exactly, and every
   decision downstream follows from the same numbers the board used. Outside an
   inference it advances with host time scaled to the device clock, which is
   what the feature stage's frame timing wants.

   Every figure this produces is replayed, not measured, and the CLI labels it
   that way wherever it is printed. */

#define CALLS_PER_INFERENCE (2u + 2u * KWS_NUM_LAYERS)
#define ALL_FP32_MASK       ((1u << KWS_NUM_LAYERS) - 1u)

static device_costs_t costs;
static uint64_t       origin_us;
static uint64_t       injected;
static int            pattern_warned;
/* Only the thread running inferences walks the bracketed sequence kws_infer
   emits. T3 reads the same counter twice per feature frame and must get plain
   host time, or its frame cost would come out structurally zero. */
static ID             inference_task = 0;

/* Per thread, because T3 reads the same counter for its frame timing and only
   the inference thread walks the bracketed sequence kws_infer emits. */
static _Thread_local unsigned call_index;
static _Thread_local unsigned inference_count;
static _Thread_local uint64_t inference_base;
static _Thread_local uint64_t inference_accum;

void host_clock_init(const device_costs_t *table)
{
    costs          = *table;
    origin_us      = umonotonic_us();
    injected       = 0;
    pattern_warned = 0;
}

const device_costs_t *host_clock_costs(void)
{
    return &costs;
}

void host_clock_set_inference_task(ID id)
{
    inference_task = id;
}

static uint64_t host_cycles_now(void)
{
    uint64_t elapsed_us = umonotonic_us() - origin_us;
    return elapsed_us * (uint64_t)T4_CYCLES_PER_US + injected;
}

/* The mask the inference now starting will run under. T4 forces all FP32 then
   all INT8 for its two calibration inferences before the table is valid. */
static uint32_t mask_for_this_inference(void)
{
    if (!t4_cost_table.valid) {
        return (inference_count == 0) ? ALL_FP32_MASK : 0u;
    }
    return adapt_state.precision_mask;
}

static uint32_t layer_cost(uint32_t layer, uint32_t mask)
{
    if (layer >= (uint32_t)costs.layers) {
        return 0;
    }
    return ((mask >> layer) & 1u) ? costs.fp32_cycles[layer]
                                  : costs.int8_cycles[layer];
}

void dwt_init(void)
{
    origin_us = umonotonic_us();
}

uint32_t dwt_read(void)
{
    if (ukernel_self_id() != inference_task) {
        return (uint32_t)host_cycles_now();
    }

    unsigned idx = call_index;

    if (idx == 0) {
        inference_base  = host_cycles_now();
        inference_accum = 0;
        call_index      = 1;
        return (uint32_t)inference_base;
    }

    /* Even indices from two upward close a layer, so the layer that just ran
       is (idx - 2) / 2. */
    if ((idx & 1u) == 0u) {
        uint32_t layer = (idx - 2u) / 2u;
        inference_accum += layer_cost(layer, mask_for_this_inference());
    }

    idx++;
    if (idx >= CALLS_PER_INFERENCE) {
        injected += inference_accum;
        inference_count++;
        idx = 0;
    }
    call_index = idx;
    return (uint32_t)(inference_base + inference_accum);
}

/* The bracket is correct only if it closed, which means call_index returned to
   zero by the time the inference finished. Comparing a constant against itself
   would prove nothing, so the check reads the clock's own state. */
void host_clock_check_pattern(void)
{
    if (call_index != 0 && !pattern_warned) {
        pattern_warned = 1;
        fprintf(stderr,
                "host clock: an inference emitted a timing sequence of a "
                "different shape, stopping %u calls into a bracket of %u. "
                "Replayed cycle costs are misattributed and no timing figure "
                "below should be quoted.\n",
                call_index, (unsigned)CALLS_PER_INFERENCE);
    }
}

void dwt_log_layer(uint8_t layer_id, uint32_t cycles)
{
    (void)layer_id;
    (void)cycles;
}

/* The board measures idle residency by counting cycles spent in WFI. A host has
   no equivalent, and a desktop scheduler idle figure has no relationship to the
   published 23.5, 39.9 and 42.0 percent, so nothing is reported rather than
   something that would be read as comparable. */
uint64_t bsp_idle_cycles(void)
{
    return 0;
}

uint32_t bsp_idle_entries(void)
{
    return 0;
}
