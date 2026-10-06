#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hcsr04_timer.h"
#include <hcsr04_timer_platform.h>

TestHcsr04Timer test_hcsr04_timer;
TestHcsr04Rcc test_hcsr04_rcc;
TestHcsr04Afio test_hcsr04_afio;
static uint32_t pclk, trig, echo, gpio_count, irq_enabled;
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pin)
{
    assert(port == GPIOB);
    if (pin->Pin == GPIO_PIN_0) { assert(pin->Mode == GPIO_MODE_INPUT && pin->Pull == GPIO_PULLDOWN); }
    else { assert(pin->Pin == GPIO_PIN_1 && pin->Mode == GPIO_MODE_OUTPUT_PP); }
    gpio_count++;
}
void HAL_GPIO_WritePin(void *port, uint16_t pin, uint32_t state)
{ assert(port == GPIOB && pin == GPIO_PIN_1); trig = state; }
uint32_t HAL_GPIO_ReadPin(void *port, uint16_t pin)
{ assert(port == GPIOB && pin == GPIO_PIN_0); return echo; }
void HAL_NVIC_DisableIRQ(int irq) { assert(irq == TIM3_IRQn); irq_enabled = 0U; }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq == TIM3_IRQn); irq_enabled = 1U; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq == TIM3_IRQn); }
void HAL_NVIC_SetPriority(int irq, uint32_t pre, uint32_t sub)
{ assert(irq == TIM3_IRQn && pre == 1U && sub == 0U); }
static void reset(void)
{
    memset(TIM3, 0, sizeof(*TIM3)); pclk = 36000000U;
    RCC->CFGR = 4U << 8; AFIO->MAPR = 0x02000100U;
    echo = trig = gpio_count = 0U;
    assert(Hcsr04Timer_Init());
    assert(gpio_count == 2U && irq_enabled && trig == 0U);
    assert(TIM3->PSC == 71U && TIM3->ARR == 65535U && TIM3->CR1 == TIM_CR1_CEN);
    assert(TIM3->CCMR2 == (TIM_CCMR2_CC3S_0 | TIM_CCMR2_CC4S_1));
    assert(AFIO->MAPR == 0x02000100U);
}
static void event(uint32_t flags, uint16_t rise, uint16_t fall)
{
    TIM3->SR = flags; TIM3->CCR3 = rise; TIM3->CCR4 = fall;
    Hcsr04Timer_IRQHandler();
    /* Plain host integers cannot reproduce hardware's rc_w0 write semantics. */
    TIM3->SR = 0U;
}
int main(void)
{
    uint16_t width;
    reset(); assert(Hcsr04Timer_Start()); assert(trig && !Hcsr04Timer_Start());
    assert(TIM3->CCR1 == 12U && TIM3->CCR2 == 10000U);
    assert(TIM3->CCER == (TIM_CCER_CC3E | TIM_CCER_CC4E | TIM_CCER_CC4P));
    event(TIM_SR_CC1IF, 0U, 0U); assert(!trig);
    event(TIM_SR_CC3IF, 200U, 0U); assert(Hcsr04Timer_Poll(&width) == HCSR04_PENDING);
    event(TIM_SR_CC4IF, 0U, 200U + 1749U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_COMPLETE && width == 1749U && !TIM3->DIER);
    /* Both edge flags and timeout pending: captured times win if strictly before deadline. */
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC1IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_CC2IF, 200U, 3116U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_COMPLETE && width == 2916U);
    TIM3->CNT = 65530U; assert(Hcsr04Timer_Start());
    assert(TIM3->CCR1 == 6U && TIM3->CCR2 == 9994U);
    event(TIM_SR_CC1IF | TIM_SR_CC3IF | TIM_SR_CC4IF, 100U, 684U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_COMPLETE && width == 584U);
    echo = 1U; assert(Hcsr04Timer_Start());
    assert(Hcsr04Timer_Poll(&width) == HCSR04_ECHO_HIGH && !trig);
    echo = 0U; TIM3->CNT = 0U;
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC1IF | TIM_SR_CC2IF, 0U, 0U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_TIMEOUT && !trig);
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC4IF, 0U, 200U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_BAD_EDGE);
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC3IF | TIM_SR_CC3OF, 200U, 0U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_OVERCAPTURE);
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC3IF, 200U, 0U); event(TIM_SR_CC3IF, 300U, 0U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_BAD_EDGE);
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC3IF | TIM_SR_CC4IF, 200U, 10000U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_TIMEOUT);
    assert(Hcsr04Timer_Start()); event(TIM_SR_CC3IF | TIM_SR_CC4IF, 300U, 200U);
    assert(Hcsr04Timer_Poll(&width) == HCSR04_BAD_EDGE);
    assert(Hcsr04Timer_Start()); Hcsr04Timer_Cancel();
    assert(!trig && !TIM3->DIER && Hcsr04Timer_Poll(&width) == HCSR04_PENDING);
    pclk = 35000001U; assert(!Hcsr04Timer_Init() && !Hcsr04Timer_Start());
    pclk = 72000000U; RCC->CFGR = 0U; assert(Hcsr04Timer_Init() && TIM3->PSC == 71U);
    AFIO->MAPR |= AFIO_MAPR_TIM3_REMAP_FULLREMAP;
    assert(!Hcsr04Timer_Init() && !Hcsr04Timer_Start());
    puts("PASS: HC-SR04 real timer driver, pin modes, dual capture, IRQ deadline, overflow, faults and recovery");
    return 0;
}
