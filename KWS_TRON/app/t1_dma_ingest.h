#pragma once

#include <stdint.h>
#include <tk/tkernel.h>

/* 1024 words = 512 stereo frames = 32 ms at 16 kHz. See docs/hardware.md. */
#define T1_AUDIO_BUFFER_LEN  1024
#define T1_AUDIO_HALF_LEN    (T1_AUDIO_BUFFER_LEN / 2)

void t1_dma_ingest_task(INT stacd, void *exinf);
const int32_t *t1_get_buffer(void);
