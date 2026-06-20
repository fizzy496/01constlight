/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
#include "public.h"

extern uint8_t ch_st[];
extern uint16_t trig_prol[];

extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef huart1;

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
   while (1)
  {
  }
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  while (1)
  {

  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  while (1)
  {
		
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  while (1)
  {
  
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
 
  while (1)
  {
   
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
 
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
 
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
 
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
 
  HAL_IncTick();
 
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 channel4 global interrupt.
  */
void DMA1_Channel4_IRQHandler(void)
{
 
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
 
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
    /* Only handle RX via direct register read. TX uses direct register write (blocking). */
    if (USART1->SR & USART_SR_RXNE)
    {
        callback_232((uint8_t)(USART1->DR & 0xFF));
    }
}

/**
  * @brief This function handles PVD interrupt (掉电检测).
  * @note  当 VDD 低于 2.9V 时触发，紧急保存当前状态到 Flash
  */
void PVD_IRQHandler(void)
{
    HAL_PWR_PVD_IRQHandler();
}
void EXTI15_10_IRQHandler(void)
{
	(EXTI->PR)&=GPIO_PIN_12;
//	W5500_Interrupt_Process();
	return;
  HAL_GPIO_EXTI_IRQHandler(W5500_INT_Pin);
}
