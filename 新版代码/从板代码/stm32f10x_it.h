/**
 * @file    stm32f10x_it.h
 * @brief   中断处理头文件 (智能环境检测系统)
 * @note    包含系统节拍计数与 SysTick 中断声明
 */

#ifndef __STM32F10X_IT_H
#define __STM32F10X_IT_H

#include <stm32f10x.h>
#include <stdint.h>

/* 系统毫秒节拍计数 (由 SysTick_Handler 递增) */
extern volatile uint32_t g_tick_ms;

/* SysTick 中断服务函数 (startup 文件向量表引用) */
void SysTick_Handler(void);

/**
 * @brief   获取系统上电以来经过的毫秒数
 * @return  当前节拍值
 */
uint32_t Sys_GetTick(void);

/**
 * @brief   基于 SysTick 节拍的延时 (非阻塞忙等)
 * @param   ms  延时毫秒数
 * @note    需先调用 SysTick 初始化
 */
void Sys_Delay(uint32_t ms);

#endif /* __STM32F10X_IT_H */
