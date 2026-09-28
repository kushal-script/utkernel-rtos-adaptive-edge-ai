#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* T1, the ingest stage, owns the capture hardware and applies window resizes. */

typedef struct {
    uint32_t window_samples;
    uint32_t resizes;
    uint32_t resize_failures;
} t1_stats_t;

extern t1_stats_t t1_stats;

void t1_ingest_task(INT stacd, void *exinf);
