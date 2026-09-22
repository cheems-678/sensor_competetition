/**
 * @file    stm32f10x_it.c
 * @brief   中断处理函数 (从板 / 传感器节点)
 * @note    本文件只处理 SysTick 1ms 系统节拍 (用于延时)。
 *          从板没有舵机(不需要TIM1中断)、没有PZT成像。
 *          其余中断在各自驱动中实现:
 *          - LoRa 下行命令 → USART2_IRQHandler (modules/lora/lora.c)
 *
 *          节拍变量 g_tick_ms 定义在 modules/systick/systick.c
 */

#include "stm32f10x_it.h"
#include "config.h"

/* 系统毫秒节拍计数 (实现在 modules/systick/systick.c) */
extern volatile uint32_t g_tick_ms;

/**
 * @brief  SysTick 中断服务函数 (1ms 节拍)
 */
void SysTick_Handler(void)
{
    g_tick_ms++;
}
