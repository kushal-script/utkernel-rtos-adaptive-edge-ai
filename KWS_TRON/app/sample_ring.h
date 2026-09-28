#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

/* Single producer single consumer ring from T2 to T3, so a frame is never torn across a block. */

typedef struct {
    int16_t  data[T3_SAMPLE_RING];
    volatile uint32_t write;
    volatile uint32_t read;
} sample_ring_t;

void     sample_ring_reset(sample_ring_t *ring);
uint32_t sample_ring_count(const sample_ring_t *ring);
void     sample_ring_push(sample_ring_t *ring, const int16_t *src, uint32_t count);

/* Copies `count` samples starting `offset` behind the write cursor without consuming; false if absent. */
bool sample_ring_peek(const sample_ring_t *ring, uint32_t offset,
                      int16_t *dst, uint32_t count);

void sample_ring_advance(sample_ring_t *ring, uint32_t count);
