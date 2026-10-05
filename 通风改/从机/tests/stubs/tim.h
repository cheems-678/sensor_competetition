#ifndef TEST_SERVO_TIM_H
#define TEST_SERVO_TIM_H
#include <stdint.h>
/* Register layout is a host model; bit definitions match STM32F103 CMSIS. */
typedef struct { uint32_t CR1, DIER, PSC, ARR, CCMR2, CCER, CCR3, CCR4, CNT, SR; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t MAPR; } AFIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
extern TIM_TypeDef test_tim2, test_tim4;
extern AFIO_TypeDef test_afio;
extern TIM_HandleTypeDef htim2, htim4;
extern uint32_t SystemCoreClock;
#define TIM2 (&test_tim2)
#define TIM4 (&test_tim4)
#define AFIO (&test_afio)
#define GPIOB ((void *)1)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_RESET 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define TIM_CHANNEL_3 8U
#define TIM_CHANNEL_4 12U
#define TIM_CR1_CEN 1U
#define TIM_CR1_ARPE (1U << 7)
#define TIM_CCER_CC1E (1U << 0)
#define TIM_CCER_CC2E (1U << 4)
#define TIM_CCER_CC3E (1U << 8)
#define TIM_CCER_CC3P (1U << 9)
#define TIM_CCER_CC4E (1U << 12)
#define TIM_CCER_CC4P (1U << 13)
#define TIM_CCMR2_CC3S 3U
#define TIM_CCMR2_OC3PE (1U << 3)
#define TIM_CCMR2_OC3M (7U << 4)
#define TIM_CCMR2_OC3M_1 (1U << 5)
#define TIM_CCMR2_OC3M_2 (1U << 6)
#define TIM_CCMR2_CC4S (3U << 8)
#define TIM_CCMR2_OC4PE (1U << 11)
#define TIM_CCMR2_OC4M (7U << 12)
#define TIM_CCMR2_OC4M_1 (1U << 13)
#define TIM_CCMR2_OC4M_2 (1U << 14)
#define AFIO_MAPR_TIM4_REMAP (1U << 12)
#define AFIO_MAPR_TIM2_REMAP (3U << 8)
#define AFIO_MAPR_TIM2_REMAP_PARTIALREMAP2 (2U << 8)
#define TIM_EVENTSOURCE_UPDATE 1U
#define TIM_FLAG_UPDATE 1U
#define HAL_OK 0
#define HAL_ERROR 1
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_COMPARE(h,ch,value) do { if ((ch)==TIM_CHANNEL_3) (h)->Instance->CCR3=(value); else (h)->Instance->CCR4=(value); } while (0)
#define __HAL_TIM_SET_COUNTER(h,value) ((h)->Instance->CNT=(value))
#define __HAL_TIM_CLEAR_FLAG(h,value) ((h)->Instance->SR &= ~(value))
uint32_t HAL_RCC_GetPCLK1Freq(void);
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
int HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
int HAL_TIM_GenerateEvent(TIM_HandleTypeDef *, uint32_t);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
void HAL_GPIO_WritePin(void *, uint16_t, uint32_t);
#endif
