#pragma once
#include <stddef.h>
#include <stdint.h>

#define DEVICE_COST_LAYERS 10

/* Per layer cycle costs measured on the board, the table the controller ranks
   its moves against. A desktop cannot measure these, so they are replayed from
   a recorded capture and the provenance travels with them. */
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

/* The table from the published run, compiled in so the tool works with no
   arguments and with no experiments directory present. */
void device_costs_builtin(device_costs_t *out);

/* Total cycles one inference costs under a precision mask, bit n set meaning
   layer n runs FP32. */
uint32_t device_costs_total(const device_costs_t *c, uint32_t precision_mask);
