#pragma once

#include <stdint.h>

/* Idle residency from the BSP power hook, reported as a share of wall time, see docs/power.md. */

uint64_t bsp_idle_cycles(void);
uint32_t bsp_idle_entries(void);
