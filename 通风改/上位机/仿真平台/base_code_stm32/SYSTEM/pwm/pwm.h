/**
 * @file    pwm.h
 * @brief   STM32F10x 定时器3（TIM3）四通道 PWM 输出驱动接口声明
 *
 * 本头文件声明用于初始化 TIM3 生成 4 路 PWM 信号的函数。
 * - 输出引脚（默认复用映射）：
 *   - TIM3_CH1 → PA6
 *   - TIM3_CH2 → PA7
 *   - TIM3_CH3 → PB0
 *   - TIM3_CH4 → PB1
 * - PWM 频率：约 **3.9 kHz**（基于 PSC=71, ARR=255，系统主频 72 MHz）
 * - PWM 模式：**模式 2**（TIM_OCMode_PWM2）
 *   - 当计数器 < CCR 时输出 **低电平**，否则输出 **高电平**
 *   - 占空比 = (ARR + 1 - CCR) / (ARR + 1)
 *
 * @note
 * - 所有通道初始占空比为 0%（CCR = 0）；
 * - 用户需通过标准外设库函数动态设置占空比，例如：
 *   @code
 *   TIM_SetCompare1(TIM3, 128); // 设置 CH1 占空比（模式2下 ≈ 50%）
 *   @endcode
 * - 若需常规 PWM 逻辑（CCR 越大占空比越高），请修改 pwm.c 中的模式为 PWM1。
 *
 * @warning
 * - 使用前确保系统主频为 72 MHz（否则 PWM 频率会偏移）；
 * - 不要直接写 CCR 寄存器，应使用 TIM_SetCompareX() 以确保预装载生效。
 */
#ifndef __PWM_H
#define __PWM_H

#include "sys.h"

/**
 * @brief 初始化 TIM3 为四通道 PWM 输出（PA6/PA7/PB0/PB1）
 * @details
 * - 配置 GPIO 为复用推挽输出（50 MHz）
 * - 设置 TIM3 时基：PSC=71, ARR=255 → PWM 周期 ≈ 256 μs（～3.9 kHz）
 * - 使用 PWM 模式 2（注意输出逻辑）
 * - 使能 ARR 和 CCR 预装载，确保波形更新同步
 * @note
 * - 调用后所有通道输出低电平；
 * - 占空比通过 TIM_SetCompare1～4() 动态调整；
 * - 无需额外使能时钟或 AFIO（已在函数内部处理）。
 */
void PWM_Init(void);

#endif
