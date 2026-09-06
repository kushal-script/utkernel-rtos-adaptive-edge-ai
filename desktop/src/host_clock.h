#pragma once

#include <stdint.h>

#include "device_costs.h"

/* The virtual cycle counter that stands in for the DWT. Costs are replayed from
   a device capture rather than measured here, for the reason set out at the top
   of host_clock.c. */

#include <tk/tkernel.h>

void                  host_clock_init(const device_costs_t *table);
const device_costs_t *host_clock_costs(void);

/* The task whose cycle reads are the bracketed per layer sequence kws_infer
   emits. Every other caller gets plain host time. Zero means the thread that
   is not a task, which is how the evaluation set is scored. */
void host_clock_set_inference_task(ID id);

/* Fails loudly if an inference did not emit the bracketed timing sequence the
   replay depends on, because a silent mismatch would misattribute every cost. */
void host_clock_check_pattern(void);
