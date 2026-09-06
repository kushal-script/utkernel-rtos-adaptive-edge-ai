#pragma once

#include <tk/tkernel.h>

/* The kernel monitor's console output, which the firmware uses for every
   telemetry line. On the board these reach the virtual COM port; here they
   reach stdout, so a desktop run produces the same BENCH_ lines a serial
   capture does and tools/parse_bench.py can read either. */

void tm_putstring(UB *str);
void tm_printf(UB *fmt, ...);
