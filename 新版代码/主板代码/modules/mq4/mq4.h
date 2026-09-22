/**
 * @file    mq4.h
 * @brief   MQ-4 甲烷(CH4)/可燃气体传感器模块驱动 (ADC + 数字输出)
 * @note    MQ-4 主要检测甲烷(CH4)、天然气等可燃气体, 亦对
 *          丙烷、丁烷等有一定的响应。模块基于 SnO2 半导体
 *          气敏材料, 气体浓度升高时电导率增大, 输出电压升高。
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          - AOUT → PA0 (ADC1_IN0, 模拟电压, 正比于气体浓度)
 *          - DOUT → PA2 (GPIO 输入, 数字报警输出, 超过阈值=HIGH)
 *          (原库 MQ-2 为 AOUT=PB0 / DOUT=PB1, 已按原理图调整)
 *
 *          接口说明:
 *          - AOUT: 模拟电压输出 (0~3.3V, 正比于气体浓度)
 *          - DOUT: 数字电平输出 (超过阈值输出 HIGH, 阈值可调)
 *
 *          重要提示:
 *          1. MQ-4 模块通常工作在 5V, AOUT 输出 0~5V,
 *             STM32 ADC 只能承受 0~3.3V!
 *             必须在 AOUT 与 PA0 之间串联分压电阻
 *             (例如: AOUT → 10kΩ → PA0 → 10kΩ → GND, 分压至 0~2.5V)
 *          2. 首次使用需通电预热 24~48 小时 (烧机),
 *             日常使用需预热约 1 分钟
 *          3. MQ-4 功耗较高, 发热属正常现象
 */
#ifndef __MQ4_H
#define __MQ4_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                  引脚配置宏 (用户可在此重定义)                    *
 *==================================================================*/
#define MQ4_ADC_PORT        GPIOA           /* 模拟输出端口           */
#define MQ4_ADC_PIN         GPIO_Pin_0      /* 模拟输出引脚 (PA0)    */
#define MQ4_ADC_CHANNEL     ADC_Channel_0   /* ADC1 通道 0           */

#define MQ4_DOUT_PORT       GPIOA           /* 数字输出端口           */
#define MQ4_DOUT_PIN        GPIO_Pin_2      /* 数字输出引脚 (PA2)    */

/*==================================================================*
 *                     ADC 分压比例 (根据实际分压电阻修改)           *
 *==================================================================*/
/* 
 * 分压比计算: Vout = Vin * R2 / (R1 + R2)
 * 例: AOUT → R1(10kΩ) → PA0 → R2(10kΩ) → GND
 *     Vout = Vin * 10k / (10k + 10k) = Vin / 2
 *     即 ADC 电压 = 传感器输出电压的一半
 *     在代码中还原: Vin = ADC_voltage * (R1 + R2) / R2
 *     若 AOUT 已直连 PA0 (未分压), 请将系数改为 1
 */
#define MQ4_VOLTAGE_DIVIDER ((float)(10.0f + 10.0f) / 10.0f)  /* 分压还原系数 */

/*==================================================================*
 *                      传感器状态定义                               *
 *==================================================================*/
#define MQ4_ALARM_NORMAL    0               /* 气体浓度正常 (DOUT=L) */
#define MQ4_ALARM_DETECTED  1               /* 气体浓度超标 (DOUT=H) */

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================*
 *                         API 函数声明                              *
 *==================================================================*/

/**
 * @brief   初始化 MQ-4 传感器
 * @note    完成以下工作:
 *          1. 初始化 ADC1 (通道 0, PA0): 单次采样模式
 *          2. 初始化 DOUT 引脚 (PA2): 浮空输入
 *
 *          注意: ADC1 被多个模拟传感器共用 (MQ4/土壤湿度/雨滴),
 *          每次读取前会自动重新选择对应通道, 可安全共存。
 *
 *          初始化后建议等待 1 分钟让传感器预热。
 */
void MQ4_Init(void);

/**
 * @brief   读取 ADC 原始值
 * @return  ADC 原始值, 范围 0~4095 (12 位)
 * @note    0 = 0V (GND);  4095 = 3.3V (VREF)
 *          不同传感器的基线值不同, 需在清洁空气中实测。
 */
uint16_t MQ4_ReadADC(void);

/**
 * @brief   读取 ADC 引脚电压 (分压后)
 * @return  电压值, 单位: 伏特 (V)
 */
float MQ4_ReadADCVoltage(void);

/**
 * @brief   读取传感器真实输出电压 (还原分压)
 * @return  传感器 AOUT 引脚真实电压, 单位: 伏特 (V)
 * @note    此值越大表示气体浓度越高。
 */
float MQ4_ReadSensorVoltage(void);

/**
 * @brief   读取数字报警输出 (DOUT)
 * @return  MQ4_ALARM_NORMAL (0)  气体浓度正常
 *          MQ4_ALARM_DETECTED (1) 气体浓度超过阈值
 * @note    阈值通过模块上的电位器调节。
 */
uint8_t MQ4_ReadAlarm(void);

#ifdef __cplusplus
}
#endif

#endif
