#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

/* On device benchmark, reported over UART for tools/parse_bench.py, never inside a timed region. */

typedef struct {
    const char *name;
    uint32_t precision_mask;
    uint32_t samples;
    uint32_t correct;
    /* 64 bit sums, 150 inferences of tens of millions of cycles overflow 32 bits. */
    uint64_t total_cycles;
    uint32_t worst_cycles;
    uint32_t best_cycles;
    uint64_t layer_cycles[16];
} bench_run_t;

void bench_task(INT stacd, void *exinf);

/* Runs one configuration over the flash evaluation set. */
void bench_run(const char *name, uint32_t precision_mask, uint32_t deadline,
               bench_run_t *out);
