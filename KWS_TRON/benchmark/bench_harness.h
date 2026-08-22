#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* On device benchmark. Runs the evaluation set that travels in flash through
   the inference core in several configurations and reports latency, accuracy,
   and the per layer cycle profile over UART, in a format tools/parse_bench.py
   turns into the figures under experiments/.

   Reporting never happens inside a timed region, results are collected into
   RAM during a run and printed afterwards, so the measurement is not perturbed
   by the act of reporting it. */

typedef struct {
    const char *name;
    uint32_t precision_mask;
    uint32_t samples;
    uint32_t correct;
    uint32_t total_cycles;
    uint32_t worst_cycles;
    uint32_t best_cycles;
    uint32_t layer_cycles[16];
} bench_run_t;

void bench_task(INT stacd, void *exinf);

/* Runs one configuration over the flash evaluation set. Exposed so a caller
   can script a sweep rather than only the default comparison. */
void bench_run(const char *name, uint32_t precision_mask, uint32_t deadline,
               bench_run_t *out);
