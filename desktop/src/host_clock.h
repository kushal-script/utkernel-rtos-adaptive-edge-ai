#pragma once

#include <stdint.h>

#include "device_costs.h"

/* Virtual cycle counter standing in for the DWT, see host_clock.c. */

#include <tk/tkernel.h>

void                  host_clock_init(const device_costs_t *table);
const device_costs_t *host_clock_costs(void);

/* Task whose reads follow kws_infer's bracketed sequence, zero for the scoring thread. */
void host_clock_set_inference_task(ID id);

/* Fails loudly if an inference did not emit the bracketed sequence. */
void host_clock_check_pattern(void);
