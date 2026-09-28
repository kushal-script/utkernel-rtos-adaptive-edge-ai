#include "host_clock.h"

#include <stdio.h>

#include "app_config.h"
#include "device_costs.h"
#include "dwt_logger.h"
#include "ipc_objects.h"
#include "power_probe.h"
#include "t4_inference.h"
#include "uthread.h"

/* Virtual cycle counter, replaying the board's per layer costs inside an inference, see desktop/README.md. */

#define CALLS_PER_INFERENCE (2u + 2u * KWS_NUM_LAYERS)
#define ALL_FP32_MASK       ((1u << KWS_NUM_LAYERS) - 1u)

static device_costs_t costs;
static uint64_t       origin_us;
static uint64_t       injected;
static int            pattern_warned;
/* Only the inference thread walks kws_infer's bracketed sequence, T3 gets host time. */
static ID             inference_task = 0;

/* Per thread, T3 reads the same counter for frame timing. */
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

/* The mask the starting inference runs under. */
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

    /* Even indices from two upward close layer (idx - 2) / 2. */
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

/* The bracket is correct only if call_index returned to zero. */
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

/* No idle residency on a host, nothing is reported rather than something incomparable. */
uint64_t bsp_idle_cycles(void)
{
    return 0;
}

uint32_t bsp_idle_entries(void)
{
    return 0;
}
