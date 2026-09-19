/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#define RS485_DIR_Pin GPIO_PIN_1
#define RS485_DIR_GPIO_Port GPIOA
#define ESP_EN_Pin GPIO_PIN_0
#define ESP_EN_GPIO_Port GPIOB
#define ESP_WAKE_Pin GPIO_PIN_12
#define ESP_WAKE_GPIO_Port GPIOB
#define RELAY1_Pin GPIO_PIN_8
#define RELAY1_GPIO_Port GPIOD
#define RELAY2_Pin GPIO_PIN_9
#define RELAY2_GPIO_Port GPIOD
#define RELAY3_Pin GPIO_PIN_10
#define RELAY3_GPIO_Port GPIOD
#define RELAY4_Pin GPIO_PIN_11
#define RELAY4_GPIO_Port GPIOD
#define RELAY5_Pin GPIO_PIN_12
#define RELAY5_GPIO_Port GPIOD
#define RELAY6_Pin GPIO_PIN_13
#define RELAY6_GPIO_Port GPIOD
#define RELAY7_Pin GPIO_PIN_14
#define RELAY7_GPIO_Port GPIOD
#define RELAY8_Pin GPIO_PIN_15
#define RELAY8_GPIO_Port GPIOD
#define ADS1256_CS_Pin GPIO_PIN_2
#define ADS1256_CS_GPIO_Port GPIOG
#define ADS1256_DRDY_Pin GPIO_PIN_3
#define ADS1256_DRDY_GPIO_Port GPIOG
#define ADS1256_DRDY_EXTI_IRQn EXTI3_IRQn
#define ADS1256_RESET_Pin GPIO_PIN_4
#define ADS1256_RESET_GPIO_Port GPIOG
#define ADS1256_SYNC_Pin GPIO_PIN_5
#define ADS1256_SYNC_GPIO_Port GPIOG
#define STATUS_LED_Pin GPIO_PIN_8
#define STATUS_LED_GPIO_Port GPIOA
#define DI1_Pin GPIO_PIN_2
#define DI1_GPIO_Port GPIOD
#define DI2_Pin GPIO_PIN_9
#define DI2_GPIO_Port GPIOG
#define DI3_Pin GPIO_PIN_10
#define DI3_GPIO_Port GPIOG
#define DI4_Pin GPIO_PIN_11
#define DI4_GPIO_Port GPIOG
#define DI5_Pin GPIO_PIN_12
#define DI5_GPIO_Port GPIOG
#define DI6_Pin GPIO_PIN_13
#define DI6_GPIO_Port GPIOG
#define DI7_Pin GPIO_PIN_14
#define DI7_GPIO_Port GPIOG
#define DI8_Pin GPIO_PIN_15
#define DI8_GPIO_Port GPIOG
#define MAX31865_CS_Pin GPIO_PIN_8
#define MAX31865_CS_GPIO_Port GPIOB
#define MAX31865_DRDY_Pin GPIO_PIN_9
#define MAX31865_DRDY_GPIO_Port GPIOB
#define MAX31865_DRDY_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
