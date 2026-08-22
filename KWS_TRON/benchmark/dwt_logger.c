#include "main.h"
#include "core_cm33.h"  /* CMSIS DWT, CoreDebug, ITM */
#include "app_config.h"

void dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t dwt_read(void) {
    return DWT->CYCCNT;
}

/* Per layer cycle count over SWO ITM port 0. */
void dwt_log_layer(uint8_t layer_id, uint32_t cycles) {
    ITM_SendChar('L');
    ITM_SendChar(layer_id);
    for (int i = 3; i >= 0; i--)
        ITM_SendChar((cycles >> (8 * i)) & 0xFF);
}

/* Overrides the weak HAL implementation. Once the kernel starts, SysTick belongs
   to it and HAL_IncTick is no longer called, so every HAL timeout would wait
   forever on a tick that never advances. Deriving the tick from the cycle
   counter keeps HAL timeouts finite without adding a second timer. */
uint32_t HAL_GetTick(void)
{
    return dwt_read() / (SYSTEM_CLOCK_HZ / 1000u);
}
