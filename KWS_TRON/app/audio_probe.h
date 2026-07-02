#pragma once

#include <tk/tkernel.h>

/* Bring up task, active only when KWS_AUDIO_PROBE is set. Collects a raw
   capture snapshot and streams it over UART for host side mic verification. */
void audio_probe_task(INT stacd, void *exinf);
