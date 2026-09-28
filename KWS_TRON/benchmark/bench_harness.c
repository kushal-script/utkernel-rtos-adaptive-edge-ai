#include "bench_harness.h"

#include <string.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "eval_set.h"
#include "ipc_objects.h"
#include "kws_infer.h"
#include "power_probe.h"
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

    /* Wait for convergence, not a fixed delay; elapsed time and block count give the capture rate. */
    uint32_t blocks_at_start = signal_source_block_count();
    uint64_t idle_at_start = bsp_idle_cycles();
    uint32_t idle_entries_at_start = bsp_idle_entries();
    uint32_t waited_ms = 0;
    while (!t5_stats.converged && waited_ms < BENCH_SETTLE_MAX_MS) {
        tk_dly_tsk(100);
        waited_ms += 100;
    }
    uint32_t blocks_seen_total = signal_source_block_count() - blocks_at_start;
    uint32_t block_rate_mhz = waited_ms ? (blocks_seen_total * 1000u) / waited_ms : 0;

    /* Idle residency over the settle window, timed by the kernel delay since the cycle counter wraps. */
    uint64_t idle_delta = bsp_idle_cycles() - idle_at_start;
    uint32_t idle_events = bsp_idle_entries() - idle_entries_at_start;
    uint64_t elapsed_cycles = (uint64_t)waited_ms * (SYSTEM_CLOCK_HZ / 1000u);
    uint32_t idle_ppm = elapsed_cycles
                            ? (uint32_t)((idle_delta * 1000000u) / elapsed_cycles)
                            : 0;

    static bench_run_t run;
    const uint32_t all_int8 = 0u;
    const uint32_t all_fp32 = (KWS_NUM_LAYERS >= 32)
                                  ? 0xFFFFFFFFu
                                  : ((1u << KWS_NUM_LAYERS) - 1u);

    tm_putstring((UB *)"BENCH_BEGIN\n");

    /* Live phase, each configuration drives the whole pipeline for a fixed window. */
    static const struct {
        const char *name;
        uint32_t mask;
        uint8_t  pinned;
    } live_configs[] = {
        { "fp32",     0u, 1 },   /* mask filled in below, all_fp32 is not const */
        { "int8",     0u, 1 },
        { "adaptive", 0u, 0 },   /* controller left free, mask ignored */
    };

    for (unsigned c = 0; c < sizeof(live_configs) / sizeof(live_configs[0]); c++) {
        if (live_configs[c].pinned) {
            adapt_state.precision_mask = (c == 0) ? all_fp32 : all_int8;
            adapt_state.pin_precision = 1;
        } else {
            adapt_state.pin_precision = 0;
        }

        t4_stats.inferences = 0;
        t4_stats.scored = 0;
        t4_stats.correct = 0;
        uint64_t live_idle0 = bsp_idle_cycles();
        uint32_t live_ov0 = signal_source_overruns();
        uint32_t live_blocks0 = signal_source_block_count();

        tk_dly_tsk(BENCH_LIVE_MS);

        uint64_t live_idle = bsp_idle_cycles() - live_idle0;
        uint64_t live_elapsed = (uint64_t)BENCH_LIVE_MS * (SYSTEM_CLOCK_HZ / 1000u);
        tm_printf((UB *)"BENCH_LIVE %s mask=%08x ms=%u inferences=%u scored=%u correct=%u idle_ppm=%u blocks=%u overruns=%u active=%u\n",
                  live_configs[c].name, (unsigned)adapt_state.precision_mask,
                  (unsigned)BENCH_LIVE_MS, (unsigned)t4_stats.inferences,
                  (unsigned)t4_stats.scored, (unsigned)t4_stats.correct,
                  (unsigned)((live_idle * 1000000u) / live_elapsed),
                  (unsigned)(signal_source_block_count() - live_blocks0),
                  (unsigned)(signal_source_overruns() - live_ov0),
                  (unsigned)adapt_state.active_frames);
    }
    adapt_state.pin_precision = 0;

    /* Static phase, pipeline and producer paused so the single instance core is measured alone. */
    signal_source_pause();
    const ID suspended[] = { tskid_t1, tskid_t2, tskid_t3, tskid_t4 };
    for (unsigned i = 0; i < sizeof(suspended) / sizeof(suspended[0]); i++) {
        if (suspended[i] > 0) {
            tk_sus_tsk(suspended[i]);
        }
    }

    bench_run("int8", all_int8, 0, &run);
    report(&run);

    bench_run("fp32", all_fp32, 0, &run);
    report(&run);

    /* The operating point the controller settled on. */
    uint32_t settled_mask = t5_stats.converged ? t5_stats.converged_mask
                                               : adapt_state.precision_mask;
    bench_run("adaptive", settled_mask, adapt_state.deadline_cycles, &run);
    report(&run);

    /* The measured per layer cost table the controller ranked layers by. */
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

    /* The convergence transient, so it can be plotted and its misses counted. */
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
    tm_printf((UB *)"BENCH_PIPELINE inferences=%u scored=%u correct=%u overruns=%u frames=%u skipped=%u resyncs=%u capture_overruns=%u active=%u\n",
              (unsigned)t4_stats.inferences, (unsigned)t4_stats.scored,
              (unsigned)t4_stats.correct, (unsigned)t4_stats.overruns,
              (unsigned)t3_stats.frames_computed, (unsigned)t3_stats.frames_skipped,
              (unsigned)t3_stats.resyncs, (unsigned)signal_source_overruns(),
              (unsigned)adapt_state.active_frames);
    tm_printf((UB *)"BENCH_GRID restarts=%u\n", (unsigned)t3_stats.grid_restarts);
    tm_printf((UB *)"BENCH_CAPTURE waited_ms=%u blocks=%u rate_per_s=%u expected_per_s=%u window=%u overruns=%u\n",
              (unsigned)waited_ms, (unsigned)blocks_seen_total,
              (unsigned)block_rate_mhz,
              (unsigned)(SAMPLE_RATE_HZ / adapt_state.window_samples),
              (unsigned)adapt_state.window_samples,
              (unsigned)signal_source_overruns());
    tm_printf((UB *)"BENCH_POWER idle_ppm=%u idle_entries=%u elapsed_ms=%u idle_cycles_hi=%u idle_cycles_lo=%u\n",
              (unsigned)idle_ppm, (unsigned)idle_events, (unsigned)waited_ms,
              (unsigned)(idle_delta >> 32), (unsigned)(idle_delta & 0xFFFFFFFFu));
    tm_printf((UB *)"BENCH_MEMORY peak_pool_bytes=%u pool_capacity=%u\n",
              (unsigned)t4_stats.peak_pool_bytes, (unsigned)KWS_LAYER_POOL_BYTES);
    tm_putstring((UB *)"BENCH_END\n");

    for (unsigned i = 0; i < sizeof(suspended) / sizeof(suspended[0]); i++) {
        if (suspended[i] > 0) {
            tk_rsm_tsk(suspended[i]);
        }
    }
    signal_source_resume();

    tk_slp_tsk(TMO_FEVR);
}
