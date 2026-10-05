/**
 * @file    digtal_led.h
 * @brief   数码管显示驱动头文件
 * @author  
 * @version V1.0
 * @date    
 */

#ifndef _DIGTAL_LED_H_
#define _DIGTAL_LED_H_

#include "sys.h"
#include "stm32f10x_conf.h"

/**
 * @defgroup DIGTAL_LED_Functions DIGTAL_LED函数
 * @{
 */
/**
 * @brief 初始化数码管硬件（GPIO、时钟）
 *
 * 配置用于段选与位选的 GPIO 引脚，并将数码管置于初始安全状态。
 */
void digtal_LED_Init(void);

/**
 * @brief 显示单个 7 段/8 段数码管的数值
 * @param num 要显示的数值（0-15），超出范围将显示 0
 *
 * 该函数直接向段选寄存器写入字形表对应的位模式。
 */
void show_digtal_one_segment(uint32_t num);

/**
 * @brief 四位数码管动态刷新显示函数
 * @param num 要显示的整数值（0-9999），超出范围将循环回 0
 *
 * 本函数设计为在定时器中断或循环中被频繁调用以实现多位动态扫描。
 */
void show_digtal_four_segment(uint32_t num);



/** @} */
#endif
