#pragma once

#include <stdint.h>

/* Idle residency measured by the BSP power hook, which is the closest thing to
   an energy figure obtainable without an ammeter. Reported as a fraction of
   wall time so configurations can be compared as a ratio, which is a defensible
   statement, rather than converted to milliwatts, which would not be.
   See docs/power.md. */

uint64_t bsp_idle_cycles(void);
uint32_t bsp_idle_entries(void);
