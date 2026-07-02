#pragma once

#include <stdint.h>
#include <tk/tkernel.h>
#include "app_config.h"

void t1_dma_ingest_task(INT stacd, void *exinf);
const int32_t *t1_get_buffer(void);
