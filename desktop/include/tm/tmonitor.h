#pragma once

#include <tk/tkernel.h>

/* Monitor console output, stdout here, so BENCH_ lines match a serial capture. */

void tm_putstring(UB *str);
void tm_printf(UB *fmt, ...);
