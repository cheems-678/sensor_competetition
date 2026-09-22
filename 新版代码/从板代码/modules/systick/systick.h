/**
 * @file    systick.h
 * @brief   从板时基/GPIO基础服务 (SysTick 1ms + DWT 微秒计时 + LED)
 */
#ifndef __SYSTICK_H
#define __SYSTICK_H

#include "../../config.h"

/* 系统毫秒节拍: 定义在 systick.c, 由 stm32f10x_it.c 的 SysTick_Handler 累加 */
extern volatile uint32_t g_tick_ms;

/* config.h 已声明 SysTick_Init/Delay_Ms/Delay_Us/Ms/LED_Set, 这里补几个测量接口 */
uint32_t SysTick_UsNow(void);      /* 当前微秒时刻(DWT周期计数换算) */
uint32_t SysTick_RawCycles(void);  /* 当前原始CPU周期计数(DWT_US_CYCCNT) */
uint8_t  SysTick_DwtOk(void);      /* DWT周期计数器是否可用 */

#endif /* __SYSTICK_H */
