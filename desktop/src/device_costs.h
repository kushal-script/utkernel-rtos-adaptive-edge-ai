#pragma once
#include <stddef.h>
#include <stdint.h>

#define DEVICE_COST_LAYERS 10

/* Per layer cycle costs replayed from a board capture, a desktop cannot measure them. */
typedef struct {
    uint32_t int8_cycles[DEVICE_COST_LAYERS];
    uint32_t fp32_cycles[DEVICE_COST_LAYERS];
    char     name[DEVICE_COST_LAYERS][16];
    int      layers;
    char     source[256];
} device_costs_t;

/* Parses the BENCH_COST lines out of a raw device capture or a parsed report. */
int  device_costs_load(const char *report_path, device_costs_t *out,
                       char *err, size_t errlen);

/* The published run's table, compiled in so the tool needs no arguments. */
void device_costs_builtin(device_costs_t *out);

/* Total cycles of one inference under a mask, bit n set means layer n runs FP32. */
uint32_t device_costs_total(const device_costs_t *c, uint32_t precision_mask);
