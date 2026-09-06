#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_tasks.h"
#include "device_costs.h"
#include "dwt_logger.h"
#include "eval_set.h"
#include "host_clock.h"
#include "host_source.h"
#include "ipc_objects.h"
#include "kws_features.h"
#include "kws_infer.h"
#include "kws_model.h"
#include "signal_source.h"
#include "t1_ingest.h"
#include "t2_variance.h"
#include "t3_features.h"
#include "t4_inference.h"
#include "t5_controller.h"
#include "uthread.h"

/* The desktop front end. It boots the same five tasks the board runs, on the
   host kernel in ukernel.c, and reports what they did. Nothing in KWS_TRON is
   modified or reimplemented here: the tasks, the controller, the feature stage
   and the inference core are the firmware sources compiled for this machine. */

#define ALL_FP32_MASK ((1u << KWS_NUM_LAYERS) - 1u)
#define CONVERGED_REF 0x0AAu

typedef struct {
    unsigned    seconds;
    const char *wav;
    const char *capture;
    double      speed;
    int         trace;
} options_t;

/* The controller changes T4's priority by identifier, so these are the same
   globals usermain publishes on the board. */
ID tskid_t1, tskid_t2, tskid_t3, tskid_t4, tskid_t5;
ID tskid_heartbeat, tskid_bench;

static const char *class_name(int index)
{
    if (index < 0 || index >= KWS_NUM_CLASSES) {
        return "none";
    }
    return kws_labels[index];
}

static void print_mask(uint32_t mask, char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    size_t n = 0;
    for (int i = KWS_NUM_LAYERS - 1; i >= 0 && n + 1 < len; i--) {
        out[n++] = ((mask >> i) & 1u) ? 'F' : 'i';
    }
    out[n] = '\0';
}

static int load_costs(const options_t *opt, device_costs_t *costs)
{
    char err[256];
    if (opt->capture != NULL) {
        if (device_costs_load(opt->capture, costs, err, sizeof(err)) != 0) {
            fprintf(stderr, "cannot read the cost table from %s: %s\n",
                    opt->capture, err);
            return -1;
        }
        return 0;
    }
    device_costs_builtin(costs);
    return 0;
}

static int boot_pipeline(const options_t *opt, device_costs_t *costs)
{
    if (load_costs(opt, costs) != 0) {
        return -1;
    }

    host_clock_init(costs);
    dwt_init();
    kws_features_init();
    ukernel_init();

    if (ipc_objects_init() != E_OK) {
        fprintf(stderr, "kernel objects failed to create\n");
        return -1;
    }

    if (opt->wav != NULL) {
        char err[256];
        if (host_source_use_wav(opt->wav, err, sizeof(err)) != 0) {
            fprintf(stderr, "cannot use %s: %s\n", opt->wav, err);
            return -1;
        }
    } else {
        host_source_use_corpus();
    }
    host_source_set_speed(opt->speed);

    struct { ID *slot; FP entry; PRI pri; const char *name; } table[] = {
        { &tskid_t1, (FP)t1_ingest_task,     PRI_T1_INGEST,     "T1 ingest"     },
        { &tskid_t2, (FP)t2_variance_task,   PRI_T2_VARIANCE,   "T2 variance"   },
        { &tskid_t3, (FP)t3_features_task,   PRI_T3_FEATURES,   "T3 features"   },
        { &tskid_t4, (FP)t4_inference_task,  PRI_T4_INFERENCE,  "T4 inference"  },
        { &tskid_t5, (FP)t5_controller_task, PRI_T5_CONTROLLER, "T5 controller" },
    };

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        T_CTSK ctsk = {
            .exinf   = NULL,
            .tskatr  = TA_HLNG | TA_RNG3,
            .task    = table[i].entry,
            .itskpri = table[i].pri,
            .stksz   = STACK_LARGE,
        };
        ID id = tk_cre_tsk(&ctsk);
        if (id < E_OK) {
            fprintf(stderr, "cannot create %s\n", table[i].name);
            return -1;
        }
        *table[i].slot = id;
        if (tk_sta_tsk(id, 0) < E_OK) {
            fprintf(stderr, "cannot start %s\n", table[i].name);
            return -1;
        }
    }

    /* Only T4's cycle reads are the bracketed per layer sequence the replay
       depends on; T3 times feature frames on the same counter. */
    host_clock_set_inference_task(tskid_t4);

    host_source_start_producer();
    return 0;
}

static void shutdown_pipeline(void)
{
    host_source_stop_producer();
    ukernel_shutdown();
}

static void print_provenance(const device_costs_t *costs)
{
    printf("\nProvenance\n");
    printf("  Pipeline      the five task sources from KWS_TRON/app, unmodified\n");
    printf("  Cycle costs   replayed from %s\n", costs->source);
    printf("  Not measured  every cycle and millisecond figure above is the\n");
    printf("                board's, replayed. This machine's own speed appears\n");
    printf("                nowhere, and no power figure is produced at all,\n");
    printf("                because a desktop has no equivalent of the idle\n");
    printf("                residency the board reports.\n");
}

static void report_run(const device_costs_t *costs, unsigned elapsed_s)
{
    char maskbuf[KWS_NUM_LAYERS + 1];
    uint32_t mask = t5_stats.converged ? t5_stats.converged_mask
                                       : adapt_state.precision_mask;
    uint32_t modelled = device_costs_total(costs, mask);
    double   ms = (double)modelled / (double)T4_CYCLES_PER_US / 1000.0;

    printf("\nCapture\n");
    printf("  Source        %s\n", host_source_description());
    printf("  Blocks        %u captured, %u overruns, window %u samples\n",
           (unsigned)signal_source_block_count(),
           (unsigned)signal_source_overruns(),
           (unsigned)adapt_state.window_samples);

    printf("\nGate, T2\n");
    printf("  Blocks        %u seen, %u passed the gate\n",
           (unsigned)t2_stats.blocks_seen, (unsigned)t2_stats.blocks_active);
    printf("  Noise floor   %u, gate threshold %u\n",
           (unsigned)t2_stats.noise_floor, (unsigned)adapt_state.vad_threshold);

    printf("\nFeatures, T3\n");
    printf("  Frames        %u computed, %u skipped by the gate\n",
           (unsigned)t3_stats.frames_computed, (unsigned)t3_stats.frames_skipped);
    printf("  Grids         %u queued, %u restarted, %u resyncs\n",
           (unsigned)t3_stats.inferences_queued,
           (unsigned)t3_stats.grid_restarts, (unsigned)t3_stats.resyncs);

    printf("\nInference, T4\n");
    printf("  Inferences    %u, peak layer pool %u of %u bytes\n",
           (unsigned)t4_stats.inferences, (unsigned)t4_stats.peak_pool_bytes,
           (unsigned)KWS_LAYER_POOL_BYTES);
    printf("  Last result   %s\n", class_name(t4_stats.last.top_class));
    if (host_source_is_labelled() && t4_stats.scored > 0) {
        printf("  Scored        %u of %u correct end to end, %.0f percent\n",
               (unsigned)t4_stats.correct, (unsigned)t4_stats.scored,
               100.0 * (double)t4_stats.correct / (double)t4_stats.scored);
        printf("                This is one short run across every mask the\n");
        printf("                controller visited, not a per configuration\n");
        printf("                figure. The board's pinned thirty second\n");
        printf("                windows give 47 to 57 percent with the three\n");
        printf("                configurations not separable, see README.md.\n");
    } else {
        printf("  Scored        nothing, the source carries no ground truth\n");
    }

    print_mask(mask, maskbuf, sizeof(maskbuf));
    printf("\nController, T5\n");
    printf("  Decisions     %u, %u demotions, %u promotions\n",
           (unsigned)t5_stats.decisions, (unsigned)t5_stats.demotions,
           (unsigned)t5_stats.promotions);
    printf("  Levers        %u window resizes by mailbox, %u priority raises\n",
           (unsigned)t5_stats.window_changes, (unsigned)t5_stats.priority_raises);
    printf("  Precision     %s  (mask %03x, high bit is layer %d)\n",
           maskbuf, (unsigned)mask, KWS_NUM_LAYERS - 1);
    printf("  Converged     %s", t5_stats.converged ? "yes" : "not yet");
    if (t5_stats.converged) {
        printf(", at decision %u", (unsigned)t5_stats.converged_at);
    }
    printf("\n");
    (void)ms;
    printf("  Model cost    %u cycles for this mask by the replayed table.\n",
           (unsigned)modelled);
    printf("                This ranks masks against each other. It is not a\n");
    printf("                latency: it excludes the conversions inserted\n");
    printf("                between layers of different precision and carries\n");
    printf("                the calibration probe's own overhead, so it neither\n");
    printf("                matches nor bounds what the board measures end to\n");
    printf("                end. Those measured latencies, and the %u ms\n",
           (unsigned)(T4_DEADLINE_US / 1000u));
    printf("                deadline they are judged against, are in README.md.\n");
    printf("  Ran for       %u s of wall clock\n", elapsed_s);

    print_provenance(costs);
}

static int cmd_run(const options_t *opt)
{
    device_costs_t costs;
    if (boot_pipeline(opt, &costs) != 0) {
        return 1;
    }

    printf("Running the five task pipeline for %u s.\n", opt->seconds);
    printf("Capture, gate, features, inference and the controller are live.\n");

    if (opt->trace) {
        uint32_t seen = 0;
        uint64_t end = umonotonic_us() + (uint64_t)opt->seconds * 1000000u;
        while (umonotonic_us() < end) {
            usleep_us(50000);
            while (seen < t5_trace_count && seen < t5_trace_capacity()) {
                const t5_trace_t *e = &t5_trace[seen++];
                char before[KWS_NUM_LAYERS + 1], after[KWS_NUM_LAYERS + 1];
                print_mask(e->mask_before, before, sizeof(before));
                print_mask(e->mask_after, after, sizeof(after));
                printf("  decision %-5u %s -> %s  %s\n",
                       (unsigned)e->decision, before, after,
                       e->mask_before == e->mask_after ? "settled" : "moved");
            }
        }
        ukernel_request_stop();
    } else {
        ukernel_run_ms(opt->seconds * 1000u);
    }

    shutdown_pipeline();
    report_run(&costs, opt->seconds);
    return 0;
}

static int cmd_converge(const options_t *opt)
{
    device_costs_t costs;
    options_t local = *opt;
    if (local.seconds < 30) {
        local.seconds = 30;
    }
    if (boot_pipeline(&local, &costs) != 0) {
        return 1;
    }

    printf("Waiting for the controller to settle, up to %u s.\n", local.seconds);
    uint64_t end = umonotonic_us() + (uint64_t)local.seconds * 1000000u;
    while (umonotonic_us() < end && !t5_stats.converged) {
        usleep_us(20000);
    }
    ukernel_request_stop();
    shutdown_pipeline();

    printf("\nController trajectory\n");
    uint32_t n = t5_trace_count < t5_trace_capacity() ? t5_trace_count
                                                      : t5_trace_capacity();
    for (uint32_t i = 0; i < n; i++) {
        const t5_trace_t *e = &t5_trace[i];
        char before[KWS_NUM_LAYERS + 1], after[KWS_NUM_LAYERS + 1];
        print_mask(e->mask_before, before, sizeof(before));
        print_mask(e->mask_after, after, sizeof(after));
        printf("  %-5u %s -> %s  %s\n", (unsigned)e->decision, before, after,
               e->mask_before == e->mask_after ? "" : "moved");
    }

    char settled[KWS_NUM_LAYERS + 1], probed[KWS_NUM_LAYERS + 1];
    print_mask(t5_stats.converged_mask, settled, sizeof(settled));
    print_mask(t5_stats.probe_mask, probed, sizeof(probed));

    printf("\nEndpoints\n");
    if (!t5_stats.converged) {
        printf("  The controller had not settled when the window closed. Give\n");
        printf("  it longer with --seconds.\n");
    } else {
        printf("  Descending from all FP32   mask %03x  %s\n",
               (unsigned)t5_stats.converged_mask, settled);
        printf("  Climbing from all INT8     mask %03x  %s\n",
               (unsigned)t5_stats.probe_mask, probed);
        printf("  %u demotions and %u promotions in total.\n",
               (unsigned)t5_stats.demotions, (unsigned)t5_stats.promotions);
        printf("  %s\n", t5_stats.probe_mask == t5_stats.converged_mask
               ? "Same operating point from both extremes, so it is a property"
                 " of the cost table rather than of where the search began."
               : "The two endpoints differ, which contradicts the recorded run.");
    }

    printf("\nCost of each mask by the replayed table. These rank masks against\n");
    printf("each other and are not latencies, see README.md for the measured ones\n");
    uint32_t masks[3] = { 0u, ALL_FP32_MASK, t5_stats.converged_mask };
    const char *labels[3] = { "all INT8 ", "all FP32 ", "converged" };
    for (int i = 0; i < 3; i++) {
        uint32_t c = device_costs_total(&costs, masks[i]);
        printf("  %s  mask %03x  %10u cycles  %6.1f ms\n", labels[i],
               (unsigned)masks[i], (unsigned)c,
               (double)c / (double)T4_CYCLES_PER_US / 1000.0);
    }

    print_provenance(&costs);
    return 0;
}

static int cmd_costs(const options_t *opt)
{
    device_costs_t costs;
    if (load_costs(opt, &costs) != 0) {
        return 1;
    }

    printf("Per layer cost measured on the board\n\n");
    printf("  %-6s %12s %12s %12s\n", "layer", "INT8", "FP32", "INT8 saving");
    for (int i = 0; i < costs.layers; i++) {
        long saving = (long)costs.fp32_cycles[i] - (long)costs.int8_cycles[i];
        printf("  %-6s %12u %12u %12ld%s\n", costs.name[i],
               (unsigned)costs.int8_cycles[i], (unsigned)costs.fp32_cycles[i],
               saving, saving < 0 ? "   INT8 is slower here" : "");
    }

    printf("\n  The four depthwise layers are the ones INT8 loses on, because\n");
    printf("  their kernels are scalar while every other layer runs packed\n");
    printf("  multiply accumulate. That inversion is why the cheapest mask is\n");
    printf("  mixed, and it is a property of the M33, not of this machine.\n");

    printf("\nWhole model, summed from the table above\n");
    printf("  These rank the masks against each other. They are calibration\n");
    printf("  sums rather than latencies: they exclude the conversions inserted\n");
    printf("  where consecutive layers disagree on precision, and they carry the\n");
    printf("  calibration probe's own overhead, so they neither match nor\n");
    printf("  consistently bound what the board measures end to end. Those\n");
    printf("  measured latencies are published in README.md.\n\n");
    uint32_t all_i8 = device_costs_total(&costs, 0u);
    uint32_t all_f32 = device_costs_total(&costs, ALL_FP32_MASK);
    uint32_t mixed = device_costs_total(&costs, CONVERGED_REF);
    printf("  all INT8   %10u cycles  %6.1f ms\n", (unsigned)all_i8,
           (double)all_i8 / (double)T4_CYCLES_PER_US / 1000.0);
    printf("  all FP32   %10u cycles  %6.1f ms\n", (unsigned)all_f32,
           (double)all_f32 / (double)T4_CYCLES_PER_US / 1000.0);
    printf("  mask %03x   %10u cycles  %6.1f ms\n", (unsigned)CONVERGED_REF,
           (unsigned)mixed, (double)mixed / (double)T4_CYCLES_PER_US / 1000.0);

    printf("\n  Source %s\n", costs.source);
    return 0;
}

static int cmd_verify(const options_t *opt)
{
    device_costs_t costs;
    if (load_costs(opt, &costs) != 0) {
        return 1;
    }
    host_clock_init(&costs);
    dwt_init();
    kws_features_init();
    ukernel_init();
    if (ipc_objects_init() != E_OK) {
        fprintf(stderr, "kernel objects failed to create\n");
        return 1;
    }

    struct { uint32_t mask; const char *name; } cases[] = {
        { 0u,             "all INT8"  },
        { ALL_FP32_MASK,  "all FP32"  },
        { CONVERGED_REF,  "converged" },
    };

    /* Scored on this thread, which is not a task, so the bracket applies. */
    host_clock_set_inference_task(0);

    printf("Inference core against the %u sample evaluation set\n",
           (unsigned)EVAL_SAMPLE_COUNT);
    printf("Cost table %s\n\n", costs.source);

    int failures = 0;
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        unsigned correct = 0;
        kws_result_t result;
        for (unsigned i = 0; i < EVAL_SAMPLE_COUNT; i++) {
            kws_infer(&eval_features[(size_t)i * EVAL_ELEMS_PER_SAMPLE],
                      cases[c].mask, T4_DEADLINE_CYCLES, &result);
            if (result.top_class == (int8_t)eval_labels[i]) {
                correct++;
            }
        }
        host_clock_check_pattern();
        double pct = 100.0 * (double)correct / (double)EVAL_SAMPLE_COUNT;
        printf("  %-10s mask %03x   %3u of %u correct   %.1f percent\n",
               cases[c].name, (unsigned)cases[c].mask, correct,
               (unsigned)EVAL_SAMPLE_COUNT, pct);
        if (correct * 1000u / EVAL_SAMPLE_COUNT < 900u) {
            failures++;
        }
    }

    printf("\n  Identical accuracy across every precision configuration is the\n");
    printf("  evidence that switching precision preserves meaning rather than\n");
    printf("  merely running, and 94.0 percent is what the board reports for\n");
    printf("  the same three masks. This build takes the scalar INT8 fallback\n");
    printf("  where the device takes the packed one; the two differ only in the\n");
    printf("  order of integer additions and the accumulator cannot overflow\n");
    printf("  for this model, so they are expected to agree exactly, but this\n");
    printf("  command checks accuracy rather than comparing logits per sample\n");
    printf("  against the device.\n");

    if (failures > 0) {
        printf("\n  A configuration scored below 90 percent, which is a real\n");
        printf("  failure and not a tolerance issue.\n");
        return 1;
    }
    return 0;
}

static void usage(void)
{
    printf(
"kws-desktop, the adaptive keyword spotting pipeline on a desktop\n"
"\n"
"Runs the same five uT-Kernel tasks the NUCLEO-H533RE runs, from the same\n"
"sources, so the system can be evaluated without a board. Per layer cycle\n"
"costs are replayed from a recorded device capture, because the mixed\n"
"precision result depends on a property of the M33 that no host CPU has.\n"
"\n"
"Usage\n"
"  kws-desktop <command> [options]\n"
"\n"
"Commands\n"
"  run        Run the pipeline on replayed audio and report what happened\n"
"  converge   Wait for the controller to settle and show both endpoints\n"
"  costs      Print the replayed per layer cost table and what it implies\n"
"  verify     Check the inference core against the evaluation set\n"
"\n"
"Options\n"
"  --seconds N     how long to run, default 20\n"
"  --wav PATH      classify a 16 kHz mono WAV instead of the built in corpus\n"
"  --speed X       pacing multiplier, default 1 which is real time\n"
"  --capture PATH  device capture to replay costs from, default the built in\n"
"                  table from the published run\n"
"  --trace         print controller decisions as they are made\n"
"\n"
"Examples\n"
"  kws-desktop run --seconds 30\n"
"  kws-desktop converge\n"
"  kws-desktop run --wav yes.wav --seconds 5\n"
"  kws-desktop costs --capture experiments/<run>/data/report.txt\n");
}

int main(int argc, char **argv)
{
    options_t opt = { .seconds = 20, .wav = NULL, .capture = NULL,
                      .speed = 1.0, .trace = 0 };

    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];
    for (int i = 2; i < argc; i++) {
        int takes_value = strcmp(argv[i], "--seconds") == 0
                       || strcmp(argv[i], "--wav") == 0
                       || strcmp(argv[i], "--capture") == 0
                       || strcmp(argv[i], "--speed") == 0;
        if (takes_value && i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", argv[i]);
            return 1;
        }

        if (strcmp(argv[i], "--seconds") == 0) {
            const char *raw = argv[++i];
            char *end = NULL;
            unsigned long long v = strtoull(raw, &end, 10);
            if (raw[0] == '-' || end == raw || *end != '\0' || v == 0 || v > 86400) {
                fprintf(stderr,
                        "--seconds takes a whole number of seconds from 1 to "
                        "86400, not '%s'\n", raw);
                return 1;
            }
            opt.seconds = (unsigned)v;
        } else if (strcmp(argv[i], "--wav") == 0) {
            opt.wav = argv[++i];
        } else if (strcmp(argv[i], "--capture") == 0) {
            opt.capture = argv[++i];
        } else if (strcmp(argv[i], "--speed") == 0) {
            const char *raw = argv[++i];
            char *end = NULL;
            double v = strtod(raw, &end);
            if (end == raw || *end != '\0' || !(v >= 0.01) || !(v <= 100.0)) {
                fprintf(stderr,
                        "--speed takes a multiplier from 0.01 to 100, not '%s'\n",
                        raw);
                return 1;
            }
            opt.speed = v;
        } else if (strcmp(argv[i], "--trace") == 0) {
            opt.trace = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unrecognised option %s\n", argv[i]);
            return 1;
        }
    }

    if (opt.seconds == 0) {
        opt.seconds = 1;
    }

    if (strcmp(cmd, "run") == 0) {
        return cmd_run(&opt);
    }
    if (strcmp(cmd, "converge") == 0) {
        return cmd_converge(&opt);
    }
    if (strcmp(cmd, "costs") == 0) {
        return cmd_costs(&opt);
    }
    if (strcmp(cmd, "verify") == 0) {
        return cmd_verify(&opt);
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }

    fprintf(stderr, "unrecognised command %s\n", cmd);
    usage();
    return 1;
}
