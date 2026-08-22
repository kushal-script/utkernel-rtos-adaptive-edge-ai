#include "bench_harness.h"

#include <string.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "eval_set.h"
#include "ipc_objects.h"
#include "kws_infer.h"
#include "t2_variance.h"
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
    uint32_t mean = run->samples ? run->total_cycles / run->samples : 0;
    uint32_t accuracy_ppm =
        run->samples ? (uint32_t)((uint64_t)run->correct * 1000000u / run->samples) : 0;

    tm_printf((UB *)"BENCH %s mask=%08x n=%u acc_ppm=%u mean=%u worst=%u best=%u us=%u\n",
              run->name, (unsigned)run->precision_mask, (unsigned)run->samples,
              (unsigned)accuracy_ppm, (unsigned)mean, (unsigned)run->worst_cycles,
              (unsigned)run->best_cycles, (unsigned)(mean / T4_CYCLES_PER_US));

    for (uint32_t l = 0; l < KWS_NUM_LAYERS && l < 16; l++) {
        uint32_t layer_mean = run->samples ? run->layer_cycles[l] / run->samples : 0;
        tm_printf((UB *)"BENCH_LAYER %s %u %s %u\n", run->name, (unsigned)l,
                  kws_layers[l].name, (unsigned)layer_mean);
    }
}

void bench_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* Let the pipeline settle so the benchmark is not competing with startup. */
    tk_dly_tsk(2000);

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

    /* Whatever the controller has converged to while the pipeline ran. */
    bench_run("adaptive", adapt_state.precision_mask, adapt_state.deadline_cycles,
              &run);
    report(&run);

    tm_printf((UB *)"BENCH_STATE vad_threshold=%u noise_floor=%u blocks=%u active=%u\n",
              (unsigned)adapt_state.vad_threshold, (unsigned)t2_stats.noise_floor,
              (unsigned)t2_stats.blocks_seen, (unsigned)t2_stats.blocks_active);
    tm_printf((UB *)"BENCH_CONTROL decisions=%u demote=%u promote=%u window=%u pri=%u ewma=%u\n",
              (unsigned)t5_stats.decisions, (unsigned)t5_stats.demotions,
              (unsigned)t5_stats.promotions, (unsigned)t5_stats.window_changes,
              (unsigned)t5_stats.priority_raises, (unsigned)t5_stats.cycles_ewma);
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
