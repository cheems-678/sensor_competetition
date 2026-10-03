#ifndef TEST_STM32F1XX_HAL_H
#define TEST_STM32F1XX_HAL_H

#include <stdint.h>

uint32_t HAL_GetTick(void);
typedef struct { unsigned instance; } UART_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2 } HAL_StatusTypeDef;
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t length, uint32_t timeout);
void HAL_Delay(uint32_t delay);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);

typedef struct { unsigned instance; } GPIO_TypeDef;
typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

extern GPIO_TypeDef TestGpioA;
#define GPIOA                       (&TestGpioA)
#define GPIO_PIN_7                  (0x0080U)
#define GPIO_MODE_INPUT             (0x00000000U)
#define GPIO_NOPULL                 (0x00000000U)
#define GPIO_SPEED_FREQ_LOW         (0x00000002U)
#define __HAL_RCC_GPIOA_CLK_ENABLE() TestGpioAClockEnable()

void TestGpioAClockEnable(void);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

#endif
