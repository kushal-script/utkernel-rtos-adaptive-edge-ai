#include "t5_controller.h"

#include <tm/tmonitor.h>

#include "app_config.h"
#include "app_tasks.h"
#include "ipc_objects.h"
#include "kws_model.h"
#include "t2_variance.h"
#include "t4_inference.h"

t5_stats_t t5_stats;

/* Window resize messages are sent from a small rotating pool, because a
   mailbox message must stay valid until the receiver has taken it. */
#define WINDOW_MSG_SLOTS 4
static window_msg_t window_msgs[WINDOW_MSG_SLOTS];
static uint32_t window_msg_next;

/* Energy ratio corresponding to T2_VAD_MARGIN_DB, held as a fixed factor so
   the gate needs no logarithm on the hot path. Nine decibels is a factor of
   about eight in power. */
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

/* Demote the latest FP32 layer to INT8. Later layers are demoted first because
   the earliest layers carry the most information about the input. */
static bool demote_one(uint32_t *mask)
{
    for (int32_t i = KWS_NUM_LAYERS - 1; i >= 0; i--) {
        if ((*mask >> i) & 1u) {
            *mask &= ~(1u << i);
            return true;
        }
    }
    return false;
}

/* The gate lever from the program plan. T3 peeks this flag without blocking,
   so the controller can suppress the feature stage entirely on silence. */
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

static bool promote_one(uint32_t *mask)
{
    for (uint32_t i = 0; i < KWS_NUM_LAYERS; i++) {
        if (((*mask >> i) & 1u) == 0u) {
            *mask |= (1u << i);
            return true;
        }
    }
    return false;
}

void t5_controller_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /* Start at the reference precision and let pressure push it down, which is
       the direction the program plan describes. */
    uint32_t mask = (KWS_NUM_LAYERS >= 32) ? 0xFFFFFFFFu
                                           : ((1u << KWS_NUM_LAYERS) - 1u);
    adapt_state.precision_mask = mask;

    uint32_t window = T1_WINDOW_DEFAULT;
    uint32_t active = T3_ACTIVE_FRAMES_MAX;
    uint32_t quiet_run = 0;

    for (;;) {
        UINT pattern = 0;
        ER err = tk_wai_flg(flgid_control, FLG_CONTROL_ANY,
                            TWF_ORW | TWF_BITCLR, &pattern, TMO_FEVR);
        if (err < E_OK) {
            continue;
        }
        t5_stats.decisions++;

        /* Learn the gate. The floor comes from blocks the gate itself judged
           quiet, so speech never drags the threshold up behind itself. */
        if (t2_stats.noise_floor > 0) {
            uint32_t learned = t2_stats.noise_floor * VAD_MARGIN_FACTOR;
            if (learned != adapt_state.vad_threshold) {
                adapt_state.vad_threshold = learned;
                t5_stats.threshold_updates++;
            }
        }

        if (pattern & FLG_QUIESCENT) {
            quiet_run++;
            /* Nothing is happening. Widen the capture block so the pipeline is
               woken less often, and shorten the context the model is given. */
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

        quiet_run = 0;
        set_feature_gate(false);

        /* T4 raises the overrun flag and the done flag for the same inference.
           Treating them as two events would demote a layer and then promote it
           straight back, so an overrun is handled once and the done flag is
           ignored when it arrives alongside one. */
        if (pattern & (FLG_INFERENCE_DONE | FLG_BUDGET_EXCEEDED)) {
            /* Learn the cost of an inference as an exponential mean, so the
               controller reacts to the machine it is actually running on. */
            uint32_t cost = t4_stats.last.total_cycles;
            t5_stats.cycles_ewma = t5_stats.cycles_ewma == 0
                                       ? cost
                                       : t5_stats.cycles_ewma -
                                             (t5_stats.cycles_ewma / 8u) + (cost / 8u);

            uint32_t deadline = adapt_state.deadline_cycles;

            if ((pattern & FLG_BUDGET_EXCEEDED) || cost > deadline) {
                if (demote_one(&mask)) {
                    adapt_state.precision_mask = mask;
                    t5_stats.demotions++;
                }
                if (!t5_stats.urgent) {
                    set_inference_priority(PRI_T4_URGENT);
                    t5_stats.urgent = 1;
                    t5_stats.priority_raises++;
                }
            } else if (((pattern & FLG_BUDGET_EXCEEDED) == 0) &&
                       t5_stats.cycles_ewma * 2u < deadline) {
                /* Comfortable slack, take some accuracy back. */
                if (promote_one(&mask)) {
                    adapt_state.precision_mask = mask;
                    t5_stats.promotions++;
                }
                if (t5_stats.urgent) {
                    set_inference_priority(PRI_T4_INFERENCE);
                    t5_stats.urgent = 0;
                }
            }

            /* Speech is present, favour responsiveness and full context. */
            uint32_t tighter = clamp_window(window - T1_WINDOW_STEP);
            uint32_t longer = active + 4 < T3_ACTIVE_FRAMES_MAX
                                  ? active + 4
                                  : T3_ACTIVE_FRAMES_MAX;
            if (tighter != window || longer != active) {
                window = tighter;
                active = longer;
                send_window(window, active);
            }
        }
    }
}
