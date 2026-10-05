/**
 * @file led.c
 * @brief LED 控制模块实现（PA12）
 *
 * 本模块提供对板载 LED 的初始化接口，使用 PA12 控制输出。
 */

#include "stm32f10x.h"
#include "led.h"

/**
 * @brief 初始化 PA12 用于控制 LED
 *
 * 将 PA12 配置为推挽输出，速度 50MHz，并将输出设为高电平（默认灭或亮取决于电路设计）。
 */
void LED_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
    
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	GPIO_SetBits(GPIOA,GPIO_Pin_12);
}
 
