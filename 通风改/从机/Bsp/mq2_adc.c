#include "mq2_adc.h"
#include "stm32f1xx_hal.h"
#include <string.h>

static ADC_HandleTypeDef g_adc;
static uint8_t g_ready, g_pending;

void Mq2Adc_Stop(void)
{
    if (g_adc.Instance != 0) { (void)HAL_ADC_Stop(&g_adc); }
    g_pending = 0U;
}

uint8_t Mq2Adc_Init(void)
{
    GPIO_InitTypeDef pin = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};
    ADC_ChannelConfTypeDef channel = {0};
    g_ready = g_pending = 0U;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    /* ADC1 is owned exclusively by this service, including fault recovery. */
    __HAL_RCC_ADC1_FORCE_RESET();
    __HAL_RCC_ADC1_RELEASE_RESET();
    memset(&g_adc, 0, sizeof(g_adc));
    clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    clock.AdcClockSelection = RCC_ADCPCLK2_DIV6;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) { return 0U; }
    pin.Pin = GPIO_PIN_7;
    pin.Mode = GPIO_MODE_ANALOG;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pin);
    g_adc.Instance = ADC1;
    g_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    g_adc.Init.ContinuousConvMode = DISABLE;
    g_adc.Init.DiscontinuousConvMode = DISABLE;
    g_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    g_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    g_adc.Init.NbrOfConversion = 1U;
    if (HAL_ADC_Init(&g_adc) != HAL_OK) { return 0U; }
    channel.Channel = ADC_CHANNEL_7;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&g_adc, &channel) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(&g_adc) != HAL_OK)
    { Mq2Adc_Stop(); return 0U; }
    g_ready = 1U;
    return 1U;
}

uint8_t Mq2Adc_Start(void)
{
    if (g_ready == 0U || g_pending != 0U) { return 0U; }
    if (HAL_ADC_Start(&g_adc) != HAL_OK) { return 0U; }
    g_pending = 1U;
    return 1U;
}

uint8_t Mq2Adc_Poll(uint16_t *raw)
{
    uint32_t value;
    if (g_pending == 0U || raw == 0 || HAL_ADC_GetError(&g_adc) != HAL_ADC_ERROR_NONE)
    { return 2U; }
    if (__HAL_ADC_GET_FLAG(&g_adc, ADC_FLAG_EOC) == 0U) { return 0U; }
    value = HAL_ADC_GetValue(&g_adc);
    g_pending = 0U;
    if (HAL_ADC_Stop(&g_adc) != HAL_OK || value > 4095U) { return 2U; }
    *raw = (uint16_t)value;
    return 1U;
}
