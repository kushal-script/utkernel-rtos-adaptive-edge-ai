#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* T5, the adaptation controller. It is the only task that writes adaptation
   state, and it drives all four levers together every cycle: the capture
   window through a mailbox to T1, the active feature count, the per layer
   numeric precision, and task priority.

   The thresholds are not constants. The controller learns the quiescent noise
   floor and the achievable cycle cost at runtime and places its decision
   boundaries relative to what it observes, so it works in an acoustic
   environment it was never tuned for. See docs/adaptation.md. */

typedef struct {
    uint32_t decisions;
    uint32_t demotions;        /* layers dropped to INT8 under pressure */
    uint32_t promotions;       /* layers restored to FP32 with slack    */
    uint32_t window_changes;
    uint32_t priority_raises;
    uint32_t cycles_ewma;      /* learned cost of one inference         */
    uint32_t threshold_updates;
    uint8_t  urgent;           /* T4 currently running at raised priority */
} t5_stats_t;

extern t5_stats_t t5_stats;

void t5_controller_task(INT stacd, void *exinf);
