/**
 * @file    iwdg.c
 * @brief   STM32F10x 独立看门狗（IWDG）驱动实现
 *
 * 独立看门狗（IWDG）用于在系统死机时自动复位 MCU。
 * - 时钟源：内部低速 RC 振荡器（LSI ≈ 40 kHz，精度 ±10%）
 * - 一旦启动，无法关闭（除非复位）
 * - 必须定期“喂狗”（重装载计数器），否则将触发系统复位
 *
 * @note
 * - IWDG 不依赖系统时钟（HCLK），即使主时钟失效仍可工作；
 * - 超时时间由预分频器（prer）和重装载值（rlr）共同决定；
 * - 典型应用场景：防止程序跑飞、死循环、硬件锁死等。
 *
 * @warning
 * - LSI 频率存在较大偏差（典型值 40kHz，范围 30～50kHz），超时时间仅为估算；
 * - 启动后必须在超时前调用 IWDG_ReloadCounter() 喂狗；
 * - 无法通过软件禁用 IWDG，只能通过复位清除。
 */
#include "iwdg.h"

/*------------------ 独立看门狗初始化 ------------------*/

/**
 * @brief 初始化并启动独立看门狗（IWDG）
 * @param prer 预分频系数，取值范围：0～7（仅低 3 位有效）
 *             分频因子 = 4 × 2^prer，最大为 256（当 prer=6 时）
 * @param rlr  重装载寄存器值，取值范围：0～0x7FF（低 11 位有效，最大 2047）
 * @details
 * 超时时间估算公式（基于 LSI ≈ 40 kHz）：
 * \f[
 * T_{out} \approx \frac{(4 \times 2^{prer}) \times rlr}{40} \quad \text{[ms]}
 * \f]
 *
 * 示例：
 * - prer=4 (分频=64), rlr=625 → Tout ≈ (64 × 625) / 40 = 1000 ms
 * - prer=6 (分频=256), rlr=1250 → Tout ≈ (256 × 1250) / 40 = 8000 ms
 *
 * @note
 * - 函数内部自动使能寄存器写权限；
 * - 初始化后立即启动 IWDG，无法停止；
 * - 应用程序必须在 Tout 时间内定期调用 IWDG_ReloadCounter() 喂狗。
 */
void IWDG_Init(u8 prer,u16 rlr) 
{	
 	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);  //使能对寄存器IWDG_PR和IWDG_RLR的写操作
	
	IWDG_SetPrescaler(prer);  //设置IWDG预分频值:设置IWDG预分频值为64
	
	IWDG_SetReload(rlr);  //设置IWDG重装载值
	
	IWDG_ReloadCounter();  //按照IWDG重装载寄存器的值重装载IWDG计数器
	
	IWDG_Enable();  //使能IWDG
}

