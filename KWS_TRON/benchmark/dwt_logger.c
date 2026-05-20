#include <stdint.h>
#include "core_cm33.h"  /* CMSIS — DWT, CoreDebug, ITM */

void dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t dwt_read(void) {
    return DWT->CYCCNT;
}

/* Send layer cycle count over SWO ITM port 0. */
void dwt_log_layer(uint8_t layer_id, uint32_t cycles) {
    ITM_SendChar('L');
    ITM_SendChar(layer_id);
    for (int i = 3; i >= 0; i--)
        ITM_SendChar((cycles >> (8 * i)) & 0xFF);
}
