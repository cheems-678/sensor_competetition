#ifndef TEST_HCSR04_TIMER_PLATFORM_H
#define TEST_HCSR04_TIMER_PLATFORM_H
#include <stdint.h>
typedef struct { uint32_t CR1, DIER, PSC, ARR, CCMR1, CCMR2, CCER, CNT, SR, EGR, CCR1, CCR2, CCR3, CCR4; } TestHcsr04Timer;
typedef struct { uint32_t CFGR; } TestHcsr04Rcc;
typedef struct { uint32_t MAPR; } TestHcsr04Afio;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
extern TestHcsr04Timer test_hcsr04_timer;
extern TestHcsr04Rcc test_hcsr04_rcc;
extern TestHcsr04Afio test_hcsr04_afio;
#define TIM3 (&test_hcsr04_timer)
#define RCC (&test_hcsr04_rcc)
#define AFIO (&test_hcsr04_afio)
#define GPIOB ((void *)1)
#define GPIO_PIN_0 1U
#define GPIO_PIN_1 2U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_INPUT 2U
#define GPIO_NOPULL 0U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_LOW 0U
#define TIM3_IRQn 29
#define RCC_CFGR_PPRE1 (7U << 8)
#define AFIO_MAPR_TIM3_REMAP (3U << 10)
#define AFIO_MAPR_TIM3_REMAP_FULLREMAP (3U << 10)
#define TIM_CR1_CEN 1U
#define TIM_EGR_UG 1U
#define TIM_CCMR2_CC3S_0 1U
#define TIM_CCMR2_CC4S_1 (2U << 8)
#define TIM_CCER_CC3E (1U << 8)
#define TIM_CCER_CC4E (1U << 12)
#define TIM_CCER_CC4P (1U << 13)
#define TIM_DIER_CC1IE (1U << 1)
#define TIM_DIER_CC2IE (1U << 2)
#define TIM_DIER_CC3IE (1U << 3)
#define TIM_DIER_CC4IE (1U << 4)
#define TIM_SR_CC1IF (1U << 1)
#define TIM_SR_CC2IF (1U << 2)
#define TIM_SR_CC3IF (1U << 3)
#define TIM_SR_CC4IF (1U << 4)
#define TIM_SR_CC3OF (1U << 11)
#define TIM_SR_CC4OF (1U << 12)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_FORCE_RESET() ((void)0)
#define __HAL_RCC_TIM3_RELEASE_RESET() ((void)0)
#define __HAL_DBGMCU_FREEZE_TIM3() ((void)0)
uint32_t HAL_RCC_GetPCLK1Freq(void);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
void HAL_GPIO_WritePin(void *, uint16_t, uint32_t);
uint32_t HAL_GPIO_ReadPin(void *, uint16_t);
void HAL_NVIC_DisableIRQ(int);
void HAL_NVIC_EnableIRQ(int);
void HAL_NVIC_ClearPendingIRQ(int);
void HAL_NVIC_SetPriority(int, uint32_t, uint32_t);
#endif
