/**
 * @file    mq4.c
 * @brief   MQ-4 甲烷/可燃气体传感器驱动实现
 * @note    MQ-4 是一款半导体气体传感器, 内部加热丝将
 *          SnO2 敏感层加热到工作温度 (~450°C)。当甲烷等
 *          还原性气体与敏感层接触时, 电导率上升, 输出电压升高。
 *
 *          ADC 采样原理:
 *          STM32F103C8 内置 12 位逐次逼近型 ADC,
 *          输入电压 0~3.3V 对应数值 0~4095。
 *
 *          【本工程适配】AOUT 接 PA0 (ADC1_IN0), DOUT 接 PA2。
 *          ADC1 与土壤湿度(PB0/CH8)、雨滴(PB1/CH9)共用,
 *          每次 Read 前重新配置通道, 可安全切换。
 *
 *          MQ-4 特性 (典型值, Rs/R0 vs 甲烷浓度):
 *          甲烷(CH4)   200ppm    500ppm    1000ppm    2000ppm
 *          -------------------------------------------------
 *                      0.80      0.65      0.50       0.35
 *          (Rs = 传感器电阻, R0 = 清洁空气中电阻, 需自行标定)
 */
#include "mq4.h"

/*==================================================================*
 *                      模块初始化                                   *
 *==================================================================*/

/**
 * @brief   初始化 MQ-4 传感器
 * @note    配置 ADC1 和 DOUT GPIO:
 *          - PA0 (ADC1_IN0): 模拟输入
 *          - PA2: 浮空输入 (LM393 比较器数字输出)
 */
void MQ4_Init(void)
{
    GPIO_InitTypeDef   gpio;
    ADC_InitTypeDef    adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    /* 配置 PA0 为模拟输入 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = MQ4_ADC_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_Init(MQ4_ADC_PORT, &gpio);

    /* 配置 PA2 为浮空输入 (DOUT) */
    gpio.GPIO_Pin  = MQ4_DOUT_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(MQ4_DOUT_PORT, &gpio);

    /* 复位 ADC1, 确保从已知状态开始 */
    ADC_DeInit(ADC1);

    ADC_StructInit(&adc);
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, MQ4_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/*==================================================================*
 *                       ADC 数据读取                                *
 *==================================================================*/

/**
 * @brief   读取 ADC 原始值 (12 位)
 * @return  ADC 原始值 0~4095, 对应 0V~3.3V
 * @note    ADC1 被多个传感器共用, 读取前先重新选择本通道。
 */
uint16_t MQ4_ReadADC(void)
{
    uint16_t value;

    /* 重新选择 ADC1 通道 0 (PA0), 避免与其他 ADC 传感器冲突 */
    ADC_RegularChannelConfig(ADC1, MQ4_ADC_CHANNEL, 1, ADC_SampleTime_55Cycles5);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);

    value = ADC_GetConversionValue(ADC1);

    return value;
}

/**
 * @brief   读取 PA0 引脚电压 (分压后)
 * @return  电压 (V), 范围 0~3.3V
 */
float MQ4_ReadADCVoltage(void)
{
    uint16_t raw = MQ4_ReadADC();

    return (float)raw * 3.3f / 4095.0f;
}

/**
 * @brief   读取传感器真实输出电压 (还原分压)
 * @return  传感器 AOUT 引脚实际电压 (V)
 */
float MQ4_ReadSensorVoltage(void)
{
    float adc_v = MQ4_ReadADCVoltage();

    return adc_v * MQ4_VOLTAGE_DIVIDER;
}

/*==================================================================*
 *                      数字报警输出读取                             *
 *==================================================================*/

/**
 * @brief   读取数字报警输出 (DOUT)
 * @return  MQ4_ALARM_NORMAL (0):  气体浓度正常
 *          MQ4_ALARM_DETECTED (1): 气体浓度超标
 * @note    DOUT 由模块上的 LM393 比较器驱动, 阈值通过电位器调节。
 */
uint8_t MQ4_ReadAlarm(void)
{
    if (GPIO_ReadInputDataBit(MQ4_DOUT_PORT, MQ4_DOUT_PIN) == Bit_SET) {
        return MQ4_ALARM_DETECTED;
    } else {
        return MQ4_ALARM_NORMAL;
    }
}
