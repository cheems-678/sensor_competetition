#include <assert.h>
#include <stdio.h>
#include "stm32f1xx_hal.h"
#include "mq2_adc.h"
ADC_TypeDef fake_adc1;
static unsigned failure, starts, stops, config, calibrations;
static uint32_t adc_error;
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c)
{
    assert(c->PeriphClockSelection == RCC_PERIPHCLK_ADC && c->AdcClockSelection == RCC_ADCPCLK2_DIV6);
    return failure == 1U ? HAL_ERROR : HAL_OK;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *p)
{ assert(port == GPIOA && p->Pin == GPIO_PIN_7 && p->Mode == GPIO_MODE_ANALOG && p->Pull == GPIO_NOPULL); }
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h)
{
    assert(h->Instance == ADC1 && h->Init.ScanConvMode == ADC_SCAN_DISABLE);
    assert(h->Init.ContinuousConvMode == DISABLE && h->Init.DiscontinuousConvMode == DISABLE);
    assert(h->Init.ExternalTrigConv == ADC_SOFTWARE_START && h->Init.DataAlign == ADC_DATAALIGN_RIGHT);
    assert(h->Init.NbrOfConversion == 1U);
    return failure == 2U ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c)
{
    (void)h; config++;
    assert(c->Channel == ADC_CHANNEL_7 && c->Rank == ADC_REGULAR_RANK_1 && c->SamplingTime == ADC_SAMPLETIME_239CYCLES_5);
    return failure == 3U ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h)
{ (void)h; calibrations++; return failure == 4U ? HAL_TIMEOUT : HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)
{ (void)h; starts++; fake_adc1.SR = 0U; return failure == 5U ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{ (void)h; stops++; return failure == 6U ? HAL_ERROR : HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { h->Instance->SR = 0U; return h->Instance->DR; }
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *h) { (void)h; return adc_error; }
int main(void)
{
    uint16_t raw = 99U;
    unsigned i;
    assert(!Mq2Adc_Start() && Mq2Adc_Poll(&raw) == 2U);
    for (i = 1U; i <= 4U; i++) { failure = i; assert(!Mq2Adc_Init() && !Mq2Adc_Start()); }
    failure = 0U; assert(Mq2Adc_Init());
    assert(config && calibrations);
    assert(Mq2Adc_Start() && !Mq2Adc_Start());
    assert(Mq2Adc_Poll(&raw) == 0U && raw == 99U);
    fake_adc1.SR = ADC_FLAG_EOC; fake_adc1.DR = 0U;
    assert(Mq2Adc_Poll(&raw) == 1U && raw == 0U);
    assert(Mq2Adc_Poll(&raw) == 2U);
    assert(Mq2Adc_Start()); fake_adc1.SR = ADC_FLAG_EOC; fake_adc1.DR = 4095U;
    assert(Mq2Adc_Poll(&raw) == 1U && raw == 4095U);
    failure = 5U; assert(!Mq2Adc_Start());
    failure = 0U; assert(Mq2Adc_Start()); adc_error = 1U;
    assert(Mq2Adc_Poll(&raw) == 2U); Mq2Adc_Stop();
    adc_error = 0U; assert(Mq2Adc_Start());
    fake_adc1.SR = ADC_FLAG_EOC; fake_adc1.DR = 4096U;
    assert(Mq2Adc_Poll(&raw) == 2U);
    assert(Mq2Adc_Start()); failure = 6U; fake_adc1.SR = ADC_FLAG_EOC;
    assert(Mq2Adc_Poll(&raw) == 2U);
    failure = 0U; Mq2Adc_Stop();
    assert(starts && stops);
    puts("MQ2 ADC: PA7/channel7, clock, calibration, nonblocking conversion and errors passed");
    return 0;
}
