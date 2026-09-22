/**
 * @file    soil_moisture.h
 * @brief   电容式土壤湿度传感器驱动 (ADC)
 * @note    工作原理:
 *          土壤含水量变化 → 介电常数变化 → 电容变化 →
 *          振荡电路频率变化 → 输出电压变化
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          - AOUT → PB0 (ADC1_IN8)
 *          - VCC  → 3.3V, GND → GND
 *          (引脚与库默认一致, 无需修改)
 *
 *          输出特性 (3.3V 供电, 典型值):
 *          干燥空气: ~1.0V  (ADC≈1240)
 *          干燥土壤: ~1.2V  (ADC≈1490)
 *          潮湿土壤: ~1.8V  (ADC≈2230)
 *          水中:     ~2.5V  (ADC≈3100)
 *
 *          注意: ADC1 被多个模拟传感器共用 (MQ4/土壤湿度/雨滴),
 *          每次读取前会自动重新选择对应通道, 可安全共存。
 */
#ifndef __SOIL_MOISTURE_H
#define __SOIL_MOISTURE_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                  引脚配置宏 (用户可在此重定义)                    *
 *==================================================================*/
#define SM_ADC_PORT         GPIOB
#define SM_ADC_PIN          GPIO_Pin_0      /* PB0 = ADC1_IN8 */
#define SM_ADC_CHANNEL      ADC_Channel_8

/*==================================================================*
 *              湿度标定值 (根据实测调整)                            *
 *==================================================================*/
/*
 * 标定方法:
 * 1. 将传感器完全插入干燥土壤, 读取 ADC 值 → SM_DRY_VALUE
 * 2. 将传感器完全插入水中, 读取 ADC 值 → SM_WET_VALUE
 * 3. 百分比 = (raw - DRY) * 100 / (WET - DRY)
 *    低于 DRY → 0%, 高于 WET → 100%
 *
 * 以下为典型值 (3.3V 供电), 请根据实际测量修改:
 */
#define SM_DRY_VALUE        1490    /* 干燥土壤典型 ADC 值 */
#define SM_WET_VALUE        2900    /* 水中典型 ADC 值 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   初始化土壤湿度传感器
 * @note    配置 ADC1 通道 8 (PB0), 无需预热。
 */
void SM_Init(void);

/**
 * @brief   读取 ADC 原始值
 * @return  12 位 ADC 值 (0~4095), 对应 0V~3.3V
 * @note    值越大表示湿度越高。
 */
uint16_t SM_ReadADC(void);

/**
 * @brief   读取传感器输出电压
 * @return  电压 (V), 范围 0~3.3V
 */
float SM_ReadVoltage(void);

/**
 * @brief   读取土壤湿度百分比
 * @return  湿度百分比 0~100
 * @note    基于 SM_DRY_VALUE 和 SM_WET_VALUE 宏线性计算, 结果限幅 0~100。
 */
uint8_t SM_ReadPercent(void);

#ifdef __cplusplus
}
#endif

#endif
