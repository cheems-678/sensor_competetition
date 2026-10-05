#include <assert.h>
#include <stdio.h>
#include "stm32f1xx_hal.h"
#include "fan_pwm.h"

GPIO_TypeDef TestGpioA, TestGpioB;
TIM_TypeDef TestTim2, TestTim3, TestTim4;
static unsigned gpio_clocks, timer_clocks, gpio_calls;

void TestGpioAClockEnable(void) { gpio_clocks |= 1U; }
void TestGpioBClockEnable(void) { gpio_clocks |= 2U; }
void TestTimerClockEnable(unsigned timer) { timer_clocks |= 1U << timer; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    assert(gpio_clocks == 3U && timer_clocks == 28U);
    assert(config->Mode == GPIO_MODE_AF_PP && config->Pull == GPIO_NOPULL);
    assert(config->Speed == GPIO_SPEED_FREQ_HIGH);
    assert(TIM2->CCR2 == 0U && TIM3->CCR4 == 0U);
    assert(TIM4->CCR3 == 0U && TIM4->CCR4 == 0U);
    assert((TIM2->CR1 & TIM_CR1_CEN) == 0U);
    assert((TIM3->CR1 & TIM_CR1_CEN) == 0U);
    assert((TIM4->CR1 & TIM_CR1_CEN) == 0U);
    if (port == GPIOA) { assert(config->Pin == GPIO_PIN_1); }
    else { assert(port == GPIOB && config->Pin == (GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9)); }
    gpio_calls++;
}

int main(void)
{
    volatile uint32_t *compares[] = {&TIM3->CCR4, &TIM4->CCR3, &TIM2->CCR2, &TIM4->CCR4};
    TIM_TypeDef *timers[] = {TIM2, TIM3, TIM4};
    unsigned channel, duty, other;
    uint32_t before[4];

    FanPwm_Init();
    assert(gpio_calls == 2U);
    for (other = 0U; other < 3U; other++)
    {
        assert(timers[other]->PSC == 0U && timers[other]->ARR == 2879U);
        assert(timers[other]->CR1 == (TIM_CR1_ARPE | TIM_CR1_CEN));
        assert(timers[other]->EGR == TIM_EGR_UG);
        timers[other]->CNT = 123U + other;
        timers[other]->EGR = 0U;
    }
    assert(TIM2->CCER == 0x10U && TIM2->CCMR1 == 0x6800U);
    assert(TIM3->CCER == 0x1000U && TIM3->CCMR2 == 0x6800U);
    assert(TIM4->CCER == 0x1100U && TIM4->CCMR2 == 0x6868U);
    for (channel = 0U; channel < 4U; channel++)
    {
        assert(FanPwm_GetDuty((uint8_t)(channel + 1U)) == 0U);
        assert(*compares[channel] == 0U);
    }
    for (channel = 0U; channel < 4U; channel++)
    {
        for (duty = 0U; duty <= 100U; duty++)
        {
            for (other = 0U; other < 4U; other++) { before[other] = *compares[other]; }
            assert(FanPwm_SetDuty((uint8_t)(channel + 1U), (uint8_t)duty) == 1U);
            assert(*compares[channel] == (2880U * duty + 50U) / 100U);
            assert(FanPwm_GetDuty((uint8_t)(channel + 1U)) == duty);
            for (other = 0U; other < 4U; other++)
            { if (other != channel) { assert(*compares[other] == before[other]); } }
        }
    }
    for (channel = 0U; channel <= 255U; channel++)
    {
        if (channel < 1U || channel > 4U)
        {
            assert(FanPwm_SetDuty((uint8_t)channel, 50U) == 0U);
            assert(FanPwm_GetDuty((uint8_t)channel) == 0U);
        }
    }
    for (channel = 1U; channel <= 4U; channel++)
    {
        for (duty = 101U; duty <= 255U; duty++)
        { assert(FanPwm_SetDuty((uint8_t)channel, (uint8_t)duty) == 0U); }
        assert(*compares[channel - 1U] == 2880U);
        assert(FanPwm_GetDuty((uint8_t)channel) == 100U);
    }
    for (other = 0U; other < 3U; other++)
    {
        assert(timers[other]->CNT == 123U + other && timers[other]->EGR == 0U);
        assert(timers[other]->PSC == 0U && timers[other]->ARR == 2879U);
    }
    FanPwm_Init();
    for (channel = 0U; channel < 4U; channel++)
    { assert(*compares[channel] == 0U && FanPwm_GetDuty((uint8_t)(channel + 1U)) == 0U); }
    puts("Four fan PWM mapping, startup, independent duty and invalid-input checks passed");
    return 0;
}
