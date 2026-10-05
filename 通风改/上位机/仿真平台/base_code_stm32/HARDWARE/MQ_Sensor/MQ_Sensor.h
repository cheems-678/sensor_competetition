/**
 ******************************************************************************
 * @file    MQ_Sensor.h
 * @brief   MQ 系列气体传感器驱动头文件
 *
 * 声明多种 MQ 气体传感器（MQ-8, MQ-135, MQ-136/137/138）的数据读取接口。
 * 所有函数返回传感器分压输出的模拟电压值（单位：V），内部可选打印估算的 ppm 浓度。
 *
 * @note    - 传感器需预热 >60 秒后数据才稳定
 *          - 实际气体浓度受温度、湿度、交叉敏感性影响，ppm 仅为参考值
 *          - 所有传感器共用 ADC 通道 1（由 \c Get_Adc(1) 读取）
 ******************************************************************************
 */
#ifndef __MQ_SENSOR_H
#define __MQ_SENSOR_H
#include <math.h>
#include <stdio.h>

#include "sys.h"
#include "adc.h"
#include "delay.h"

/**
 * @defgroup MQ_Sensor_API MQ 系列传感器数据采集 API
 * @{
 * @brief 提供五种常见 MQ 传感器的电压读取接口
 *
 * 所有函数均执行以下操作：
 * 1. 延时 20ms 稳定信号
 * 2. 从 ADC 通道 1 读取原始值
 * 3. 转换为 0～3.3V 的模拟电压
 * 4. （可选）通过串口打印估算的 ppm 浓度（仅调试用）
 * 5. 返回电压值（单位：V）
 *
 * @warning 返回值是 **电压**，不是 ppm。若需 ppm，请在应用层调用对应换算逻辑，
 *          或扩展本模块提供专用 ppm 接口。
 */

/**
 * @brief 获取 MQ-8 传感器输出电压（用于检测氢气 H₂）
 * @return float 传感器分压输出电压（范围约 0.0 ～ 3.3 V）
 * @see GetMQ8Out
 */
float GetMQ8Out(void);

/**
 * @brief 获取 MQ-135 传感器输出电压（用于空气质量监测，如 CO₂、NH₃ 等）
 * @return float 传感器分压输出电压（范围约 0.0 ～ 3.3 V）
 * @see GetMQ135Out
 */
float GetMQ135Out(void);

/**
 * @brief 获取 MQ-136 传感器输出电压（用于检测 H₂S、NH₃、丙酮等）
 * @return float 传感器分压输出电压（范围约 0.0 ～ 3.3 V）
 * @see GetMQ136Out
 */
float GetMQ136Out(void);

/**
 * @brief 获取 MQ-137 传感器输出电压（用于检测 H₂S、NH₃、丙酮等）
 * @return float 传感器分压输出电压（范围约 0.0 ～ 3.3 V）
 * @see GetMQ137Out
 */
float GetMQ137Out(void);

/**
 * @brief 获取 MQ-138 传感器输出电压（用于检测 H₂S、NH₃、丙酮等）
 * @return float 传感器分压输出电压（范围约 0.0 ～ 3.3 V）
 * @see GetMQ138Out
 */
float GetMQ138Out(void);

/** @} */ // end of MQ_Sensor_API

#endif /* __MQ_SENSOR_H */