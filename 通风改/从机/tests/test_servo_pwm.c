#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tim.h"
#include "sg90_test_pwm.h"

TIM_TypeDef test_tim2, test_tim4;
AFIO_TypeDef test_afio;
TIM_HandleTypeDef htim2 = {&test_tim2}, htim4 = {&test_tim4};
uint32_t SystemCoreClock = 72000000U;
static unsigned events;
static uint32_t gpio_modes[4];
static uint16_t gpio_low;
static uint8_t fail_start;
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000U; }
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t ch)
{
    if (fail_start) { return HAL_ERROR; }
    h->Instance->CCER |= (ch == TIM_CHANNEL_3) ? TIM_CCER_CC3E : TIM_CCER_CC4E;
    h->Instance->CR1 |= TIM_CR1_CEN;
    return HAL_OK;
}
int HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t ch)
{
    h->Instance->CCER &= ~((ch == TIM_CHANNEL_3) ? TIM_CCER_CC3E : TIM_CCER_CC4E);
    if (!(h->Instance->CCER & (TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E)))
        h->Instance->CR1 &= ~TIM_CR1_CEN;
    return HAL_OK;
}
int HAL_TIM_GenerateEvent(TIM_HandleTypeDef *h, uint32_t event)
{ (void)h; assert(event == TIM_EVENTSOURCE_UPDATE); events++; return HAL_OK; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pin)
{
    unsigned i; assert(port == GPIOB);
    for (i = 0; i < 4; i++) if (pin->Pin & (1U << (8U + i))) gpio_modes[i] = pin->Mode;
}
void HAL_GPIO_WritePin(void *port, uint16_t pin, uint32_t state)
{ assert(port == GPIOB && state == GPIO_PIN_RESET); gpio_low |= pin; }

static void reset(void)
{
    uint8_t id;
    for (id = 1; id <= 4; id++) Sg90TestPwm_StopChannel(id);
    memset(&test_tim2, 0, sizeof(test_tim2)); memset(&test_tim4, 0, sizeof(test_tim4));
    test_tim2.PSC = test_tim4.PSC = 71U;
    test_tim2.ARR = test_tim4.ARR = 19999U;
    test_tim2.CR1 = test_tim4.CR1 = TIM_CR1_ARPE;
    test_tim2.CCMR2 = test_tim4.CCMR2 = TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2 |
        TIM_CCMR2_OC3PE | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4PE;
    test_afio.MAPR = AFIO_MAPR_TIM2_REMAP_PARTIALREMAP2;
    events = 0; gpio_low = 0; fail_start = 0;
}
int main(void)
{
    reset();
    assert(Sg90TestPwm_Start(1500U));
    test_tim4.CNT = 777U;
    assert(Sg90TestPwm_StartChannel(2U, 1300U));
    assert(test_tim4.CNT == 777U && events == 1U && test_tim4.CCR3 == 1500U);
    assert(Sg90TestPwm_StartChannel(3U, 1700U));
    test_tim2.CNT = 999U;
    assert(Sg90TestPwm_StartChannel(4U, 1500U));
    assert(test_tim2.CNT == 999U && events == 2U);
    assert(test_tim4.CCR4 == 1300U && test_tim2.CCR3 == 1700U && test_tim2.CCR4 == 1500U);
    assert(Sg90TestPwm_SetChannelPulse(2U, 1700U));
    assert(test_tim4.CCR3 == 1500U && test_tim4.CCR4 == 1700U && test_tim4.CNT == 777U);
    Sg90TestPwm_StopChannel(2U);
    assert(Sg90TestPwm_IsChannelRunning(1U) && test_tim4.CNT == 777U);
    assert(gpio_modes[1] == GPIO_MODE_OUTPUT_PP && (gpio_low & GPIO_PIN_9));
    fail_start = 1U;
    assert(!Sg90TestPwm_StartChannel(2U, 1500U));
    assert(Sg90TestPwm_IsChannelRunning(1U));
    fail_start = 0U;
    assert(Sg90TestPwm_StartChannel(2U, 1500U) && events == 2U);
    assert(!Sg90TestPwm_SetChannelPulse(4U, 1200U));
    assert(Sg90TestPwm_IsChannelRunning(3U) && !Sg90TestPwm_IsChannelRunning(4U));
    test_tim4.ARR = 1234U;
    assert(!Sg90TestPwm_SetPulse(1500U));
    assert(!(test_tim4.CCER & (TIM_CCER_CC3E | TIM_CCER_CC4E)));
    assert(Sg90TestPwm_IsChannelRunning(3U));
    reset(); test_afio.MAPR = 0U;
    assert(!Sg90TestPwm_StartChannel(3U, 1500U));
    assert(Sg90TestPwm_Start(1500U));
    assert(!Sg90TestPwm_StartChannel(0U, 1500U));
    assert(!Sg90TestPwm_StartChannel(5U, 1500U));
    assert(Sg90TestPwm_IsChannelRunning(1U));
    puts("PASS real four-channel PWM driver: CCR isolation, counter continuity, group/individual faults");
    return 0;
}
