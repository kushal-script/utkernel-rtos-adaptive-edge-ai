/*
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 */

#include "main.h"
#include "app_config.h"

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
extern DMA_HandleTypeDef hdma_spi2_rx;
extern const int32_t *signal_source_i2s_buffer(void);
extern uint32_t signal_source_i2s_words(void);

static DMA_NodeTypeDef  spi2_rx_node;
static DMA_QListTypeDef spi2_rx_queue;
#endif

void HAL_MspInit(void)
{
}

void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hadc->Instance==ADC1)
  {
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
    PeriphClkInitStruct.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_ADC_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(ADC1_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(ADC1_IRQn);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* hadc)
{
  if(hadc->Instance==ADC1)
  {
    __HAL_RCC_ADC_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0|GPIO_PIN_1);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0|GPIO_PIN_1);

    HAL_NVIC_DisableIRQ(ADC1_IRQn);
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hi2c->Instance==I2C1)
  {
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    PeriphClkInitStruct.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    __HAL_RCC_I2C1_CLK_ENABLE();

    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* hi2c)
{
  if(hi2c->Instance==I2C1)
  {
    __HAL_RCC_I2C1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);

    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(huart->Instance==USART2)
  {
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART2;
    PeriphClkInitStruct.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
  if(huart->Instance==USART2)
  {
    __HAL_RCC_USART2_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);
  }
}

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
/* INMP441 wiring, kernel clock choice, and DMA shape are in docs/hardware.md. */
void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  if (hi2s->Instance == SPI2)
  {
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI2;
    PeriphClkInitStruct.Spi2ClockSelection   = RCC_SPI2CLKSOURCE_PLL1Q;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Init.Mode must carry the circular value, otherwise I2S_DMARxCplt
       disables the DMA request after the first block. */
    hdma_spi2_rx.Instance                   = GPDMA1_Channel0;
    hdma_spi2_rx.Init.Request               = GPDMA1_REQUEST_SPI2_RX;
    hdma_spi2_rx.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
    hdma_spi2_rx.Init.Direction             = DMA_PERIPH_TO_MEMORY;
    hdma_spi2_rx.Init.SrcInc                = DMA_SINC_FIXED;
    hdma_spi2_rx.Init.DestInc               = DMA_DINC_INCREMENTED;
    hdma_spi2_rx.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_HALFWORD;
    hdma_spi2_rx.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_HALFWORD;
    hdma_spi2_rx.Init.SrcBurstLength        = 1;
    hdma_spi2_rx.Init.DestBurstLength       = 1;
    hdma_spi2_rx.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 |
                                              DMA_DEST_ALLOCATED_PORT0;
    hdma_spi2_rx.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
    hdma_spi2_rx.Init.Mode                  = DMA_LINKEDLIST_CIRCULAR;

    hdma_spi2_rx.InitLinkedList.Priority          = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    hdma_spi2_rx.InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
    hdma_spi2_rx.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    hdma_spi2_rx.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hdma_spi2_rx.InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;
    if (HAL_DMAEx_List_Init(&hdma_spi2_rx) != HAL_OK)
    {
      Error_Handler();
    }

    DMA_NodeConfTypeDef node = {0};
    node.NodeType   = DMA_GPDMA_LINEAR_NODE;
    node.Init       = hdma_spi2_rx.Init;
    node.Init.Mode  = DMA_NORMAL;
    node.SrcAddress = (uint32_t)&SPI2->RXDR;
    node.DstAddress = (uint32_t)signal_source_i2s_buffer();
    node.DataSize   = signal_source_i2s_words() * sizeof(int32_t);
    if (HAL_DMAEx_List_BuildNode(&node, &spi2_rx_node) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_DMAEx_List_InsertNode_Tail(&spi2_rx_queue, &spi2_rx_node) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_DMAEx_List_SetCircularMode(&spi2_rx_queue) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_DMAEx_List_LinkQ(&hdma_spi2_rx, &spi2_rx_queue) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(hi2s, hdmarx, hdma_spi2_rx);

    if (HAL_DMA_ConfigChannelAttributes(&hdma_spi2_rx,
                                        DMA_CHANNEL_NPRIV) != HAL_OK)
    {
      Error_Handler();
    }
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI2)
  {
    __HAL_RCC_SPI2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15);
    HAL_DMA_DeInit(&hdma_spi2_rx);
  }
}
#endif
