#ifndef TEST_ESP_USART_H
#define TEST_ESP_USART_H
#include "stm32f1xx_hal.h"
typedef struct { unsigned instance; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart1;
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
#define __DMB() ((void)0)
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *handle, uint8_t *data, uint16_t size);
#endif
