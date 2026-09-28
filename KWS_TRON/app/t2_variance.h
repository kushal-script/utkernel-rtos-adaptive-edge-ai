#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "sample_ring.h"

/* T2, the variance monitor and voice activity gate, see docs/adaptation.md. */

typedef struct {
    uint32_t energy;         /* mean square of the last block          */
    uint32_t noise_floor;    /* slow estimate, only updated when quiet */
    uint32_t blocks_seen;
    uint32_t blocks_active;  /* blocks that passed the gate            */
    uint32_t hangover;       /* blocks still forced active after speech */
} t2_stats_t;

extern t2_stats_t t2_stats;
extern sample_ring_t t2_ring;

void t2_variance_task(INT stacd, void *exinf);

/* Mean square of a block, the quantity the gate compares. */
uint32_t t2_block_energy(const int16_t *samples, uint32_t count);
