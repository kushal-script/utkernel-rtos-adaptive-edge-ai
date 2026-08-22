#include "t1_ingest.h"

#include <tm/tmonitor.h>

#include "app_config.h"
#include "ipc_objects.h"
#include "signal_source.h"

t1_stats_t t1_stats;

void t1_ingest_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    if (signal_source_init() != E_OK) {
        tm_putstring((UB *)"T1: signal source init failed\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }

    uint32_t window = adapt_state.window_samples;
    if (signal_source_start(window) != E_OK) {
        tm_putstring((UB *)"T1: capture start failed\n");
        tk_slp_tsk(TMO_FEVR);
        return;
    }
    t1_stats.window_samples = window;
    tm_printf((UB *)"T1: capture running, window %u samples\n", (unsigned)window);

    for (;;) {
        T_MSG *raw = NULL;
        if (tk_rcv_mbx(mbxid_window, &raw, TMO_FEVR) < E_OK) {
            continue;
        }

        const window_msg_t *msg = (const window_msg_t *)raw;
        adapt_state.active_frames = msg->active_frames;

        if (msg->window_samples != t1_stats.window_samples) {
            if (signal_source_set_window(msg->window_samples) == E_OK) {
                t1_stats.window_samples = msg->window_samples;
                adapt_state.window_samples = msg->window_samples;
                t1_stats.resizes++;
            } else {
                t1_stats.resize_failures++;
            }
        }
    }
}
