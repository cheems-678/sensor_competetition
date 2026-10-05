/**
 * @file    bkp.h
 * @brief   STM32F10x 后备寄存器与侵入检测（Tamper Detection）驱动接口
 *
 * 本头文件提供侵入检测（Tamper）功能的初始化接口。
 * 侵入检测用于安全敏感场景（如防拆机、防篡改）：
 * - 当 TAMPER 引脚（通常为 PC13）检测到有效电平跳变时，
 *   硬件自动清除所有后备寄存器（BKP_DRx）和 RTC 配置；
 * - 可选配置中断通知 CPU（默认已启用，见 bkp.c）。
 *
 * @note
 * - 后备寄存器仅在 VBAT 供电或系统复位时保持数据，侵入事件会强制清零；
 * - 初始化后无需轮询，事件由硬件自动处理；
 * - 如需自定义中断响应，请参考 TAMPER_IRQHandler() 实现；
 * - 默认 TAMPER 引脚为低电平有效（可在 TAMPER_Init 中修改）。
 *
 * @warning
 * - 调用 TAMPER_Init() 前确保系统时钟已初始化；
 * - 侵入事件不可逆，请勿在关键数据未备份时依赖后备寄存器长期存储。
 */
#ifndef __BKP_H
#define __BKP_H

#include "sys.h"
#include "stm32f10x_conf.h"
#include "stm32f10x_bkp.h"

// 注意：stm32f10x_conf.h 通常由用户工程统一包含，此处可省略

/**
 * @brief 初始化侵入检测（Tamper Detection）功能
 * @details
 * - 使能 PWR 和 BKP 时钟；
 * - 允许访问后备寄存器区域；
 * - 配置 TAMPER 引脚为低电平有效；
 * - 清除历史侵入标志；
 * - 使能 TAMPER 引脚检测（并可选使能中断，见 bkp.c 实现）。
 * @note
 * - 默认使用芯片的 TAMPER 引脚（通常为 PC13）；
 * - 一旦触发侵入事件，所有 BKP_DRx 寄存器将被硬件清零；
 * - 中断处理逻辑位于 bkp.c 的 TAMPER_IRQHandler() 中。
 */
void TAMPER_Init(void);

#endif // __BKP_H

