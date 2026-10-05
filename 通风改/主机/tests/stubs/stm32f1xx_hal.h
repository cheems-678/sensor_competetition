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
extern GPIO_TypeDef TestGpioB;
#define GPIOA                       (&TestGpioA)
#define GPIOB                       (&TestGpioB)
#define GPIO_PIN_1                  (0x0002U)
#define GPIO_PIN_7                  (0x0080U)
#define GPIO_PIN_8                  (0x0100U)
#define GPIO_PIN_9                  (0x0200U)
#define GPIO_PIN_11                 (0x0800U)
#define GPIO_MODE_INPUT             (0x00000000U)
#define GPIO_MODE_AF_PP             (0x00000002U)
#define GPIO_NOPULL                 (0x00000000U)
#define GPIO_PULLUP                 (0x00000001U)
#define GPIO_SPEED_FREQ_LOW         (0x00000002U)
#define GPIO_SPEED_FREQ_HIGH        (0x00000003U)
#define __HAL_RCC_GPIOA_CLK_ENABLE() TestGpioAClockEnable()
#define __HAL_RCC_GPIOB_CLK_ENABLE() TestGpioBClockEnable()

typedef struct
{
    volatile uint32_t CR1, PSC, ARR, CCMR1, CCMR2, CCER;
    volatile uint32_t CCR2, CCR3, CCR4, EGR, CNT;
} TIM_TypeDef;
extern TIM_TypeDef TestTim2, TestTim3, TestTim4;
#define TIM2 (&TestTim2)
#define TIM3 (&TestTim3)
#define TIM4 (&TestTim4)
#define TIM_CR1_ARPE (1U << 7)
#define TIM_CR1_CEN (1U << 0)
#define TIM_CCMR1_OC2M_Pos (12U)
#define TIM_CCMR1_OC2PE (1U << 11)
#define TIM_CCMR2_OC3M_Pos (4U)
#define TIM_CCMR2_OC3PE (1U << 3)
#define TIM_CCMR2_OC4M_Pos (12U)
#define TIM_CCMR2_OC4PE (1U << 11)
#define TIM_CCER_CC2E (1U << 4)
#define TIM_CCER_CC3E (1U << 8)
#define TIM_CCER_CC4E (1U << 12)
#define TIM_EGR_UG (1U << 0)
#define __HAL_RCC_TIM2_CLK_ENABLE() TestTimerClockEnable(2U)
#define __HAL_RCC_TIM3_CLK_ENABLE() TestTimerClockEnable(3U)
#define __HAL_RCC_TIM4_CLK_ENABLE() TestTimerClockEnable(4U)

void TestGpioAClockEnable(void);
void TestGpioBClockEnable(void);
void TestTimerClockEnable(unsigned timer);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

#endif
