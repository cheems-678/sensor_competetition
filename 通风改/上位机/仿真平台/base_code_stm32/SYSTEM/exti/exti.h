/**
 * @file    exti.h
 * @brief   STM32F10x 外部中断（EXTI）驱动接口声明
 *
 * 本头文件声明用于配置 PA0 和 PA1 为外部中断输入的初始化函数。
 * - 中断线：EXTI0（PA0）、EXTI1（PA1）
 * - 触发方式：上升沿（适用于按键一端接地、内部下拉的设计）
 * - 中断服务函数在 exti.c 中定义，用户需在其中添加实际处理逻辑。
 *
 * @note
 * - 默认硬件连接：按键一端接 PA0/PA1，另一端接地；GPIO 配置为内部下拉（IPD）；
 * - 按键按下时引脚从低→高，产生上升沿，触发中断；
 * - 若使用上拉设计（按键接 VCC），应改为下降沿触发，并调整 GPIO 模式为 IP_U；
 * - 中断优先级在 exti.c 中配置（抢占优先级 2，子优先级 0/1）。
 *
 * @warning
 * - 不要重复初始化同一 EXTI 线；
 * - 中断服务函数必须清除挂起位，否则将导致持续中断。
 */
#ifndef __EXTI_H
#define __EXIT_H	 
#include "sys.h"
						    
/**
 * @brief 初始化 PA0 和 PA1 为上升沿触发的外部中断（用于按键检测）
 * @details
 * - 配置 GPIOA 的 Pin0 和 Pin1 为内部下拉输入；
 * - 使能 EXTI0 和 EXTI1，上升沿触发；
 * - 配置 NVIC 中断优先级；
 * @note 调用后，按键按下将触发 EXTI0_IRQHandler 或 EXTI1_IRQHandler。
 */
void EXTI_KEY_Init(void);

/* 
 * 中断服务函数（ISR）由 startup_stm32f10x_xx.s 自动链接，
 * 通常无需在头文件中声明，但保留以下注释供参考：
 */
// void EXTI0_IRQHandler(void);
// void EXTI1_IRQHandler(void);		 
// void EXTI2_IRQHandler(void);
// void EXTI3_IRQHandler(void);
// void EXTI4_IRQHandler(void);
// void EXTI9_5_IRQHandler(void);
// void EXTI15_10_IRQHandler(void);

#endif // __EXTI_H