#include "hcsr04_timer.h"
#include <hcsr04_timer_platform.h>

static volatile uint16_t g_origin, g_rise, g_width;
static volatile Hcsr04Result g_result;
static volatile uint8_t g_active, g_have_rise;
static uint8_t g_ready;

static void Finish(Hcsr04Result result)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
    TIM3->DIER = 0U;
    TIM3->CCER = 0U;
    g_active = 0U;
    g_result = result; /* Publish after all captured data has been written. */
}

uint8_t Hcsr04Timer_Init(void)
{
    GPIO_InitTypeDef pin = {0};
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    g_ready = g_active = g_have_rise = 0U;
    g_result = HCSR04_PENDING;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
    pin.Pin = GPIO_PIN_1;
    pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_0;
    pin.Mode = GPIO_MODE_INPUT;
    pin.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &pin);
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) { clock *= 2U; }
    if (clock < 1000000U || clock % 1000000U != 0U) { return 0U; }
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();
    /* Both default and partial remap retain PB0/TI3. Reject full remap;
       avoid touching MAPR's write-only SWJ bits and other peripherals. */
    if ((AFIO->MAPR & AFIO_MAPR_TIM3_REMAP) == AFIO_MAPR_TIM3_REMAP_FULLREMAP)
    { return 0U; }
    TIM3->PSC = clock / 1000000U - 1U;
    TIM3->ARR = 65535U;
    TIM3->CCMR1 = 0U; /* CH1/CH2 compare only; output pins disabled. */
    TIM3->CCMR2 = TIM_CCMR2_CC3S_0 | TIM_CCMR2_CC4S_1;
    TIM3->CCER = 0U;
    TIM3->DIER = 0U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CR1 = TIM_CR1_CEN;
    __HAL_DBGMCU_FREEZE_TIM3();
    HAL_NVIC_SetPriority(TIM3_IRQn, 1U, 0U);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    g_ready = 1U;
    return 1U;
}

uint8_t Hcsr04Timer_Start(void)
{
    if (g_ready == 0U || g_active != 0U) { return 0U; }
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    g_width = 0U;
    g_have_rise = 0U;
    g_result = HCSR04_PENDING;
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) != GPIO_PIN_RESET)
    {
        Finish(HCSR04_ECHO_HIGH);
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
        return 1U;
    }
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->SR = 0U;
    g_origin = (uint16_t)TIM3->CNT;
    TIM3->CCR1 = (uint16_t)(g_origin + 12U);
    TIM3->CCR2 = (uint16_t)(g_origin + HCSR04_ECHO_TIMEOUT_US);
    /* Capture rising/falling edges in hardware even if IRQ handling is delayed. */
    TIM3->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E | TIM_CCER_CC4P;
    TIM3->DIER = TIM_DIER_CC1IE | TIM_DIER_CC2IE | TIM_DIER_CC3IE | TIM_DIER_CC4IE;
    g_active = 1U;
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    TIM3->CR1 |= TIM_CR1_CEN;
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    return 1U;
}

void Hcsr04Timer_IRQHandler(void)
{
    uint32_t flags = TIM3->SR;
    uint16_t rise = 0U, fall = 0U;
    /* Reading CCRx acknowledges its flag. Only read captured channels present
       in this snapshot, so a new opposite edge retains its pending interrupt. */
    if ((flags & TIM_SR_CC3IF) != 0U) { rise = (uint16_t)TIM3->CCR3; }
    if ((flags & TIM_SR_CC4IF) != 0U) { fall = (uint16_t)TIM3->CCR4; }
    flags |= TIM3->SR & (TIM_SR_CC3OF | TIM_SR_CC4OF);
    TIM3->SR = ~flags; /* STM32 rc_w0: do not erase flags arriving after the read. */
    if (g_active == 0U) { return; }
    if ((flags & TIM_SR_CC1IF) != 0U)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
        TIM3->DIER &= ~TIM_DIER_CC1IE;
    }
    if ((flags & (TIM_SR_CC3OF | TIM_SR_CC4OF)) != 0U)
    { Finish(HCSR04_OVERCAPTURE); return; }
    if ((flags & TIM_SR_CC3IF) != 0U)
    {
        uint16_t elapsed = (uint16_t)(rise - g_origin);
        if (g_have_rise != 0U || elapsed < 12U || elapsed >= HCSR04_ECHO_TIMEOUT_US)
        { Finish(HCSR04_BAD_EDGE); return; }
        g_rise = rise;
        g_have_rise = 1U;
    }
    if ((flags & TIM_SR_CC4IF) != 0U)
    {
        uint16_t elapsed = (uint16_t)(fall - g_origin);
        g_width = (uint16_t)(fall - g_rise);
        if (g_have_rise == 0U || g_width == 0U || g_width > elapsed)
        { Finish(HCSR04_BAD_EDGE); return; }
        if (elapsed >= HCSR04_ECHO_TIMEOUT_US)
        { Finish(HCSR04_TIMEOUT); return; }
        Finish(HCSR04_COMPLETE);
        return;
    }
    if ((flags & TIM_SR_CC2IF) != 0U) { Finish(HCSR04_TIMEOUT); }
}

Hcsr04Result Hcsr04Timer_Poll(uint16_t *pulse_us)
{
    Hcsr04Result result;
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    result = g_result;
    if (pulse_us != 0) { *pulse_us = g_width; }
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    return result;
}

void Hcsr04Timer_Cancel(void)
{
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    Finish(HCSR04_PENDING);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}
