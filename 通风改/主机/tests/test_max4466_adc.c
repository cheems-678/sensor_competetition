#include <assert.h>
#include <stdio.h>
#include "max4466_adc.h"
#include <max4466_adc_platform.h>
FakeChannel fake_channel;
FakeDma fake_dma;
static ADC_HandleTypeDef *adc;
static uint16_t *buffer;
static unsigned pins, rank, tick, stops, enabled;
static int fail;
static uint8_t copy_race;
void *Max4466TestCopy(void *dst, const void *src, size_t n) {
    size_t i; for(i=0U;i<n;i++) ((uint8_t *)dst)[i]=((const uint8_t *)src)[i];
    if(copy_race) { DMA1_Channel1->CNDTR=1200U; HAL_ADC_ConvCpltCallback(adc); copy_race=0U; }
    return dst;
}
uint32_t HAL_GetTick(void) { return tick; }
int HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *p) { assert(p->AdcClockSelection == 6U); return fail==1; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *p) {
    assert(p->Mode == GPIO_MODE_ANALOG && p->Pull == 0U);
    assert(p->Pin == (port == GPIOA ? 113U : 1U)); pins++;
}
int HAL_DMA_Init(DMA_HandleTypeDef *p) {
    assert(p->Instance == DMA1_Channel1 && p->Init.Mode == DMA_CIRCULAR);
    assert(p->Init.MemInc && p->Init.PeriphDataAlignment == 2U && p->Init.MemDataAlignment == 2U); return fail==2;
}
int HAL_ADC_Init(ADC_HandleTypeDef *p) { adc = p; rank = 0U; assert(p->Init.NbrOfConversion == 5U && p->Init.ContinuousConvMode && p->Init.ScanConvMode); return fail==3; }
int HAL_ADC_ConfigChannel(ADC_HandleTypeDef *p, ADC_ChannelConfTypeDef *c) {
    static const unsigned channels[5] = {0,4,6,8,5}; (void)p;
    assert(c->Rank == rank + 1U && c->Channel == channels[rank++] && c->SamplingTime == 239U); return fail==4;
}
int HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *p) { assert(p == adc); return fail==5; }
int HAL_ADC_Start_DMA(ADC_HandleTypeDef *p, uint32_t *buf, uint32_t count) {
    assert(p == adc && count == 1280U); buffer = (uint16_t *)buf; DMA1_Channel1->CNDTR = count; return fail==6;
}
int HAL_ADC_Stop_DMA(ADC_HandleTypeDef *p) { (void)p; stops++; return 0; }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *p) { (void)p; DMA1->ISR = 0U; }
void HAL_NVIC_DisableIRQ(int irq) { assert(irq == 11); enabled = 0; }
void HAL_NVIC_EnableIRQ(int irq) { assert(irq == 11); enabled = 1; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq == 11); }
void HAL_NVIC_SetPriority(int irq, unsigned preempt, unsigned sub) { assert(irq == 11 && preempt == 2 && sub == 0); }
int main(void) {
    uint16_t output[640]; uint32_t seq, stamp; unsigned i;
    assert(Max4466Adc_Read(output,&seq,&stamp) == 3U);
    assert(Max4466Adc_Start() && enabled && pins == 2 && rank == 5);
    assert(Max4466Adc_Read(output,&seq,&stamp) == 0U);
    for(i=0;i<1280;i++) buffer[i]=(uint16_t)i;
    tick=20; DMA1_Channel1->CNDTR=600; HAL_ADC_ConvHalfCpltCallback(adc);
    assert(Max4466Adc_Read(output,&seq,&stamp) == 1U && seq == 1 && stamp == 20 && output[639] == 639);
    tick=34; DMA1_Channel1->CNDTR=1200; HAL_ADC_ConvCpltCallback(adc);
    assert(Max4466Adc_Read(output,&seq,&stamp) == 1U && output[0] == 640);
    HAL_ADC_ConvHalfCpltCallback(adc); HAL_ADC_ConvCpltCallback(adc);
    assert(Max4466Adc_Read(output,&seq,&stamp) == 2U);
    HAL_ADC_ConvHalfCpltCallback(adc); DMA1_Channel1->CNDTR=1200;
    assert(Max4466Adc_Read(output,&seq,&stamp) == 2U);
    DMA1->ISR=DMA_FLAG_HT1 | DMA_FLAG_TC1; Max4466Adc_IRQHandler();
    assert(Max4466Adc_Read(output,&seq,&stamp) == 3U);
    assert(Max4466Adc_Start()); HAL_ADC_ErrorCallback(adc);
    assert(Max4466Adc_Read(output,&seq,&stamp) == 3U);
    for(fail=1;fail<=6;fail++) assert(!Max4466Adc_Start() && !enabled);
    fail=0; assert(Max4466Adc_Start());
    DMA1_Channel1->CNDTR=600U; HAL_ADC_ConvHalfCpltCallback(adc); copy_race=1U;
    assert(Max4466Adc_Read(output,&seq,&stamp)==2U);
    assert(Max4466Adc_Read(output,&seq,&stamp)==1U);
    DMA1->ISR=DMA_FLAG_TE1; Max4466Adc_IRQHandler(); assert(Max4466Adc_Read(output,&seq,&stamp)==3U);
    puts("MAX4466 ADC pin/rank/clock/DMA/stable-half/gap/error/restart passed"); return 0;
}
