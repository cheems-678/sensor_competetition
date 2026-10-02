/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
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
#include "i2c.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

I2C_HandleTypeDef hi2c1;
static HAL_StatusTypeDef g_i2c_init_status;

/* I2C1 init function */
void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  g_i2c_init_status = HAL_I2C_Init(&hi2c1);
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspInit 0 */

  /* USER CODE END I2C1_MspInit 0 */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* I2C1 clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();
  /* USER CODE BEGIN I2C1_MspInit 1 */
//  /*Reset I2C****************在设备初始化之前便添加此操作************************/
//  i2cHandle->Instance->CR1 |= 0x8000;
//  i2cHandle->Instance->CR1 &= ~0x8000;
  /* USER CODE END I2C1_MspInit 1 */
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspDeInit 0 */

  /* USER CODE END I2C1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);

  /* USER CODE BEGIN I2C1_MspDeInit 1 */
  /* USER CODE END I2C1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

static HAL_StatusTypeDef SlaveI2c_WaitPin(uint16_t pin, GPIO_PinState state)
{
  uint32_t start = HAL_GetTick();
  while (HAL_GPIO_ReadPin(GPIOB, pin) != state)
  {
    if ((uint32_t)(HAL_GetTick() - start) >= 2U) { return HAL_TIMEOUT; }
  }
  return HAL_OK;
}

HAL_StatusTypeDef SlaveI2c_InitAndRecover(void)
{
  GPIO_InitTypeDef pins = {0};
  HAL_StatusTypeDef result = HAL_OK;
  uint8_t pulse;

  if (hi2c1.Instance == I2C1) { (void)HAL_I2C_DeInit(&hi2c1); }
  MX_I2C1_Init();
  if (g_i2c_init_status != HAL_OK) { return g_i2c_init_status; }
  if (((I2C1->SR2 & I2C_SR2_BUSY) == 0U) &&
      (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) &&
      (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET)) { return HAL_OK; }

  CLEAR_BIT(I2C1->CR1, I2C_CR1_PE);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
  pins.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  pins.Mode = GPIO_MODE_OUTPUT_OD;
  pins.Pull = GPIO_NOPULL;
  pins.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &pins);

  /* Release a slave left in the middle of a byte. Never actively drive high. */
  for (pulse = 0U; pulse < 9U; pulse++)
  {
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) { break; }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    if (SlaveI2c_WaitPin(GPIO_PIN_6, GPIO_PIN_SET) != HAL_OK)
    { result = HAL_TIMEOUT; break; }
    HAL_Delay(1U);
  }
  /* ST ES096 section 2.8.7: SCL/SDA transitions BEFORE software reset. */
  if ((SlaveI2c_WaitPin(GPIO_PIN_6, GPIO_PIN_SET) != HAL_OK) ||
      (SlaveI2c_WaitPin(GPIO_PIN_7, GPIO_PIN_SET) != HAL_OK)) { result = HAL_TIMEOUT; }
  if (result == HAL_OK)
  {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    result = SlaveI2c_WaitPin(GPIO_PIN_7, GPIO_PIN_RESET);
    if (result == HAL_OK)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
      result = SlaveI2c_WaitPin(GPIO_PIN_6, GPIO_PIN_RESET);
    }
    if (result == HAL_OK)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
      result = SlaveI2c_WaitPin(GPIO_PIN_6, GPIO_PIN_SET);
    }
    if (result == HAL_OK)
    {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
      result = SlaveI2c_WaitPin(GPIO_PIN_7, GPIO_PIN_SET);
    }
  }
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
  pins.Mode = GPIO_MODE_AF_OD;
  HAL_GPIO_Init(GPIOB, &pins);
  SET_BIT(I2C1->CR1, I2C_CR1_SWRST);
  __DSB();
  CLEAR_BIT(I2C1->CR1, I2C_CR1_SWRST);
  g_i2c_init_status = HAL_I2C_Init(&hi2c1);
  if (result != HAL_OK) { return result; }
  if (g_i2c_init_status != HAL_OK) { return g_i2c_init_status; }
  return ((I2C1->SR2 & I2C_SR2_BUSY) != 0U) ? HAL_BUSY : HAL_OK;
}

/* USER CODE END 1 */
