/**
 * @file    rain.c
 * @brief   雨滴/雨水检测传感器驱动实现
 * @note    雨滴传感器利用感应板上的水膜导电性变化工作:
 *          - 无水时感应板接近开路, AO 输出高电压
 *          - 有水时感应板导电, AO 输出降低
 *
 *          【本工程适配】AO 接 PB1 (ADC1_IN9), DO 接 PB2。
 *          ADC1 与 MQ4(PA0/CH0)、土壤湿度(PB0/CH8)共用,
 *          每次 Read 前重新配置通道, 可安全切换。
 */
#include "rain.h"

/*==================================================================*
 *                      模块初始化                                   *
 *==================================================================*/

void RAIN_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef  adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1, ENABLE);

    /* 配置 PB1 为模拟输入 (AO) */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = RAIN_ADC_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_Init(RAIN_ADC_PORT, &gpio);

    /* 配置 PB2 为浮空输入 (DO) */
    gpio.GPIO_Pin  = RAIN_DOUT_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(RAIN_DOUT_PORT, &gpio);

    ADC_DeInit(ADC1);

    ADC_StructInit(&adc);
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, RAIN_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/*==================================================================*
 *                       ADC 数据读取                                *
 *==================================================================*/

uint16_t RAIN_ReadADC(void)
{
    /* 重新选择 ADC1 通道 9 (PB1), 避免与其他 ADC 传感器冲突 */
    ADC_RegularChannelConfig(ADC1, RAIN_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
    return ADC_GetConversionValue(ADC1);
}

float RAIN_ReadVoltage(void)
{
    return (float)RAIN_ReadADC() * 3.3f / 4095.0f;
}

uint8_t RAIN_ReadPercent(void)
{
    uint16_t raw = RAIN_ReadADC();
    uint32_t pct;

    /* 电压越低雨量越大: 0~4095 反转为 100~0 */
    pct = (uint32_t)(4095u - raw) * 100UL / 4095UL;

    return (uint8_t)pct;
}

/*==================================================================*
 *                       数字报警输出读取                             *
 *==================================================================*/

uint8_t RAIN_ReadAlarm(void)
{
    BitAction lvl = GPIO_ReadInputDataBit(RAIN_DOUT_PORT, RAIN_DOUT_PIN);

#if RAIN_DOUT_ACTIVE_LOW
    return (lvl == Bit_RESET) ? RAIN_DETECTED : RAIN_NORMAL;
#else
    return (lvl == Bit_SET)   ? RAIN_DETECTED : RAIN_NORMAL;
#endif
}
