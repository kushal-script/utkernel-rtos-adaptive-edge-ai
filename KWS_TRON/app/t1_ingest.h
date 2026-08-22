#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* T1, the ingest stage. It owns the capture hardware and nothing else. The
   interrupt that fills a block is what wakes the rest of the pipeline, so this
   task itself is idle almost all the time, blocked on the mailbox waiting for
   the controller to resize the capture window. */

typedef struct {
    uint32_t window_samples;
    uint32_t resizes;
    uint32_t resize_failures;
} t1_stats_t;

extern t1_stats_t t1_stats;

void t1_ingest_task(INT stacd, void *exinf);
