#include "ws2812.h"

#include <string.h>

#include "stm32f1xx_hal.h"

#define WS2812_PIXEL_COUNT       (30U)
#define WS2812_BITS_PER_PIXEL    (24U)
#define WS2812_DATA_BITS         (WS2812_PIXEL_COUNT * WS2812_BITS_PER_PIXEL)
#define WS2812_BUFFER_LENGTH     (WS2812_DATA_BITS + 1U)
#define WS2812_TIMER_PERIOD      (90U) /* 72 MHz / 90 = 800 kHz */
#define WS2812_ZERO_HIGH_TICKS   (29U) /* about 0.40 us */
#define WS2812_ONE_HIGH_TICKS    (58U) /* about 0.81 us */
#define WS2812_SEND_TIMEOUT_MS   (5U)
#define WS2812_POLL_LIMIT        (1000000UL)

volatile Ws2812Diagnostics Ws2812Diag;
static uint16_t g_ws2812_pwm[WS2812_BUFFER_LENGTH];

static void Ws2812_PinLow(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void Ws2812_PinTimerOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static uint16_t Ws2812_EncodeBit(uint8_t value, uint8_t bit)
{
    return ((value & (uint8_t)(1U << bit)) != 0U) ?
           WS2812_ONE_HIGH_TICKS : WS2812_ZERO_HIGH_TICKS;
}

static void Ws2812_FillByte(uint16_t *buffer, uint32_t *index, uint8_t value)
{
    int8_t bit;

    for (bit = 7; bit >= 0; bit--)
    {
        buffer[*index] = Ws2812_EncodeBit(value, (uint8_t)bit);
        (*index)++;
    }
}

static uint8_t Ws2812_WaitForFlag(volatile uint32_t *reg,
                                  uint32_t flag,
                                  uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t polls = 0U;

    while ((*reg & flag) == 0U)
    {
        if (((uint32_t)(HAL_GetTick() - start) >= timeout_ms) ||
            (++polls >= WS2812_POLL_LIMIT))
        {
            return 0U;
        }
    }
    return 1U;
}

void Ws2812_Init(void)
{
    memset((void *)&Ws2812Diag, 0, sizeof(Ws2812Diag));
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    Ws2812_PinLow();
}

uint8_t Ws2812_SetSolid(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t pixel;
    uint32_t index = 0U;
    uint32_t start = HAL_GetTick();
    uint8_t ok = 1U;

    for (pixel = 0U; pixel < WS2812_PIXEL_COUNT; pixel++)
    {
        /* WS2812 consumes each pixel in GRB, most-significant-bit first. */
        Ws2812_FillByte(g_ws2812_pwm, &index, green);
        Ws2812_FillByte(g_ws2812_pwm, &index, red);
        Ws2812_FillByte(g_ws2812_pwm, &index, blue);
    }
    g_ws2812_pwm[index] = 0U;

    DMA1_Channel2->CCR = 0U;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    __HAL_RCC_TIM1_FORCE_RESET();
    __HAL_RCC_TIM1_RELEASE_RESET();

    TIM1->PSC = 0U;
    TIM1->ARR = WS2812_TIMER_PERIOD - 1U;
    TIM1->CCR1 = g_ws2812_pwm[0];
    TIM1->CCMR1 = TIM_CCMR1_OC1PE |
                  TIM_CCMR1_OC1M_0 |
                  TIM_CCMR1_OC1M_1 |
                  TIM_CCMR1_OC1M_2; /* PWM mode 2. */
    /* PB13 is TIM1_CH1N. Invert the complementary output so every WS2812
     * cell starts high and CCR1 directly represents the high pulse width.
     */
    TIM1->CCER = TIM_CCER_CC1NE | TIM_CCER_CC1NP;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->CR1 = TIM_CR1_ARPE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;

    DMA1_Channel2->CPAR = (uint32_t)&TIM1->CCR1;
    DMA1_Channel2->CMAR = (uint32_t)&g_ws2812_pwm[1];
    DMA1_Channel2->CNDTR = WS2812_BUFFER_LENGTH - 1U;
    DMA1_Channel2->CCR = DMA_CCR_DIR | DMA_CCR_MINC |
                         DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 |
                         DMA_CCR_PL_1;

    Ws2812_PinTimerOutput();
    DMA1_Channel2->CCR |= DMA_CCR_EN;
    TIM1->DIER = TIM_DIER_CC1DE;
    TIM1->CR1 |= TIM_CR1_CEN;

    if (Ws2812_WaitForFlag(&DMA1->ISR, DMA_ISR_TCIF2,
                           WS2812_SEND_TIMEOUT_MS) == 0U)
    {
        ok = 0U;
    }
    else
    {
        /* TC occurs at the last data-bit compare. Let that PWM period finish
         * so the zero-width reset entry becomes active before stopping.
         */
        TIM1->SR &= ~TIM_SR_UIF;
        if (Ws2812_WaitForFlag(&TIM1->SR, TIM_SR_UIF,
                               WS2812_SEND_TIMEOUT_MS) == 0U)
        {
            ok = 0U;
        }
    }

    Ws2812Diag.last_dma_isr = DMA1->ISR;
    TIM1->DIER = 0U;
    TIM1->CR1 = 0U;
    TIM1->CCER = 0U;
    TIM1->BDTR = 0U;
    DMA1_Channel2->CCR = 0U;
    DMA1->IFCR = DMA_IFCR_CGIF2;
    Ws2812_PinLow();

    Ws2812Diag.last_output_idle_ok =
        ((TIM1->CR1 & TIM_CR1_CEN) == 0U) &&
        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_RESET);
    if (Ws2812Diag.last_output_idle_ok == 0U)
    {
        ok = 0U;
    }
    Ws2812Diag.last_elapsed_ms = HAL_GetTick() - start;
    if (ok == 0U)
    {
        Ws2812Diag.failure_count++;
        return 0U;
    }

    Ws2812Diag.send_count++;
    Ws2812Diag.last_red = red;
    Ws2812Diag.last_green = green;
    Ws2812Diag.last_blue = blue;
    return 1U;
}
