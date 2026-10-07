#include "max4466_adc.h"
#include <max4466_adc_platform.h>
#include <string.h>
static ADC_HandleTypeDef adc;
static DMA_HandleTypeDef dma;
static uint16_t samples[2U * MAX4466_BLOCK_WORDS];
static volatile uint32_t produced, completed_tick;
static volatile uint8_t half, fault;
static uint32_t consumed;
static uint8_t running;

void Max4466Adc_Stop(void)
{
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    if (running) { (void)HAL_ADC_Stop_DMA(&adc); }
    running = 0U;
}
uint8_t Max4466Adc_Start(void)
{
    GPIO_InitTypeDef gpio;
    RCC_PeriphCLKInitTypeDef clock;
    ADC_ChannelConfTypeDef channel;
    static const uint32_t inputs[5] = { ADC_CHANNEL_0, ADC_CHANNEL_4,
        ADC_CHANNEL_6, ADC_CHANNEL_8, ADC_CHANNEL_5 };
    uint8_t i;
    Max4466Adc_Stop();
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_ADC1_CLK_ENABLE();
    memset(&clock, 0, sizeof(clock));
    clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clock.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) { return 0U; }
    memset(&gpio, 0, sizeof(gpio)); gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &gpio); gpio.Pin = GPIO_PIN_0; HAL_GPIO_Init(GPIOB, &gpio);
    memset(&dma, 0, sizeof(dma)); dma.Instance = DMA1_Channel1;
    dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    dma.Init.PeriphInc = DMA_PINC_DISABLE; dma.Init.MemInc = DMA_MINC_ENABLE;
    dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    dma.Init.Mode = DMA_CIRCULAR; dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&dma) != HAL_OK) { return 0U; }
    memset(&adc, 0, sizeof(adc)); adc.Instance = ADC1;
    adc.Init.ScanConvMode = ADC_SCAN_ENABLE; adc.Init.ContinuousConvMode = ENABLE;
    adc.Init.DiscontinuousConvMode = DISABLE;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT; adc.Init.NbrOfConversion = 5U;
    __HAL_LINKDMA(&adc, DMA_Handle, dma);
    if (HAL_ADC_Init(&adc) != HAL_OK) { return 0U; }
    memset(&channel, 0, sizeof(channel)); channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    for (i = 0U; i < 5U; ++i) {
        channel.Channel = inputs[i]; channel.Rank = i + 1U;
        if (HAL_ADC_ConfigChannel(&adc, &channel) != HAL_OK) { return 0U; }
    }
    if (HAL_ADCEx_Calibration_Start(&adc) != HAL_OK) { return 0U; }
    produced = consumed = completed_tick = 0U; half = fault = 0U;
    __HAL_DMA_CLEAR_FLAG(&dma, DMA_FLAG_GL1);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 2U, 0U);
    running = 1U;
    if (HAL_ADC_Start_DMA(&adc, (uint32_t *)samples, 2U * MAX4466_BLOCK_WORDS) != HAL_OK) {
        Max4466Adc_Stop(); return 0U;
    }
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn); return 1U;
}
static void Complete(ADC_HandleTypeDef *handle, uint8_t index)
{
    if (handle == &adc && running && !fault) {
        half = index; completed_tick = HAL_GetTick(); __DMB(); produced++;
    }
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *handle) { Complete(handle, 0U); }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *handle) { Complete(handle, 1U); }
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *handle) { if (handle == &adc) { fault = 1U; } }
void Max4466Adc_IRQHandler(void)
{
    uint32_t flags = DMA1->ISR;
    if ((flags & DMA_FLAG_TE1) ||
        ((flags & (DMA_FLAG_HT1 | DMA_FLAG_TC1)) == (DMA_FLAG_HT1 | DMA_FLAG_TC1))) { fault = 1U; }
    HAL_DMA_IRQHandler(&dma);
}
static uint8_t Stable(uint8_t index)
{
    uint32_t left = DMA1_Channel1->CNDTR;
    if (DMA1->ISR & (DMA_FLAG_HT1 | DMA_FLAG_TC1 | DMA_FLAG_TE1)) { return 0U; }
    return index == 0U ? (left > 0U && left <= MAX4466_BLOCK_WORDS) :
                        (left > MAX4466_BLOCK_WORDS && left <= 2U * MAX4466_BLOCK_WORDS);
}
uint8_t Max4466Adc_Read(uint16_t *output, uint32_t *sequence, uint32_t *tick)
{
    uint32_t count, stamp, mask;
    uint8_t index, gap;
    if (!output || !sequence || !tick || !running || fault) { return 3U; }
    mask = __get_PRIMASK(); __disable_irq();
    count = produced; stamp = completed_tick; index = half; __set_PRIMASK(mask);
    if (count == consumed) { return 0U; }
    gap = (uint32_t)(count - consumed) != 1U; consumed = count;
    if (gap || !Stable(index)) { return 2U; }
    memcpy(output, samples + index * MAX4466_BLOCK_WORDS, MAX4466_BLOCK_WORDS * sizeof(uint16_t));
    __DMB();
    if (fault) { return 3U; }
    if (count != produced || !Stable(index)) { return 2U; }
    *sequence = count; *tick = stamp; return 1U;
}
