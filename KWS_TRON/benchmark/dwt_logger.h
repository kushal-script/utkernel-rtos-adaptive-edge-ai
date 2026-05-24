#pragma once
#include <stdint.h>

void     dwt_init(void);
uint32_t dwt_read(void);
void     dwt_log_layer(uint8_t layer_id, uint32_t cycles);
