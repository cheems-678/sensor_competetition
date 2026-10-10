#include "max4466_adc.h"
#include <max4466_adc_platform.h>
#include <string.h>

static ADC_HandleTypeDef adc;
static DMA_HandleTypeDef dma;
static uint16_t samples[MAX4466_CAPTURE_SAMPLES];
static uint32_t started_tick, sequence;
static uint8_t ready, capturing;

void Max4466Adc_Stop(void)
{
    HAL_NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    if (capturing) { (void)HAL_ADC_Stop_DMA(&adc); }
    /* Also quiesce partially initialized hardware and failed HAL stops.
     * Never reset DMA1: channel 2 belongs to the LED strip. */
    if (dma.Instance == DMA1_Channel1) {
        __HAL_DMA_DISABLE_IT(&dma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE);
        __HAL_DMA_DISABLE(&dma);
        __HAL_DMA_CLEAR_FLAG(&dma, DMA_FLAG_GL1);
    }
    if (adc.Instance == ADC1) { __HAL_ADC_DISABLE(&adc); }
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    ready = capturing = 0U;
}

static uint8_t Begin(void)
{
    started_tick = HAL_GetTick();
    /* HAL uses Start_IT internally. NVIC remains disabled throughout; remove
     * peripheral interrupt enables immediately and poll normal-DMA flags. */
    if (HAL_ADC_Start_DMA(&adc, (uint32_t *)samples, MAX4466_CAPTURE_SAMPLES) != HAL_OK) {
        Max4466Adc_Stop(); return 0U;
    }
    capturing = 1U;
    __HAL_DMA_DISABLE_IT(&dma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    /* STM32F1 HAL_ADC_Start_DMA does not propagate HAL_DMA_Start_IT's status.
     * Confirm the channel really armed instead of returning false success. */
    if ((dma.Instance->CCR & DMA_CCR_EN) == 0U ||
        dma.Instance->CNDTR == 0U || dma.Instance->CNDTR > MAX4466_CAPTURE_SAMPLES) {
        Max4466Adc_Stop(); return 0U;
    }
    return 1U;
}

uint8_t Max4466Adc_Start(void)
{
    GPIO_InitTypeDef gpio;
    RCC_PeriphCLKInitTypeDef clock;
    ADC_ChannelConfTypeDef channel;
    static const uint32_t channels[MAX4466_CHANNELS] = {
        ADC_CHANNEL_0, ADC_CHANNEL_4, ADC_CHANNEL_6, ADC_CHANNEL_8, ADC_CHANNEL_5
    };
    uint8_t rank;
    Max4466Adc_Stop();
    memset(&adc, 0, sizeof(adc)); memset(&dma, 0, sizeof(dma));
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE(); __HAL_RCC_ADC1_CLK_ENABLE();
    memset(&clock, 0, sizeof(clock));
    clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clock.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) { goto failed; }
    memset(&gpio, 0, sizeof(gpio));
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &gpio);
    dma.Instance = DMA1_Channel1;
    dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    dma.Init.PeriphInc = DMA_PINC_DISABLE; dma.Init.MemInc = DMA_MINC_ENABLE;
    dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    dma.Init.Mode = DMA_NORMAL; dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&dma) != HAL_OK) { goto failed; }
    adc.Instance = ADC1;
    adc.Init.ScanConvMode = ADC_SCAN_ENABLE;
    adc.Init.ContinuousConvMode = ENABLE; adc.Init.DiscontinuousConvMode = DISABLE;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT; adc.Init.NbrOfConversion = MAX4466_CHANNELS;
    __HAL_LINKDMA(&adc, DMA_Handle, dma);
    if (HAL_ADC_Init(&adc) != HAL_OK) { goto failed; }
    memset(&channel, 0, sizeof(channel));
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    for (rank = 0U; rank < MAX4466_CHANNELS; ++rank) {
        channel.Channel = channels[rank]; channel.Rank = rank + 1U;
        if (HAL_ADC_ConfigChannel(&adc, &channel) != HAL_OK) { goto failed; }
    }
    if (HAL_ADCEx_Calibration_Start(&adc) != HAL_OK) { goto failed; }
    sequence = 0U; ready = 1U;
    return Begin();
failed:
    Max4466Adc_Stop(); return 0U;
}

void Max4466Adc_IRQHandler(void)
{
    /* Defensive only: this version never enables this IRQ. A stray interrupt
     * must not re-enter HAL repeatedly and starve the communication loop. */
    Max4466Adc_Stop();
}

uint8_t Max4466Adc_Read(Max4466AdcWindow *output)
{
    uint32_t flags, sum[MAX4466_CHANNELS] = {0U}, now = HAL_GetTick();
    uint16_t frame;
    uint8_t ch;
    if (!output || !ready) { return 3U; }
    if (!capturing) {
        if ((uint32_t)(now - started_tick) >= MAX4466_CAPTURE_PERIOD_MS) {
            if (!Begin()) { return 3U; }
        }
        return 0U;
    }
    flags = DMA1->ISR;
    if (flags & DMA_FLAG_TE1) { Max4466Adc_Stop(); return 3U; }
    if ((flags & DMA_FLAG_TC1) && dma.Instance->CNDTR == 0U) {
        /* Non-circular DMA cannot overwrite this completed window. Stop ADC
         * and DMA first, including when the main loop was delayed by UART. */
        if (HAL_ADC_Stop_DMA(&adc) != HAL_OK) { Max4466Adc_Stop(); return 3U; }
        capturing = 0U;
        __HAL_DMA_CLEAR_FLAG(&dma, DMA_FLAG_GL1);
        __DMB();
        for (ch = 0U; ch < MAX4466_CHANNELS; ++ch) {
            output->minimum[ch] = 4095U; output->maximum[ch] = 0U;
        }
        for (frame = 0U; frame < MAX4466_CAPTURE_FRAMES; ++frame) {
            for (ch = 0U; ch < MAX4466_CHANNELS; ++ch) {
                uint16_t value = samples[frame * MAX4466_CHANNELS + ch];
                if (value > 4095U) { Max4466Adc_Stop(); return 3U; }
                if (value < output->minimum[ch]) { output->minimum[ch] = value; }
                if (value > output->maximum[ch]) { output->maximum[ch] = value; }
                sum[ch] += value;
            }
        }
        for (ch = 0U; ch < MAX4466_CHANNELS; ++ch) {
            output->mean[ch] = (uint16_t)(sum[ch] / MAX4466_CAPTURE_FRAMES);
        }
        output->samples = MAX4466_CAPTURE_FRAMES;
        output->sequence = ++sequence;
        output->tick = started_tick + MAX4466_CAPTURE_DURATION_MS;
        return 1U;
    }
    if ((uint32_t)(now - started_tick) >= MAX4466_CAPTURE_TIMEOUT_MS) {
        Max4466Adc_Stop(); return 3U;
    }
    return 0U;
}
