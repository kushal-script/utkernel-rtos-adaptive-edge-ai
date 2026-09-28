#include "t5_controller.h"

#include <stdbool.h>
#include <stdint.h>

#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "ipc_objects.h"
#include "kws_model.h"
#include "signal_source.h"
#include "t2_variance.h"
#include "t4_inference.h"

t5_stats_t t5_stats;
t5_trace_t t5_trace[BENCH_HISTORY];
uint32_t t5_trace_count;

uint32_t t5_trace_capacity(void) { return BENCH_HISTORY; }

/* Resize messages come from a rotating pool, a message must stay valid until received. */
#define WINDOW_MSG_SLOTS 4
static window_msg_t window_msgs[WINDOW_MSG_SLOTS];
static uint32_t window_msg_next;

/* Energy ratio for T2_VAD_MARGIN_DB, a fixed factor so the hot path needs no logarithm. */
#define VAD_MARGIN_FACTOR 8u

static void send_window(uint32_t window_samples, uint32_t active_frames)
{
    window_msg_t *msg = &window_msgs[window_msg_next];
    window_msg_next = (window_msg_next + 1u) % WINDOW_MSG_SLOTS;

    msg->window_samples = (uint16_t)window_samples;
    msg->active_frames  = (uint16_t)active_frames;
    if (tk_snd_mbx(mbxid_window, (T_MSG *)msg) == E_OK) {
        t5_stats.window_changes++;
    }
}

static uint32_t clamp_window(uint32_t value)
{
    if (value < T1_WINDOW_MIN) {
        return T1_WINDOW_MIN;
    }
    if (value > T1_WINDOW_MAX) {
        return T1_WINDOW_MAX;
    }
    return value - (value % T1_WINDOW_STEP);
}

/* The gate lever, T3 peeks this flag without blocking. */
static void set_feature_gate(bool skip)
{
    if (skip) {
        tk_set_flg(flgid_gate, FLG_SKIP_FEATURES);
    } else {
        tk_clr_flg(flgid_gate, ~FLG_SKIP_FEATURES);
    }
}

static void set_inference_priority(PRI priority)
{
    if (tskid_t4 > 0) {
        tk_chg_pri(tskid_t4, priority);
    }
}

/* The single move that most reduces projected cost, a hill climb that cannot cycle, see docs/adaptation.md. */
typedef struct {
    int32_t index;
    uint8_t action;
    int32_t gain;
} t5_move_t;

static t5_move_t best_move(uint32_t mask)
{
    t5_move_t best = { -1, T5_ACTION_NONE, 0 };

    for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
        bool is_fp32 = ((mask >> i) & 1u) != 0u;
        int32_t gain = is_fp32 ? t4_layer_saving(i) : -t4_layer_saving(i);
        if (gain > best.gain) {
            best.gain = gain;
            best.index = (int32_t)i;
            best.action = is_fp32 ? T5_ACTION_DEMOTE : T5_ACTION_PROMOTE;
        }
    }
    return best;
}

static void trace_decision(uint32_t before, uint32_t after, uint8_t action,
                           uint8_t over, uint32_t cycles)
{
    if (t5_trace_count >= BENCH_HISTORY) {
        return;
    }
    t5_trace_t *row = &t5_trace[t5_trace_count++];
    row->decision     = (uint16_t)t5_stats.decisions;
    row->mask_before  = (uint16_t)before;
    row->mask_after   = (uint16_t)after;
    row->action       = action;
    row->over_deadline = over;
    row->cycles       = cycles;
    row->ewma         = t5_stats.cycles_ewma;
}

void t5_controller_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* Start at the reference precision and let pressure push it down. */
    uint32_t mask = (KWS_NUM_LAYERS >= 32) ? 0xFFFFFFFFu
                                           : ((1u << KWS_NUM_LAYERS) - 1u);
    adapt_state.precision_mask = mask;

    uint32_t window = T1_WINDOW_DEFAULT;
    uint32_t active = T3_ACTIVE_FRAMES_MAX;
    uint32_t quiet_run = 0;
    uint32_t settled = 0;
    uint32_t probe_started = 0;
    uint32_t clean_run = 0;
    uint32_t last_overruns = 0;

    for (;;) {
        UINT pattern = 0;
        ER err = tk_wai_flg(flgid_control, FLG_CONTROL_ANY,
                            TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        if (err < E_OK) {
            continue;
        }
        t5_stats.decisions++;

        /* The floor is learned only from blocks the gate judged quiet. */
        if (t2_stats.noise_floor > 0) {
            uint32_t learned = t2_stats.noise_floor * VAD_MARGIN_FACTOR;
            if (learned != adapt_state.vad_threshold) {
                adapt_state.vad_threshold = learned;
                t5_stats.threshold_updates++;
            }
        }

        if (pattern & FLG_QUIESCENT) {
            quiet_run++;
            /* Quiet: widen the capture block and shorten the context. */
            if (quiet_run > 2) {
                set_feature_gate(true);
                uint32_t wider = clamp_window(window + T1_WINDOW_STEP);
                uint32_t shorter = active > T3_ACTIVE_FRAMES_MIN + 4
                                       ? active - 4
                                       : T3_ACTIVE_FRAMES_MIN;
                if (wider != window || shorter != active) {
                    window = wider;
                    active = shorter;
                    send_window(window, active);
                }
            }
            continue;
        }

        /* Any live event reopens the gate immediately. */
        quiet_run = 0;
        set_feature_gate(false);

        /* Overrun and done arrive together, so a late inference is one decision. */
        if (pattern & (FLG_INFERENCE_DONE | FLG_BUDGET_EXCEEDED)) {
            /* Inference cost as an exponential mean. */
            uint32_t cost = t4_stats.last.total_cycles;
            t5_stats.cycles_ewma = t5_stats.cycles_ewma == 0
                                       ? cost
                                       : t5_stats.cycles_ewma -
                                             (t5_stats.cycles_ewma / 8u) + (cost / 8u);

            uint32_t deadline = adapt_state.deadline_cycles;

            /* Re read the mask every decision, the benchmark writes it directly to pin a configuration. */
            mask = adapt_state.precision_mask;
            uint32_t before = mask;
            uint8_t action = T5_ACTION_NONE;
            uint8_t over = ((pattern & FLG_BUDGET_EXCEEDED) || cost > deadline);

            if (over) {
                t5_stats.deadline_misses++;
            }

            /* Precision moves wait for the measured cost table. */
            if (t4_cost_table.valid && !adapt_state.pin_precision) {
                t5_move_t move = best_move(mask);
                if (move.index >= 0) {
                    if (move.action == T5_ACTION_DEMOTE) {
                        mask &= ~(1u << move.index);
                        t5_stats.demotions++;
                    } else {
                        mask |= (1u << move.index);
                        t5_stats.promotions++;
                    }
                    adapt_state.precision_mask = mask;
                    action = move.action;
                }

                /* Priority tracks the deadline, not the move. */
                if (over && !t5_stats.urgent) {
                    set_inference_priority(PRI_T4_URGENT);
                    t5_stats.urgent = 1;
                    t5_stats.priority_raises++;
                } else if (!over && t5_stats.urgent) {
                    set_inference_priority(PRI_T4_INFERENCE);
                    t5_stats.urgent = 0;
                }
            }

            /* On first convergence restart once from the opposite extreme to check path independence. */
            /* Not while pinned, a pinned controller never moves and the probe would overwrite the held mask. */
            if (action == T5_ACTION_NONE && !adapt_state.pin_precision) {
                settled++;
                if (settled >= T5_CONVERGE_DECISIONS && !t5_stats.converged) {
                    t5_stats.converged_at = t5_stats.decisions;
                    if (!probe_started) {
                        probe_started = 1;
                        t5_stats.converged_mask = mask;
                        settled = 0;
                        mask = 0u;
                        adapt_state.precision_mask = mask;
                    } else {
                        t5_stats.converged = 1;
                        t5_stats.probe_mask = mask;
                    }
                }
            } else {
                settled = 0;
            }

            trace_decision(before, mask, action, over, cost);

            /* Widen on dropped blocks, shorten only while the chain keeps up. */
            uint32_t overruns_now = signal_source_overruns();
            bool capture_behind = overruns_now > last_overruns;
            last_overruns = overruns_now;

            uint32_t target_window = window;
            if (capture_behind) {
                clean_run = 0;
                target_window = clamp_window(window + T1_WINDOW_STEP);
            } else if (++clean_run >= T5_WINDOW_SHRINK_AFTER) {
                clean_run = 0;
                target_window = clamp_window(window - T1_WINDOW_STEP);
            }

            uint32_t longer = active + 4 < T3_ACTIVE_FRAMES_MAX
                                  ? active + 4
                                  : T3_ACTIVE_FRAMES_MAX;
            if (target_window != window || longer != active) {
                window = target_window;
                active = longer;
                send_window(window, active);
            }
        }
    }
}
