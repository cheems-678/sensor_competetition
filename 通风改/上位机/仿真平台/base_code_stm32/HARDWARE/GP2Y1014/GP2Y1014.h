/**
 ******************************************************************************
 * @file    GP2Y1014.h
 * @brief   GP2Y1014AU0F 粉尘传感器驱动头文件
 *
 * 声明 GP2Y1014AU0F 光学粉尘传感器的采样接口及全局数据变量。
 * - 控制引脚：PA0（低电平点亮内部 LED）
 * - 模拟输出：连接至 ADC（默认通道由 adc.h 配置，通常为 PA1）
 *
 * @note    - 传感器需严格遵循 10ms 采样周期（LED 脉冲宽度 ≈320μs）
 *          - PM2.5 浓度单位为 mg/m³（即 1 mg/m³ = 1000 μg/m³）
 *          - 依赖 delay.h 提供微秒级延时，adc.h 提供 Get_Adc()
 *          - 调试信息通过 printf 输出（需重定向至串口）
 ******************************************************************************
 */
#ifndef __GP2Y1014_H
#define __GP2Y1014_H
#include <math.h>
#include <stdio.h>

#include "sys.h"
#include "adc.h"
#include "delay.h"

void GetGP2Y ( void );

#endif