/*
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 */

#include "main.h"
#include "stm32h5xx_it.h"

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
#include "app_config.h"

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
extern DMA_HandleTypeDef hdma_spi2_rx;
#endif

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void ADC1_IRQHandler(void)
{
  HAL_ADC_IRQHandler(&hadc1);
}

void I2C1_EV_IRQHandler(void)
{
  HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void)
{
  HAL_I2C_ER_IRQHandler(&hi2c1);
}

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
/* SPI2 RX audio stream. HAL_DMA_IRQHandler dispatches into the I2S half and
   full complete callbacks implemented in app/signal_source.c. */
void GPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi2_rx);
}
#endif
