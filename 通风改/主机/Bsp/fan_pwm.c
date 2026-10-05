#include "fan_pwm.h"

#include "stm32f1xx_hal.h"

/* APB1 timers run at 72 MHz in SystemClock_Config: 72 MHz / 2880 = 25 kHz. */
#define FAN_PWM_PERIOD_TICKS (2880U)

static uint8_t g_duty[FAN_PWM_CHANNEL_COUNT];

static void FanPwm_InitTimer(TIM_TypeDef *timer)
{
    timer->CR1 = TIM_CR1_ARPE;
    timer->PSC = 0U;
    timer->ARR = FAN_PWM_PERIOD_TICKS - 1U;
}

static uint32_t FanPwm_Compare(uint8_t duty_percent)
{
    if (duty_percent >= 100U)
    {
        return FAN_PWM_PERIOD_TICKS;
    }
    return ((uint32_t)FAN_PWM_PERIOD_TICKS * duty_percent + 50U) / 100U;
}

void FanPwm_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t channel;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    FanPwm_InitTimer(TIM2);
    FanPwm_InitTimer(TIM3);
    FanPwm_InitTimer(TIM4);

    TIM2->CCMR1 = (6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM2->CCER = TIM_CCER_CC2E;
    TIM2->CCR2 = 0U;

    TIM3->CCMR2 = (6U << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM3->CCER = TIM_CCER_CC4E;
    TIM3->CCR4 = 0U;

    TIM4->CCMR2 = (6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (6U << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM4->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM4->CCR3 = 0U;
    TIM4->CCR4 = 0U;

    TIM2->EGR = TIM_EGR_UG;
    TIM3->EGR = TIM_EGR_UG;
    TIM4->EGR = TIM_EGR_UG;

    /* Apply zero compares before connecting the pins to the PWM outputs. */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_1;
    HAL_GPIO_Init(GPIOA, &gpio);

    TIM2->CR1 |= TIM_CR1_CEN;
    TIM3->CR1 |= TIM_CR1_CEN;
    TIM4->CR1 |= TIM_CR1_CEN;

    for (channel = 0U; channel < FAN_PWM_CHANNEL_COUNT; channel++)
    {
        g_duty[channel] = 0U;
    }
}

uint8_t FanPwm_SetDuty(uint8_t channel, uint8_t duty_percent)
{
    uint32_t compare;

    if ((channel < 1U) || (channel > FAN_PWM_CHANNEL_COUNT) || (duty_percent > 100U))
    {
        return 0U;
    }

    compare = FanPwm_Compare(duty_percent);
    if (channel == 1U)
    {
        TIM3->CCR4 = compare; /* Fan 1: PB1 */
    }
    else if (channel == 2U)
    {
        TIM4->CCR3 = compare; /* Fan 2: PB8 */
    }
    else if (channel == 3U)
    {
        TIM2->CCR2 = compare; /* Fan 3: PA1 */
    }
    else
    {
        TIM4->CCR4 = compare; /* Fan 4: PB9 */
    }
    g_duty[channel - 1U] = duty_percent;
    return 1U;
}

uint8_t FanPwm_GetDuty(uint8_t channel)
{
    return ((channel >= 1U) && (channel <= FAN_PWM_CHANNEL_COUNT)) ?
           g_duty[channel - 1U] : 0U;
}
