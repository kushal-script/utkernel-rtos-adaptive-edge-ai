#pragma once

#include <tk/tkernel.h>

/* T1 ISR to downstream tasks. Full IPC map is in docs/architecture.md. */
#define FLG_HALF_READY  (1u << 0)
#define FLG_FULL_READY  (1u << 1)

extern ID flgid_audio;

void ipc_objects_init(void);
