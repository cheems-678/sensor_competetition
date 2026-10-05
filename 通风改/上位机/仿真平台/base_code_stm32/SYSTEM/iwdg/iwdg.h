/**
 * @file    iwdg.h
 * @brief   STM32F10x 独立看门狗（IWDG）驱动接口声明
 *
 * 独立看门狗（Independent Watchdog, IWDG）用于在系统异常时自动复位 MCU。
 * - 时钟源：内部低速 RC 振荡器（LSI ≈ 40 kHz，精度 ±10%）
 * - 一旦启动，无法通过软件关闭（只能复位清除）
 * - 必须在超时前定期“喂狗”（重装载计数器），否则触发系统复位
 *
 * @note
 * - IWDG 不依赖系统主时钟（HCLK），即使 CPU 死锁仍可工作；
 * - 超时时间由预分频器（prer）和重装载值（rlr）共同决定；
 * - 适用于防止程序跑飞、死循环等严重故障。
 *
 * @warning
 * - LSI 频率存在较大偏差（典型 40kHz，范围 30～50kHz），超时仅为估算；
 * - 启动后必须在 Tout 时间内调用 IWDG_ReloadCounter() 喂狗；
 * - 调试时注意：IWDG 可能导致仿真器断连后自动复位。
 */
#ifndef __IWDG_H
#define __IWDG_H	    

#include "stm32f10x_iwdg.h"

/**
 * @brief 初始化并启动独立看门狗（IWDG）
 * @param prer 预分频系数，取值范围：0 ～ 7（仅低 3 位有效）  
 *             对应分频因子 = 4 × 2^prer，最大为 256（当 prer = 6）
 * @param rlr  重装载寄存器值，取值范围：0 ～ 2047（低 11 位有效）
 * @details
 * 超时时间估算公式（基于 LSI ≈ 40 kHz）：
 * \f[
 * T_{out} \approx \frac{(4 \times 2^{prer}) \times rlr}{40} \quad \text{[ms]}
 * \f]
 *
 * 常见配置示例：
 * - 1 秒超时：prer=4 (64 分频), rlr=625 → (64×625)/40 ≈ 1000 ms
 * - 2 秒超时：prer=5 (128 分频), rlr=625 → (128×625)/40 ≈ 2000 ms
 *
 * @note
 * - 函数内部自动使能寄存器写权限；
 * - 调用后 IWDG 立即启动，无法停止；
 * - 应用程序必须在主循环中定期调用 IWDG_ReloadCounter() 喂狗。
 */
void IWDG_Init(u8 prer,u16 rlr) ;


#endif

 
