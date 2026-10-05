#include "sg90_test_pwm.h"

#include "tim.h"

typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t channel;
    uint16_t pin;
} ServoOutput;

static const ServoOutput outputs[SG90_SERVO_COUNT] = {
    {&htim4, TIM_CHANNEL_3, GPIO_PIN_8},
    {&htim4, TIM_CHANNEL_4, GPIO_PIN_9},
    {&htim2, TIM_CHANNEL_3, GPIO_PIN_10},
    {&htim2, TIM_CHANNEL_4, GPIO_PIN_11}
};
static uint8_t running[SG90_SERVO_COUNT];

static uint8_t ValidId(uint8_t servo_id)
{
    return (uint8_t)((servo_id >= 1U) && (servo_id <= SG90_SERVO_COUNT));
}

static uint32_t EnableBit(const ServoOutput *output)
{
    return (output->channel == TIM_CHANNEL_3) ? TIM_CCER_CC3E : TIM_CCER_CC4E;
}

static uint8_t ConfigValid(const ServoOutput *output)
{
    TIM_TypeDef *timer = output->timer->Instance;
    uint32_t mask, mode;
    if ((timer != TIM4) && (timer != TIM2))
    {
        return 0U;
    }
    if (output->channel == TIM_CHANNEL_3)
    {
        mask = TIM_CCMR2_CC3S | TIM_CCMR2_OC3M | TIM_CCMR2_OC3PE;
        mode = TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3PE;
    }
    else
    {
        mask = TIM_CCMR2_CC4S | TIM_CCMR2_OC4M | TIM_CCMR2_OC4PE;
        mode = TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4PE;
    }
    return (uint8_t)((SystemCoreClock == 72000000U) &&
                    (HAL_RCC_GetPCLK1Freq() == 36000000U) &&
                    ((timer == TIM4) ?
                     ((AFIO->MAPR & AFIO_MAPR_TIM4_REMAP) == 0U) :
                     ((AFIO->MAPR & AFIO_MAPR_TIM2_REMAP) ==
                      AFIO_MAPR_TIM2_REMAP_PARTIALREMAP2)) &&
                    (timer->PSC == 71U) && (timer->ARR == 19999U) &&
                    ((timer->CR1 & TIM_CR1_ARPE) != 0U) &&
                    ((timer->CCMR2 & mask) == mode) &&
                    ((timer->CCER & (TIM_CCER_CC3P | TIM_CCER_CC4P |
                                    TIM_CCER_CC1E | TIM_CCER_CC2E)) == 0U) &&
                    (timer->DIER == 0U));
}

void Sg90TestPwm_StopChannel(uint8_t servo_id)
{
    GPIO_InitTypeDef pin = {0};
    const ServoOutput *output;
    if (ValidId(servo_id) == 0U)
    {
        return;
    }
    output = &outputs[servo_id - 1U];
    running[servo_id - 1U] = 0U;
    if ((output->timer->Instance == TIM4) || (output->timer->Instance == TIM2))
    {
        /* HAL only disables the counter when no other channel remains enabled. */
        (void)HAL_TIM_PWM_Stop(output->timer, output->channel);
        __HAL_TIM_SET_COMPARE(output->timer, output->channel, 0U);
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, output->pin, GPIO_PIN_RESET);
    pin.Pin = output->pin;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);
}

static void StopGroup(const ServoOutput *output)
{
    uint8_t id;
    for (id = 1U; id <= SG90_SERVO_COUNT; id++)
    {
        if (outputs[id - 1U].timer == output->timer)
        {
            Sg90TestPwm_StopChannel(id);
        }
    }
}

uint8_t Sg90TestPwm_StartChannel(uint8_t servo_id, uint16_t pulse_us)
{
    GPIO_InitTypeDef pin = {0};
    const ServoOutput *output;
    TIM_TypeDef *timer;
    uint8_t id;
    uint32_t owned = 0U;
    if (ValidId(servo_id) == 0U)
    {
        return 0U;
    }
    output = &outputs[servo_id - 1U];
    if ((pulse_us < SG90_TEST_MIN_US) || (pulse_us > SG90_TEST_MAX_US))
    {
        Sg90TestPwm_StopChannel(servo_id);
        return 0U;
    }
    if (ConfigValid(output) == 0U)
    {
        StopGroup(output);
        return 0U;
    }
    timer = output->timer->Instance;
    for (id = 1U; id <= SG90_SERVO_COUNT; id++)
    {
        if ((outputs[id - 1U].timer == output->timer) && (running[id - 1U] != 0U))
        {
            owned |= EnableBit(&outputs[id - 1U]);
        }
    }
    if (((timer->CCER & (TIM_CCER_CC3E | TIM_CCER_CC4E)) != owned) ||
        (((timer->CR1 & TIM_CR1_CEN) != 0U) != (owned != 0U)))
    {
        StopGroup(output);
        return 0U;
    }
    if (running[servo_id - 1U] != 0U)
    {
        return Sg90TestPwm_SetChannelPulse(servo_id, pulse_us);
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    pin.Pin = output->pin;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);
    __HAL_TIM_SET_COMPARE(output->timer, output->channel, 0U);
    if (owned == 0U)
    {
        /* Only the first channel may latch zero and reset the shared timer. */
        if (HAL_TIM_GenerateEvent(output->timer, TIM_EVENTSOURCE_UPDATE) != HAL_OK)
        {
            StopGroup(output);
            return 0U;
        }
        __HAL_TIM_SET_COUNTER(output->timer, 0U);
        __HAL_TIM_CLEAR_FLAG(output->timer, TIM_FLAG_UPDATE);
    }
    if (HAL_TIM_PWM_Start(output->timer, output->channel) != HAL_OK)
    {
        Sg90TestPwm_StopChannel(servo_id);
        return 0U;
    }
    __HAL_TIM_SET_COMPARE(output->timer, output->channel, pulse_us);
    running[servo_id - 1U] = 1U;
    return 1U;
}

uint8_t Sg90TestPwm_IsChannelRunning(uint8_t servo_id)
{
    const ServoOutput *output;
    if ((ValidId(servo_id) == 0U) || (running[servo_id - 1U] == 0U))
    {
        return 0U;
    }
    output = &outputs[servo_id - 1U];
    return (uint8_t)((ConfigValid(output) != 0U) &&
                    ((output->timer->Instance->CR1 & TIM_CR1_CEN) != 0U) &&
                    ((output->timer->Instance->CCER & EnableBit(output)) != 0U));
}

uint8_t Sg90TestPwm_SetChannelPulse(uint8_t servo_id, uint16_t pulse_us)
{
    if (ValidId(servo_id) == 0U)
    {
        return 0U;
    }
    if (ConfigValid(&outputs[servo_id - 1U]) == 0U)
    {
        StopGroup(&outputs[servo_id - 1U]);
        return 0U;
    }
    if ((pulse_us < SG90_TEST_MIN_US) || (pulse_us > SG90_TEST_MAX_US) ||
        (Sg90TestPwm_IsChannelRunning(servo_id) == 0U))
    {
        Sg90TestPwm_StopChannel(servo_id);
        return 0U;
    }
    /* CCR preload changes this channel at the next complete period boundary. */
    __HAL_TIM_SET_COMPARE(outputs[servo_id - 1U].timer,
                          outputs[servo_id - 1U].channel, pulse_us);
    return 1U;
}

/* Historical single-servo interfaces remain bound to PB8. */
uint8_t Sg90TestPwm_Start(uint16_t pulse_us)
{
    return Sg90TestPwm_StartChannel(1U, pulse_us);
}
uint8_t Sg90TestPwm_SetPulse(uint16_t pulse_us)
{
    return Sg90TestPwm_SetChannelPulse(1U, pulse_us);
}
void Sg90TestPwm_Stop(void)
{
    Sg90TestPwm_StopChannel(1U);
}
