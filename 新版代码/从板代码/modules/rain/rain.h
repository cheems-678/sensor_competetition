/**
 * @file    rain.h
 * @brief   雨滴/雨水检测传感器模块驱动 (ADC + 数字输出)
 * @note    雨滴传感器由雨滴感应板 + LM393 比较器电路组成:
 *          - 感应板表面有水珠时, 导电性增强, 输出电压降低
 *          - AO (模拟输出): 输出电压, 无雨时偏高, 有雨时偏低
 *          - DO (数字输出): 阈值比较结果, 灵敏度由电位器调节
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          - AO → PB1 (ADC1_IN9, 模拟输入)
 *          - DO → PB2 (GPIO 输入, 数字报警输出)
 *
 *          输出特性 (3.3V 供电):
 *          干燥:   AO 接近 3.3V (ADC 接近 4095)
 *          有雨:   AO 下降, 雨水越多电压越低
 */
#ifndef __RAIN_H
#define __RAIN_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                  引脚配置宏 (用户可在此重定义)                    *
 *==================================================================*/
#define RAIN_ADC_PORT       GPIOB
#define RAIN_ADC_PIN        GPIO_Pin_1      /* PB1 = ADC1_IN9 (AO)   */
#define RAIN_ADC_CHANNEL    ADC_Channel_9

#define RAIN_DOUT_PORT      GPIOB
#define RAIN_DOUT_PIN       GPIO_Pin_2      /* PB2 = DO (数字输出)   */

/*==================================================================*
 *                  数字输出极性 (根据实际模块调整)                  *
 *==================================================================*/
/*
 * 常见雨滴模块 DO 在检测到雨水时输出低电平 (LM393 同相输出)。
 * 若你的模块是高电平触发, 请将本宏改为 0。
 */
#define RAIN_DOUT_ACTIVE_LOW  1   /* 1=低电平表示有雨, 0=高电平表示有雨 */

/*==================================================================*
 *                      传感器状态定义                               *
 *==================================================================*/
#define RAIN_NORMAL         0               /* 无雨/小雨             */
#define RAIN_DETECTED       1               /* 检测到雨水            */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   初始化雨滴传感器
 * @note    配置 ADC1 通道 9 (PB1) 与 DO 引脚 (PB2)。
 *          ADC1 与 MQ4(PA0/CH0)、土壤湿度(PB0/CH8)共用,
 *          每次读取前自动选择本通道。
 */
void RAIN_Init(void);

/**
 * @brief   读取 ADC 原始值
 * @return  12 位 ADC 值 (0~4095), 无雨时偏大, 有雨时偏小
 */
uint16_t RAIN_ReadADC(void);

/**
 * @brief   读取 ADC 引脚电压
 * @return  电压 (V), 范围 0~3.3V
 */
float RAIN_ReadVoltage(void);

/**
 * @brief   读取雨量百分比
 * @return  0~100, 0=干燥, 100=完全浸湿
 * @note    根据 ADC 反算: 电压越低雨量越大。
 */
uint8_t RAIN_ReadPercent(void);

/**
 * @brief   读取数字报警输出 (DO)
 * @return  RAIN_DETECTED (1): 检测到雨
 *          RAIN_NORMAL (0):   无雨
 */
uint8_t RAIN_ReadAlarm(void);

#ifdef __cplusplus
}
#endif

#endif
