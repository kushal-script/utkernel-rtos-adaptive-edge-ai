#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* T5, the only writer of adaptation decisions; thresholds are learned at runtime, see docs/adaptation.md. */

typedef struct {
    uint32_t decisions;
    uint32_t demotions;        /* layers dropped to INT8 under pressure */
    uint32_t promotions;       /* layers restored to FP32 with slack    */
    uint32_t window_changes;
    uint32_t priority_raises;
    uint32_t cycles_ewma;      /* learned cost of one inference         */
    uint32_t threshold_updates;
    uint8_t  urgent;           /* T4 currently running at raised priority */
    uint8_t  converged;        /* no lever has moved for T5_CONVERGE_DECISIONS */
    uint32_t converged_mask;
    uint32_t probe_mask;       /* where a restart from the other extreme landed */
    uint32_t converged_at;     /* decision index convergence was declared */
    uint32_t deadline_misses;  /* inferences over the deadline, mostly transient */
} t5_stats_t;

/* One controller decision, kept so the convergence transient can be plotted. */
typedef enum {
    T5_ACTION_NONE = 0,
    T5_ACTION_DEMOTE,
    T5_ACTION_PROMOTE,
} t5_action_t;

typedef struct {
    uint16_t decision;
    uint16_t mask_before;
    uint16_t mask_after;
    uint8_t  action;
    uint8_t  over_deadline;
    uint32_t cycles;
    uint32_t ewma;
} t5_trace_t;

extern t5_trace_t t5_trace[];
extern uint32_t t5_trace_count;
uint32_t t5_trace_capacity(void);

extern t5_stats_t t5_stats;

void t5_controller_task(INT stacd, void *exinf);
