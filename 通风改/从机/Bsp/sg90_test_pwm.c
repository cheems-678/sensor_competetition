#include "sg90_test_pwm.h"

#include "tim.h"

static uint8_t running;

static uint8_t Sg90TestPwm_ConfigValid(void)
{
    return (uint8_t)((htim4.Instance == TIM4) &&
                    (SystemCoreClock == 72000000U) &&
                    (HAL_RCC_GetPCLK1Freq() == 36000000U) &&
                    ((AFIO->MAPR & AFIO_MAPR_TIM4_REMAP) == 0U) &&
                    (TIM4->PSC == 71U) && (TIM4->ARR == 19999U) &&
                    ((TIM4->CR1 & TIM_CR1_ARPE) != 0U) &&
                    ((TIM4->CCMR2 & (TIM_CCMR2_CC3S | TIM_CCMR2_OC3M |
                                     TIM_CCMR2_OC3PE)) ==
                     (TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3PE)) &&
                    ((TIM4->CCER & TIM_CCER_CC3P) == 0U) &&
                    (TIM4->DIER == 0U));
}

void Sg90TestPwm_Stop(void)
{
    GPIO_InitTypeDef pin = {0};

    running = 0U;
    if (htim4.Instance == TIM4)
    {
        (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3);
        __HAL_TIM_DISABLE_IT(&htim4, TIM_IT_UPDATE);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    pin.Pin = GPIO_PIN_8;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);
}

uint8_t Sg90TestPwm_Start(uint16_t pulse_us)
{
    GPIO_InitTypeDef pin = {0};

    if ((pulse_us < SG90_TEST_MIN_US) || (pulse_us > SG90_TEST_MAX_US) ||
        (Sg90TestPwm_ConfigValid() == 0U) ||
        ((TIM4->CR1 & TIM_CR1_CEN) != 0U) ||
        ((TIM4->CCER & (TIM_CCER_CC1E | TIM_CCER_CC2E |
                        TIM_CCER_CC3E | TIM_CCER_CC4E)) != 0U))
    {
        Sg90TestPwm_Stop();
        return 0U;
    }

    pin.Pin = GPIO_PIN_8;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);

    /* Start low. The first requested pulse is latched at a full-period boundary. */
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    if (HAL_TIM_GenerateEvent(&htim4, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
    {
        Sg90TestPwm_Stop();
        return 0U;
    }
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim4, TIM_FLAG_UPDATE);
    if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3) != HAL_OK)
    {
        Sg90TestPwm_Stop();
        return 0U;
    }
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse_us);
    running = 1U;
    return 1U;
}

uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us)
{
    if ((running == 0U) || (pulse_us < SG90_TEST_MIN_US) ||
        (pulse_us > SG90_TEST_MAX_US) || (Sg90TestPwm_ConfigValid() == 0U) ||
        ((TIM4->CR1 & TIM_CR1_CEN) == 0U) ||
        ((TIM4->CCER & TIM_CCER_CC3E) == 0U))
    {
        Sg90TestPwm_Stop();
        return 0U;
    }
    /* OC3 preload is enabled: never reset CNT or force an update while moving. */
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse_us);
    return 1U;
}
