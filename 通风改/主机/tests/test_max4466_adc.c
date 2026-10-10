#include <assert.h>
#include <stdio.h>
#include "max4466_adc.h"
#include <max4466_adc_platform.h>
FakeChannel fake_channel;
FakeDma fake_dma;
static ADC_HandleTypeDef *adc;
static uint16_t *buffer;
static uint32_t tick;
static unsigned pins, starts, stops, ranks;
static int fail;
static unsigned fail_rank = 1U;

uint32_t HAL_GetTick(void) { return tick; }
int HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p)
{ assert(p->AdcClockSelection == 6U); return fail == 1; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *p)
{
    assert(p->Mode == GPIO_MODE_ANALOG && !p->Pull);
    if (port == GPIOA) { assert(p->Pin == (1U | 16U | 32U | 64U)); }
    else { assert(port == GPIOB && p->Pin == 1U); }
    pins++;
}
int HAL_DMA_Init(DMA_HandleTypeDef *p)
{
    assert(p->Instance == DMA1_Channel1 && p->Init.Mode == DMA_NORMAL);
    assert(p->Init.MemInc && p->Init.PeriphDataAlignment == 2U &&
           p->Init.MemDataAlignment == 2U && p->Init.Priority == DMA_PRIORITY_LOW);
    return fail == 2;
}
int HAL_ADC_Init(ADC_HandleTypeDef *p)
{ adc = p; ranks = 0U; assert(p->Init.NbrOfConversion == 5U && p->Init.ScanConvMode && p->Init.ContinuousConvMode); return fail == 3; }
int HAL_ADC_ConfigChannel(ADC_HandleTypeDef *p, ADC_ChannelConfTypeDef *c)
{
    static const unsigned channels[5] = {0U, 4U, 6U, 8U, 5U};
    assert(p == adc && ranks < 5U && c->Channel == channels[ranks] &&
           c->Rank == ranks + 1U && c->SamplingTime == 239U);
    ranks++;
    return fail == 4 && ranks == fail_rank;
}
int HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *p) { assert(p == adc && ranks == 5U); return fail == 5; }
int HAL_ADC_Start_DMA(ADC_HandleTypeDef *p, uint32_t *buf, uint32_t count)
{
    assert(p == adc && count == 2560U); buffer = (uint16_t *)buf;
    starts++;
    DMA1->ISR &= ~DMA_FLAG_GL1;
    DMA1_Channel1->CNDTR = count;
    DMA1_Channel1->CCR = fail == 7 ? 0U : (DMA_CCR_EN | DMA_IT_HT | DMA_IT_TC | DMA_IT_TE);
    return fail == 6;
}
int HAL_ADC_Stop_DMA(ADC_HandleTypeDef *p)
{
    assert(p == adc); stops++;
    DMA1_Channel1->CCR = 0U;
    return fail == 8;
}
void HAL_NVIC_DisableIRQ(int irq) { assert(irq == 11); }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; assert(!"DMA IRQ must remain disabled"); }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq == 11); }

static void constant(uint16_t value)
{ unsigned i; for (i = 0U; i < MAX4466_CAPTURE_SAMPLES; i++) { buffer[i] = value; } }
static void complete(uint32_t now)
{ tick = now; DMA1_Channel1->CNDTR = 0U; DMA1->ISR |= DMA_FLAG_TC1; }

int main(void)
{
    Max4466AdcWindow w;
    unsigned i, ch, old_starts;
    assert(Max4466Adc_Read(&w) == 3U);
    tick = 0U; assert(Max4466Adc_Start() && pins == 2U && ranks == 5U);
    assert(DMA1_Channel1->CCR == DMA_CCR_EN);
    constant(2048U);
    tick = 26U; DMA1->ISR |= DMA_FLAG_HT1;
    assert(Max4466Adc_Read(&w) == 0U); /* Half buffer is never published. */
    complete(54U);
    assert(Max4466Adc_Read(&w) == 1U && w.tick == 53U && w.sequence == 1U);
    for (ch = 0U; ch < 5U; ch++) {
        assert(w.minimum[ch] == 2048U && w.maximum[ch] == 2048U && w.mean[ch] == 2048U);
    }
    assert(w.samples == 512U);
    old_starts = starts; tick = 59U; assert(Max4466Adc_Read(&w) == 0U && starts == old_starts);
    tick = 60U; assert(Max4466Adc_Read(&w) == 0U && starts == old_starts + 1U);
    for (i = 0U; i < 512U; i++) {
        for (ch = 0U; ch < 5U; ch++) {
            buffer[i * 5U + ch] = i % 2U ? (uint16_t)(1000U + 100U * ch) : 1000U;
        }
    }
    complete(170U); /* UART/main-loop delay doesn't renew the source timestamp. */
    DMA1->ISR |= 0x20U; /* Channel 2 TC: owned by the LED strip, never cleared. */
    assert(Max4466Adc_Read(&w) == 1U && w.tick == 113U);
    for (ch = 0U; ch < 5U; ch++) {
        assert(w.minimum[ch] == 1000U && w.maximum[ch] == 1000U + 100U * ch &&
               w.mean[ch] == 1000U + 50U * ch);
    }
    assert(DMA1->ISR == 0x20U);
    assert(Max4466Adc_Read(&w) == 0U); constant(4095U); complete(224U);
    assert(Max4466Adc_Read(&w) == 1U);
    for (ch = 0U; ch < 5U; ch++) {
        assert(w.maximum[ch] - w.minimum[ch] == 0U && w.mean[ch] == 4095U);
    }
    tick = 230U; assert(Max4466Adc_Read(&w) == 0U); constant(0U); complete(284U);
    assert(Max4466Adc_Read(&w) == 1U);
    for (ch = 0U; ch < 5U; ch++) { assert(w.maximum[ch] == 0U && w.mean[ch] == 0U); }
    /* Vary exactly one rank: no extrema or mean may leak to another channel. */
    for (ch = 0U; ch < 5U; ch++) {
        tick += 6U; assert(Max4466Adc_Read(&w) == 0U); constant(2048U);
        for (i = 0U; i < 512U; i++) { buffer[i * 5U + ch] = i % 2U ? 4095U : 0U; }
        complete(tick + 54U); assert(Max4466Adc_Read(&w) == 1U);
        for (i = 0U; i < 5U; i++) {
            assert((unsigned)(w.maximum[i] - w.minimum[i]) == (i == ch ? 4095U : 0U));
            assert(w.mean[i] == (i == ch ? 2047U : 2048U));
        }
    }
    tick += 6U; assert(Max4466Adc_Read(&w) == 0U);
    tick += 99U; assert(Max4466Adc_Read(&w) == 0U);
    tick++; assert(Max4466Adc_Read(&w) == 3U && !DMA1_Channel1->CCR);
    for (fail = 1; fail <= 7; fail++) {
        assert(!Max4466Adc_Start() && !DMA1_Channel1->CCR);
    }
    fail = 4;
    for (fail_rank = 1U; fail_rank <= 5U; fail_rank++) {
        assert(!Max4466Adc_Start() && ranks == fail_rank && !DMA1_Channel1->CCR);
    }
    fail = 0; tick = 500U; assert(Max4466Adc_Start());
    DMA1->ISR |= DMA_FLAG_TE1; assert(Max4466Adc_Read(&w) == 3U);
    assert(Max4466Adc_Start()); constant(2048U); buffer[2559] = 4096U; complete(554U);
    assert(Max4466Adc_Read(&w) == 3U);
    assert(Max4466Adc_Start()); constant(1234U); complete(608U); fail = 8;
    assert(Max4466Adc_Read(&w) == 3U && !DMA1_Channel1->CCR);
    fail = 0; tick = 0xFFFFFFF0U; assert(Max4466Adc_Start()); constant(1000U);
    complete(38U); assert(Max4466Adc_Read(&w) == 1U && w.tick == 37U);
    tick = 44U; assert(Max4466Adc_Read(&w) == 0U);
    Max4466Adc_IRQHandler(); assert(Max4466Adc_Read(&w) == 3U);
    assert(stops > 0U);
    puts("Five-channel ranks/interleaving/isolation/normal-DMA/no-IRQ/rails/stale-source/fault/wrap passed");
    return 0;
}
