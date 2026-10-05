/**
 * @file    tim.h
 * @brief   STM32F10x 定时器驱动接口声明：TIM1 与 TIM2 周期中断配置
 *
 * 本头文件声明两个定时器的初始化函数，用于生成周期性更新中断：
 * - **TIM1**：高级定时器，带重复计数器（Repetition Counter），适用于低频高可靠性中断；
 * - **TIM2**：通用定时器，适用于常规高频率周期任务。
 *
 * @note
 * - 系统主频假设为 **72 MHz**；
 * - 两定时器均配置为 **10 kHz 计数频率**（PSC = 7199）；
 * - 基础计数周期 = (ARR + 1) / 10kHz = **1.1 ms**（ARR = 10）；
 * - **TIM1 实际中断周期 = 1.1 ms × (RCR + 1) = 9.9 ms**（因 RCR = 8）；
 * - 中断优先级：抢占优先级 0，子优先级 3（高优先级）。
 *
 * @warning
 * - 使用前必须实现对应的中断服务函数：
 *   - TIM1: `void TIM1_UP_IRQHandler(void)`
 *   - TIM2: `void TIM2_IRQHandler(void)`
 * - 若需精确 1.1 ms 中断，请使用 TIM2；TIM1 因 RCR 机制会延长中断周期。
 */
#ifndef __TIM_H
#define __TIM_H

#include "sys.h"

/**
 * @brief 初始化 TIM1 为周期性更新中断（带重复计数器）
 * @details
 * - 中断周期：**9.9 ms**（ARR=10, PSC=7199, RCR=8）
 * - 中断源：TIM1_UP_IRQn
 * - 优先级：抢占 0，子优先级 3
 * @note
 * - 适用于需要较长中断间隔但避免大 ARR 值的场景；
 * - 用户必须在其他文件中定义 `TIM1_UP_IRQHandler` 中断服务函数。
 */
void TIM1_Init(void);

/**
 * @brief 初始化 TIM2 为周期性更新中断
 * @details
 * - 中断周期：**1.1 ms**（ARR=10, PSC=7199）
 * - 中断源：TIM2_IRQn
 * - 优先级：抢占 0，子优先级 3
 * @note
 * - 适用于高频率周期任务（如状态轮询、LED 闪烁、软件定时等）；
 * - 用户必须在其他文件中定义 `TIM2_IRQHandler` 中断服务函数。
 */
void TIM2_Init(void);

#endif // __TIM_H
