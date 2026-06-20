/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BEEP_Pin GPIO_PIN_3
#define BEEP_GPIO_Port GPIOC
#define SCL_Pin GPIO_PIN_2
#define SCL_GPIO_Port GPIOA
#define SDA_Pin GPIO_PIN_15
#define SDA_GPIO_Port GPIOA
#define Low_High_Pin GPIO_PIN_6
#define Low_High_GPIO_Port GPIOA
#define Sharp_Flash_Pin GPIO_PIN_7
#define Sharp_Flash_GPIO_Port GPIOA
#define SCLK_Pin GPIO_PIN_4
#define SCLK_GPIO_Port GPIOC
#define SYNC_Pin GPIO_PIN_5
#define SYNC_GPIO_Port GPIOC
#define DIN_Pin GPIO_PIN_0
#define DIN_GPIO_Port GPIOB
#define DS18B20_Pin GPIO_PIN_1
#define DS18B20_GPIO_Port GPIOB
#define KEY2_Pin GPIO_PIN_12
#define KEY2_GPIO_Port GPIOB
#define KEY3_Pin GPIO_PIN_13
#define KEY3_GPIO_Port GPIOB
#define TM_SCL_Pin GPIO_PIN_14
#define TM_SCL_GPIO_Port GPIOB
#define TM_SDA_Pin GPIO_PIN_15
#define TM_SDA_GPIO_Port GPIOB
#define KEY1_Pin GPIO_PIN_6
#define KEY1_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_7
#define LED_GPIO_Port GPIOC
#define W5500_INT_Pin GPIO_PIN_12
#define W5500_INT_GPIO_Port GPIOC
#define W5500_INT_EXTI_IRQn EXTI15_10_IRQn
#define Is_Link_Pin GPIO_PIN_2
#define Is_Link_GPIO_Port GPIOD
#define W5500_RST_Pin GPIO_PIN_3
#define W5500_RST_GPIO_Port GPIOB
#define W5500_MOSI_Pin GPIO_PIN_4
#define W5500_MOSI_GPIO_Port GPIOB
#define W5500_MISO_Pin GPIO_PIN_5
#define W5500_MISO_GPIO_Port GPIOB
#define W5500_SCLK_Pin GPIO_PIN_6
#define W5500_SCLK_GPIO_Port GPIOB
#define W5500_SCS_Pin GPIO_PIN_7
#define W5500_SCS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
