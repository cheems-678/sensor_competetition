/**
 * @file    exti.c
 * @brief   STM32F10x 外部中断（EXTI）配置：PA0/PA1 上升沿触发
 *
 * 本文件实现将 GPIOA 的 PA0 和 PA1 配置为外部中断输入，用于按键检测。
 * - 触发条件：**上升沿**（适用于按键一端接地，另一端接 PAx，按下时从低→高）
 * - 中断线：EXTI0（PA0）、EXTI1（PA1）
 * - 中断优先级：抢占优先级 2，子优先级分别为 0（EXTI0）和 1（EXTI1）
 *
 * @note
 * - 默认假设按键硬件连接为：PA0/PA1 → 按键 → GND，内部下拉（IPD）使空闲时为低电平；
 * - 按键按下时引脚被拉高，产生上升沿，触发中断；
 * - 若硬件使用上拉电阻（按键接 VCC），应改为下降沿触发（EXTI_Trigger_Falling）；
 * - 当前中断服务函数仅清除标志位，用户需在其中添加实际处理逻辑。
 *
 * @warning
 * - PA0/PA1 必须映射到 EXTI0/1（STM32F10x 中默认 AFIO 映射即为此）；
 * - 若使用其他 GPIO（如 PB0），需额外配置 AFIO_EXTICR 寄存器。
 */
#include "exti.h"

/*------------------ 外部中断初始化 ------------------*/

/**
 * @brief 初始化 PA0 和 PA1 为上升沿触发的外部中断（用于按键检测）
 * @details
 * 1. 配置 PA0/PA1 为 **内部下拉输入（IPD）**；
 * 2. 使能 EXTI0 和 EXTI1，上升沿触发；
 * 3. 配置 NVIC 中断优先级（EXTI0: sub=0, EXTI1: sub=1）；
 * @note
 * - 内部下拉确保无按键时引脚为低电平；
 * - 按键按下 → 引脚被拉高 → 上升沿 → 触发中断；
 * - 若使用外部上拉，请改用 GPIO_Mode_IPU 并切换为下降沿触发。
 */
void EXTI_KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure; 
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1; //选择对应的引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &GPIO_InitStructure);  


    EXTI_InitStructure.EXTI_Line    = EXTI_Line0 | EXTI_Line1;
  	EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;	
  	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
  	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  	EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;		
  	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;	
  	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;		
  	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			
  	NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;		
  	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x02;	
  	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x01;		
  	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			
  	NVIC_Init(&NVIC_InitStructure);
}

/*------------------ 中断服务函数 ------------------*/

/**
 * @brief EXTI0 中断服务函数（对应 PA0）
 * @details
 * - 检测 EXTI_Line0 中断标志；
 * - 清除挂起位；
 * - 用户应在 if 块内添加按键处理逻辑（如去抖、状态更新等）。
 * @note 当前仅清除标志，无实际功能。
 */
void EXTI0_IRQHandler(void)
{
  if (EXTI_GetITStatus(EXTI_Line0) != RESET)
  {
    EXTI_ClearITPendingBit(EXTI_Line0);
  }
}

/**
 * @brief EXTI1 中断服务函数（对应 PA1）
 * @details
 * - 检测 EXTI_Line1 中断标志；
 * - 清除挂起位；
 * - 用户应在 if 块内添加按键处理逻辑。
 * @note 当前仅清除标志，无实际功能。
 */
void EXTI1_IRQHandler(void)
{
  if (EXTI_GetITStatus(EXTI_Line1) != RESET)
  {
    EXTI_ClearITPendingBit(EXTI_Line1);
  }
}