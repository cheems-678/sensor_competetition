/**
 * @file    soil_moisture.c
 * @brief   电容式土壤湿度传感器驱动实现
 * @note    电容式土壤湿度传感器利用土壤介电常数随含水量
 *          变化的特性来测量湿度:
 *          - 湿度越高 → 介电常数越大 → 输出电压越高
 *          - 供电: 3.3V, 输出 0~3.3V
 *
 *          【本工程适配】AOUT 接 PB0 (ADC1_IN8)。
 *          ADC1 与 MQ4(PA0/CH0)、雨滴(PB1/CH9)共用,
 *          每次 Read 前重新配置通道, 可安全切换。
 */
#include "soil_moisture.h"

/*==================================================================*
 *                      模块初始化                                   *
 *==================================================================*/

void SM_Init(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef  adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = SM_ADC_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_Init(SM_ADC_PORT, &gpio);

    ADC_DeInit(ADC1);

    ADC_StructInit(&adc);
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, SM_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/*==================================================================*
 *                       ADC 数据读取                                *
 *==================================================================*/

uint16_t SM_ReadADC(void)
{
    /* 重新选择 ADC1 通道 8 (PB0), 避免与其他 ADC 传感器冲突 */
    ADC_RegularChannelConfig(ADC1, SM_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
    return ADC_GetConversionValue(ADC1);
}

float SM_ReadVoltage(void)
{
    return (float)SM_ReadADC() * 3.3f / 4095.0f;
}

/*==================================================================*
 *                       湿度百分比计算                              *
 *==================================================================*/

uint8_t SM_ReadPercent(void)
{
    uint16_t raw = SM_ReadADC();
    uint32_t pct;

    /* 低于干燥基准 → 0% */
    if (raw <= SM_DRY_VALUE) {
        return 0;
    }

    /* 高于浸湿基准 → 100% */
    if (raw >= SM_WET_VALUE) {
        return 100;
    }

    /* 在范围内, 线性计算 (先乘 100 再除, 避免整数除法精度损失) */
    pct = (uint32_t)(raw - SM_DRY_VALUE) * 100UL;
    pct = pct / (SM_WET_VALUE - SM_DRY_VALUE);

    return (uint8_t)pct;
}
