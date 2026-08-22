#include "sample_ring.h"

#include <string.h>

#define RING_MASK (T3_SAMPLE_RING - 1u)

void sample_ring_reset(sample_ring_t *ring)
{
    ring->write = 0;
    ring->read  = 0;
    memset(ring->data, 0, sizeof(ring->data));
}

uint32_t sample_ring_count(const sample_ring_t *ring)
{
    return ring->write - ring->read;
}

void sample_ring_push(sample_ring_t *ring, const int16_t *src, uint32_t count)
{
    uint32_t write = ring->write;
    for (uint32_t i = 0; i < count; i++) {
        ring->data[(write + i) & RING_MASK] = src[i];
    }
    ring->write = write + count;
}

bool sample_ring_peek(const sample_ring_t *ring, uint32_t offset,
                      int16_t *dst, uint32_t count)
{
    uint32_t write = ring->write;
    if (offset + count > T3_SAMPLE_RING || offset + count > write) {
        return false;
    }
    uint32_t start = write - offset - count;
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = ring->data[(start + i) & RING_MASK];
    }
    return true;
}

void sample_ring_advance(sample_ring_t *ring, uint32_t count)
{
    ring->read += count;
}
