/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32l4xx_it.c
 * @brief   Interrupt Service Routines.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_it.h"
#include "hardware/encoder.h"
#include "main.h"
#include "synth/time.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern I2C_HandleTypeDef hi2c2;
extern DMA_HandleTypeDef hdma_sai1_a;
extern DMA_HandleTypeDef hdma_sai1_b;
extern DMA_HandleTypeDef hdma_spi2_tx;
extern DMA_HandleTypeDef hdma_spi2_rx;
extern DMA_HandleTypeDef hdma_usart3_tx;
extern DMA_HandleTypeDef hdma_usart3_rx;
extern UART_HandleTypeDef huart3;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
 * @brief This function handles Non maskable interrupt.
 */
void NMI_Handler(void) {
	/* USER CODE BEGIN NonMaskableInt_IRQn 0 */

	/* USER CODE END NonMaskableInt_IRQn 0 */
	HAL_RCC_NMI_IRQHandler();
	/* USER CODE BEGIN NonMaskableInt_IRQn 1 */

	/* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
 * @brief This function handles Hard fault interrupt.
 */
void HardFault_Handler(void) {
	/* USER CODE BEGIN HardFault_IRQn 0 */

	/* USER CODE END HardFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_HardFault_IRQn 0 */
		/* USER CODE END W1_HardFault_IRQn 0 */
	}
}

/**
 * @brief This function handles Memory management fault.
 */
void MemManage_Handler(void) {
	/* USER CODE BEGIN MemoryManagement_IRQn 0 */

	/* USER CODE END MemoryManagement_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
		/* USER CODE END W1_MemoryManagement_IRQn 0 */
	}
}

/**
 * @brief This function handles Prefetch fault, memory access fault.
 */
void BusFault_Handler(void) {
	/* USER CODE BEGIN BusFault_IRQn 0 */

	/* USER CODE END BusFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_BusFault_IRQn 0 */
		/* USER CODE END W1_BusFault_IRQn 0 */
	}
}

/**
 * @brief This function handles Undefined instruction or illegal state.
 */
void UsageFault_Handler(void) {
	/* USER CODE BEGIN UsageFault_IRQn 0 */

	/* USER CODE END UsageFault_IRQn 0 */
	while (1) {
		/* USER CODE BEGIN W1_UsageFault_IRQn 0 */
		/* USER CODE END W1_UsageFault_IRQn 0 */
	}
}

/**
 * @brief This function handles System service call via SWI instruction.
 */
void SVC_Handler(void) {
	/* USER CODE BEGIN SVCall_IRQn 0 */

	/* USER CODE END SVCall_IRQn 0 */
	/* USER CODE BEGIN SVCall_IRQn 1 */

	/* USER CODE END SVCall_IRQn 1 */
}

/**
 * @brief This function handles Debug monitor.
 */
void DebugMon_Handler(void) {
	/* USER CODE BEGIN DebugMonitor_IRQn 0 */

	/* USER CODE END DebugMonitor_IRQn 0 */
	/* USER CODE BEGIN DebugMonitor_IRQn 1 */

	/* USER CODE END DebugMonitor_IRQn 1 */
}

/**
 * @brief This function handles Pendable request for system service.
 */
void PendSV_Handler(void) {
	/* USER CODE BEGIN PendSV_IRQn 0 */

	/* USER CODE END PendSV_IRQn 0 */
	/* USER CODE BEGIN PendSV_IRQn 1 */

	/* USER CODE END PendSV_IRQn 1 */
}

/**
 * @brief This function handles System tick timer.
 */
void SysTick_Handler(void) {
	/* USER CODE BEGIN SysTick_IRQn 0 */
	encoder_irq();

	/* USER CODE END SysTick_IRQn 0 */
	HAL_IncTick();
	/* USER CODE BEGIN SysTick_IRQn 1 */

	/* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32L4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32l4xx.s).                    */
/******************************************************************************/

/**
 * @brief This function handles EXTI line2 interrupt.
 */
void EXTI2_IRQHandler(void) {
	/* USER CODE BEGIN EXTI2_IRQn 0 */
	trigger_cv_clock();
	/* USER CODE END EXTI2_IRQn 0 */
	HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
	/* USER CODE BEGIN EXTI2_IRQn 1 */

	/* USER CODE END EXTI2_IRQn 1 */
}

/**
 * @brief This function handles DMA1 channel1 global interrupt.
 */
void DMA1_Channel1_IRQHandler(void) {
	/* USER CODE BEGIN DMA1_Channel1_IRQn 0 */

	/* USER CODE END DMA1_Channel1_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_adc1);
	/* USER CODE BEGIN DMA1_Channel1_IRQn 1 */

	/* USER CODE END DMA1_Channel1_IRQn 1 */
}

/**
 * @brief This function handles DMA1 channel2 global interrupt.
 */
void DMA1_Channel2_IRQHandler(void) {
	/* USER CODE BEGIN DMA1_Channel2_IRQn 0 */

	/* USER CODE END DMA1_Channel2_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_usart3_tx);
	/* USER CODE BEGIN DMA1_Channel2_IRQn 1 */

	/* USER CODE END DMA1_Channel2_IRQn 1 */
}

/**
 * @brief This function handles DMA1 channel3 global interrupt.
 */
void DMA1_Channel3_IRQHandler(void) {
	/* USER CODE BEGIN DMA1_Channel3_IRQn 0 */

	/* USER CODE END DMA1_Channel3_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_usart3_rx);
	/* USER CODE BEGIN DMA1_Channel3_IRQn 1 */

	/* USER CODE END DMA1_Channel3_IRQn 1 */
}

/**
 * @brief This function handles DMA1 channel4 global interrupt.
 */
#include "hardware/spi.h"
void DMA1_Channel4_IRQHandler(void) {
	/* USER CODE BEGIN DMA1_Channel4_IRQn 0 */
	if (alex_dma_mode) {
		alex_dma_done();
		return;
	}
	/* USER CODE END DMA1_Channel4_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_spi2_rx);
	/* USER CODE BEGIN DMA1_Channel4_IRQn 1 */

	/* USER CODE END DMA1_Channel4_IRQn 1 */
}

/**
 * @brief This function handles DMA1 channel5 global interrupt.
 */
void DMA1_Channel5_IRQHandler(void) {
	/* USER CODE BEGIN DMA1_Channel5_IRQn 0 */

	/* USER CODE END DMA1_Channel5_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_spi2_tx);
	/* USER CODE BEGIN DMA1_Channel5_IRQn 1 */

	/* USER CODE END DMA1_Channel5_IRQn 1 */
}

/**
 * @brief This function handles I2C2 event interrupt.
 */
void I2C2_EV_IRQHandler(void) {
	/* USER CODE BEGIN I2C2_EV_IRQn 0 */

	/* USER CODE END I2C2_EV_IRQn 0 */
	HAL_I2C_EV_IRQHandler(&hi2c2);
	/* USER CODE BEGIN I2C2_EV_IRQn 1 */

	/* USER CODE END I2C2_EV_IRQn 1 */
}

/**
 * @brief This function handles USART3 global interrupt.
 */
void USART3_IRQHandler(void) {
	/* USER CODE BEGIN USART3_IRQn 0 */

	/* USER CODE END USART3_IRQn 0 */
	HAL_UART_IRQHandler(&huart3);
	/* USER CODE BEGIN USART3_IRQn 1 */

	/* USER CODE END USART3_IRQn 1 */
}

/**
 * @brief This function handles DMA2 channel1 global interrupt.
 */
void DMA2_Channel1_IRQHandler(void) {
	/* USER CODE BEGIN DMA2_Channel1_IRQn 0 */

	/* USER CODE END DMA2_Channel1_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_sai1_a);
	/* USER CODE BEGIN DMA2_Channel1_IRQn 1 */

	/* USER CODE END DMA2_Channel1_IRQn 1 */
}

/**
 * @brief This function handles DMA2 channel2 global interrupt.
 */
void DMA2_Channel2_IRQHandler(void) {
	/* USER CODE BEGIN DMA2_Channel2_IRQn 0 */

	/* USER CODE END DMA2_Channel2_IRQn 0 */
	HAL_DMA_IRQHandler(&hdma_sai1_b);
	/* USER CODE BEGIN DMA2_Channel2_IRQn 1 */

	/* USER CODE END DMA2_Channel2_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
