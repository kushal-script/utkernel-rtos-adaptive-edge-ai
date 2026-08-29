#include "bench_harness.h"

#include <string.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "eval_set.h"
#include "ipc_objects.h"
#include "kws_infer.h"
#include "signal_source.h"
#include "t2_variance.h"
#include "t3_features.h"
#include "t4_inference.h"
#include "t5_controller.h"

void bench_run(const char *name, uint32_t precision_mask, uint32_t deadline,
               bench_run_t *out)
{
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->precision_mask = precision_mask;
    out->best_cycles = 0xFFFFFFFFu;

    static kws_result_t result;

    for (uint32_t s = 0; s < EVAL_SAMPLE_COUNT; s++) {
        const int8_t *grid = &eval_features[(uint32_t)s * EVAL_ELEMS_PER_SAMPLE];
        kws_infer(grid, precision_mask, deadline, &result);

        if ((uint8_t)result.top_class == eval_labels[s]) {
            out->correct++;
        }
        out->total_cycles += result.total_cycles;
        if (result.total_cycles > out->worst_cycles) {
            out->worst_cycles = result.total_cycles;
        }
        if (result.total_cycles < out->best_cycles) {
            out->best_cycles = result.total_cycles;
        }
        for (uint32_t l = 0; l < KWS_NUM_LAYERS && l < 16; l++) {
            out->layer_cycles[l] += result.layer_cycles[l];
        }
        out->samples++;
    }
}

static void report(const bench_run_t *run)
{
    uint32_t mean = run->samples ? (uint32_t)(run->total_cycles / run->samples) : 0;
    uint32_t accuracy_ppm =
        run->samples ? (uint32_t)((uint64_t)run->correct * 1000000u / run->samples) : 0;

    tm_printf((UB *)"BENCH %s mask=%08x n=%u acc_ppm=%u mean=%u worst=%u best=%u us=%u\n",
              run->name, (unsigned)run->precision_mask, (unsigned)run->samples,
              (unsigned)accuracy_ppm, (unsigned)mean, (unsigned)run->worst_cycles,
              (unsigned)run->best_cycles, (unsigned)(mean / T4_CYCLES_PER_US));

    for (uint32_t l = 0; l < KWS_NUM_LAYERS && l < 16; l++) {
        uint32_t layer_mean = run->samples ? (uint32_t)(run->layer_cycles[l] / run->samples) : 0;
        tm_printf((UB *)"BENCH_LAYER %s %u %s %u\n", run->name, (unsigned)l,
                  kws_layers[l].name, (unsigned)layer_mean);
    }
}

void bench_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* Wait for the controller to settle rather than for a fixed delay, so the
       benchmark always describes a converged operating point. The elapsed time
       and the block count together give the capture rate, which is the check
       that the timer is really pacing the DMA. */
    uint32_t blocks_at_start = signal_source_block_count();
    uint32_t waited_ms = 0;
    while (!t5_stats.converged && waited_ms < BENCH_SETTLE_MAX_MS) {
        tk_dly_tsk(100);
        waited_ms += 100;
    }
    uint32_t blocks_seen_total = signal_source_block_count() - blocks_at_start;
    uint32_t block_rate_mhz = waited_ms ? (blocks_seen_total * 1000u) / waited_ms : 0;

    /* The inference core keeps its activation arenas in static storage, so it
       is single instance. Suspending the pipeline for the duration is both what
       makes this safe and what makes the measurement clean: nothing else is
       competing for the core or the cycle counter while a run is timed. */
    const ID suspended[] = { tskid_t1, tskid_t2, tskid_t3, tskid_t4 };
    for (unsigned i = 0; i < sizeof(suspended) / sizeof(suspended[0]); i++) {
        if (suspended[i] > 0) {
            tk_sus_tsk(suspended[i]);
        }
    }

    static bench_run_t run;
    const uint32_t all_int8 = 0u;
    const uint32_t all_fp32 = (KWS_NUM_LAYERS >= 32)
                                  ? 0xFFFFFFFFu
                                  : ((1u << KWS_NUM_LAYERS) - 1u);

    tm_putstring((UB *)"BENCH_BEGIN\n");

    bench_run("int8", all_int8, 0, &run);
    report(&run);

    bench_run("fp32", all_fp32, 0, &run);
    report(&run);

    /* The operating point the controller settled on, not whatever it happened
       to be trying when the benchmark started. */
    uint32_t settled_mask = t5_stats.converged ? t5_stats.converged_mask
                                               : adapt_state.precision_mask;
    bench_run("adaptive", settled_mask, adapt_state.deadline_cycles, &run);
    report(&run);

    /* The measured per layer cost table the controller ranked layers by. This
       is the evidence that the mixed operating point is not reachable by any
       single precision compile. */
    if (t4_cost_table.valid) {
        for (uint32_t l = 0; l < KWS_NUM_LAYERS; l++) {
            tm_printf((UB *)"BENCH_COST %u %s int8=%u fp32=%u saving=%d\n",
                      (unsigned)l, kws_layers[l].name,
                      (unsigned)t4_cost_table.int8_cycles[l],
                      (unsigned)t4_cost_table.fp32_cycles[l],
                      (int)t4_layer_saving(l));
        }
        tm_printf((UB *)"BENCH_ESTIMATE int8=%u fp32=%u settled=%u mask=%08x\n",
                  (unsigned)t4_estimate_cycles(all_int8),
                  (unsigned)t4_estimate_cycles(all_fp32),
                  (unsigned)t4_estimate_cycles(settled_mask),
                  (unsigned)settled_mask);
    }

    /* The convergence transient, so the trajectory can be plotted and the
       deadline misses during convergence counted rather than hidden. */
    for (uint32_t i = 0; i < t5_trace_count; i++) {
        const t5_trace_t *row = &t5_trace[i];
        tm_printf((UB *)"BENCH_TRACE %u before=%04x after=%04x act=%u over=%u cycles=%u ewma=%u\n",
                  (unsigned)row->decision, (unsigned)row->mask_before,
                  (unsigned)row->mask_after, (unsigned)row->action,
                  (unsigned)row->over_deadline, (unsigned)row->cycles,
                  (unsigned)row->ewma);
    }

    tm_printf((UB *)"BENCH_STATE vad_threshold=%u noise_floor=%u blocks=%u active=%u\n",
              (unsigned)adapt_state.vad_threshold, (unsigned)t2_stats.noise_floor,
              (unsigned)t2_stats.blocks_seen, (unsigned)t2_stats.blocks_active);
    tm_printf((UB *)"BENCH_CONTROL decisions=%u demote=%u promote=%u window=%u pri=%u ewma=%u\n",
              (unsigned)t5_stats.decisions, (unsigned)t5_stats.demotions,
              (unsigned)t5_stats.promotions, (unsigned)t5_stats.window_changes,
              (unsigned)t5_stats.priority_raises, (unsigned)t5_stats.cycles_ewma);
    tm_printf((UB *)"BENCH_CONVERGE converged=%u mask=%08x at=%u misses=%u traced=%u\n",
              (unsigned)t5_stats.converged, (unsigned)t5_stats.converged_mask,
              (unsigned)t5_stats.converged_at, (unsigned)t5_stats.deadline_misses,
              (unsigned)t5_trace_count);
    tm_printf((UB *)"BENCH_PIPELINE inferences=%u scored=%u correct=%u overruns=%u frames=%u skipped=%u resyncs=%u capture_overruns=%u\n",
              (unsigned)t4_stats.inferences, (unsigned)t4_stats.scored,
              (unsigned)t4_stats.correct, (unsigned)t4_stats.overruns,
              (unsigned)t3_stats.frames_computed, (unsigned)t3_stats.frames_skipped,
              (unsigned)t3_stats.resyncs, (unsigned)signal_source_overruns());
    tm_printf((UB *)"BENCH_CAPTURE waited_ms=%u blocks=%u rate_mhz=%u expected_mhz=%u window=%u overruns=%u\n",
              (unsigned)waited_ms, (unsigned)blocks_seen_total,
              (unsigned)block_rate_mhz,
              (unsigned)((SAMPLE_RATE_HZ * 1000u) / adapt_state.window_samples),
              (unsigned)adapt_state.window_samples,
              (unsigned)signal_source_overruns());
    tm_printf((UB *)"BENCH_MEMORY peak_pool_bytes=%u pool_capacity=%u\n",
              (unsigned)t4_stats.peak_pool_bytes, (unsigned)KWS_LAYER_POOL_BYTES);
    tm_putstring((UB *)"BENCH_END\n");

    for (unsigned i = 0; i < sizeof(suspended) / sizeof(suspended[0]); i++) {
        if (suspended[i] > 0) {
            tk_rsm_tsk(suspended[i]);
        }
    }

    tk_slp_tsk(TMO_FEVR);
}
