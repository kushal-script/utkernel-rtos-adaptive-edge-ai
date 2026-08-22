#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

/* Single producer single consumer ring between the variance stage and the
   feature stage. The DMA to CPU handoff itself is copy free, the CPU reads the
   half the DMA is not writing. This ring exists because an analysis frame is
   longer than one capture block and must not be torn across a refill. */

typedef struct {
    int16_t  data[T3_SAMPLE_RING];
    volatile uint32_t write;
    volatile uint32_t read;
} sample_ring_t;

void     sample_ring_reset(sample_ring_t *ring);
uint32_t sample_ring_count(const sample_ring_t *ring);
void     sample_ring_push(sample_ring_t *ring, const int16_t *src, uint32_t count);

/* Copies `count` samples starting `offset` behind the write cursor without
   consuming them, so overlapping frames can be read. Returns false if that
   much history is not present. */
bool sample_ring_peek(const sample_ring_t *ring, uint32_t offset,
                      int16_t *dst, uint32_t count);

void sample_ring_advance(sample_ring_t *ring, uint32_t count);
