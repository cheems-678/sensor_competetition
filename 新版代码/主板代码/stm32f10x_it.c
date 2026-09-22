/**
 * @file    stm32f10x_it.c
 * @brief   中断处理函数 (粮仓声学层析系统)
 * @note    本文件处理:
 *          - SysTick (1ms) 系统节拍, 用于系统延时
 *          - TIM1 更新中断 (100us), 驱动5路舵机软件PWM
 *          其他中断在各自驱动文件中实现:
 *          - LoRa 下行命令 → USART2_IRQHandler (lora.c)
 */

#include "stm32f10x_it.h"

/* 来自 modules/servo/servo.c, 每100us维护一次5路软件PWM */
extern void Servo_PWM_ISR(void);

/* 系统毫秒节拍计数 */
volatile uint32_t g_tick_ms = 0;

/**
 * @brief  SysTick 中断服务函数 (1ms 节拍)
 * @note   每 1ms 触发一次, 累加系统节拍计数
 */
void SysTick_Handler(void)
{
    g_tick_ms++;
}

/**
 * @brief   TIM1 更新中断服务函数 (100us, 舵机软件PWM)
 * @note    由 servo.c 的 Servo_Init() 使能, 优先级高于USART2,
 *          保证50Hz舵机脉冲宽度不受串口中断影响
 */
void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        Servo_PWM_ISR();
    }
}

/**
 * @brief   获取系统节拍
 */
uint32_t Sys_GetTick(void)
{
    return g_tick_ms;
}

/**
 * @brief   基于节拍的延时
 * @param   ms  延时毫秒数
 */
void Sys_Delay(uint32_t ms)
{
    uint32_t start = g_tick_ms;
    while ((g_tick_ms - start) < ms);
}
