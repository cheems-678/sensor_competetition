/**
 * @file led.h
 * @brief LED 控制模块头文件（PA12）
 */

#ifndef __LED_H
#define __LED_H
#include "stm32f10x.h"

/** @defgroup LED_Macros LED 宏定义
 *  @{ */

/** @brief 控制 LED 的输出宏（映射到 PA12） */
#define LED PAout(12) /**< PA12 引脚控制 LED */

/** @} */

/** @defgroup LED_Functions LED 函数
 *  @{ */

/**
 * @brief 初始化 PA12 用于控制 LED
 *
 * 初始化 PA12 为推挽输出并设置为初始高电平。
 */
void LED_Init(void);

/** @} */

#endif
