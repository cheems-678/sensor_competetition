/**
 * @file    delay.h
 * @brief   STM32F10x 精确延时函数接口声明
 *
 * 本头文件声明三类阻塞式延时函数：
 * - delay_us()：微秒级高精度延时（基于 SysTick）
 * - delay_ms()：毫秒级高精度延时（基于 SysTick）
 * - delay()：低精度死循环延时（仅用于初始化或调试）
 *
 * @note
 * - 所有函数均为 **阻塞式（Busy-Wait）**，执行期间 CPU 不处理其他任务；
 * - delay_us/ms 使用 SysTick 定时器，**会覆盖其原有配置**；
 * - 若系统已使用 SysTick 作为 OS 节拍（如 FreeRTOS、uC/OS 或 HAL 的 uwTick），
 *   则 **禁止使用 delay_us/ms**，否则将导致系统时间紊乱；
 * - delay() 函数依赖编译器不优化空循环，仅适用于 -O0 且无严格时序要求的场景。
 *
 * @warning
 * - 本延时库适用于裸机（Bare-Metal）开发；
 * - 在 RTOS 或使用 HAL_Delay 的项目中，请改用系统提供的延时接口。
 */

#ifndef __DELAY_H
#define __DELAY_H 			   
#include "sys.h"  

/**
 * @brief 微秒级精确延时（阻塞）
 * @param nus 延时时间，单位：微秒（us），建议范围：1 ～ 1,000,000
 * @details
 * - 基于 SysTick 定时器硬件实现，精度高；
 * - 内部会临时重配置 SysTick，使用后恢复关闭状态；
 * @warning
 *   此函数会干扰任何依赖 SysTick 的系统服务（如 osSystickHandler、HAL_IncTick）。
 */
void delay(uint32_t nus);

/**
 * @brief 毫秒级精确延时（阻塞）
 * @param nms 延时时间，单位：毫秒（ms）
 * @details
 * - 原理同 delay_us，但以 1ms 为单位计数；
 * - 更适合较长延时，减少循环开销；
 * @warning 同 delay_us：不可与操作系统共存。
 */
void delay_ms(uint32_t nms);

/**
 * @brief 极简死循环延时（低精度）
 * @param nus 循环次数（非精确时间单位！）
 * @details
 * - 实际延时取决于 CPU 主频、编译器优化级别；
 * - 必须在编译时禁用优化（-O0）才有效；
 * - 通常用于 Bootloader 或外设初始化阶段；
 * @note 不推荐在应用逻辑中使用。
 */
void delay_us(uint32_t nus);

#endif // __DELAY_H
