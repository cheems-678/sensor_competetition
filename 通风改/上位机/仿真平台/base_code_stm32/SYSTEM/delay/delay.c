/**
 * @file    delay.c
 * @brief   STM32F10x 精确延时函数实现（基于 SysTick）
 *
 * 本文件提供两种延时实现方式：
 *
 * 1. **高精度延时（推荐）**  
 *    - 使用 SysTick 定时器硬件计数
 *    - 支持微秒（us）和毫秒（ms）级延时
 *    - 不依赖编译器优化级别，精度高
 *    - 注意：此实现会临时占用 SysTick，**不可与操作系统（如 FreeRTOS）共存**
 *
 * 2. **低精度死循环延时（仅用于调试或无 SysTick 场景）**  
 *    - 依赖 CPU 主频和编译器优化
 *    - 必须关闭编译优化（-O0），否则会被优化掉
 *    - 不推荐在正式产品中使用
 *
 * @note
 * - 所有延时函数均为阻塞式（Busy-Wait）
 * - 若系统已使用 SysTick 作为 OS 节拍（如 HAL_Delay），请勿使用本文件的 delay_us/ms
 * - 推荐在裸机（Bare-Metal）项目中使用
 */
#include "delay.h"

/*------------------ 高精度延时（基于 SysTick） ------------------*/

/**
 * @brief 微秒级精确延时（阻塞）
 * @param nus 延时时间，单位：微秒（us）
 * @details
 * - 通过配置 SysTick 为 1us 计数周期实现
 * - 每次调用会重新配置并启动 SysTick
 * - 自动关闭中断和定时器，避免影响系统节拍
 * @warning
 * - 此函数会覆盖 SysTick 的当前配置！
 * - 若系统其他部分依赖 SysTick（如 osSystickHandler），将导致异常
 * - 最大延时受 uint32_t 限制（约 4294 秒，实际受限于系统主频）
 */
void delay_us(uint32_t nus)
{
    // 设置 SysTick 的计数周期
    SysTick_Config(SystemCoreClock/1000000); // 每周期 1us
    // 关闭 SysTick 的中断，不可省略
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    // 开始计数
    for(uint32_t i=0; i<nus; i++)
    {   // 循环一次就是 1us
        while( !((SysTick->CTRL)&(1<<16)) );
    }
    // 关闭 SysTick 定时器
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
}

/**
 * @brief 毫秒级精确延时（阻塞）
 * @param nms 延时时间，单位：毫秒（ms）
 * @details
 * - 原理同 delay_us，但配置为 1ms 周期
 * - 更适合较长延时，减少循环次数
 * @warning 同 delay_us：会覆盖 SysTick 配置，不可与 OS 共存
 */
void delay_ms(uint32_t nms)
{
    // 设置 SysTick 的计数周期
    SysTick_Config(SystemCoreClock/1000); // 每周期 1ms
    // 关闭 SysTick 的中断，不可省略
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
    // 开始计数
    for(uint32_t i=0; i<nms; i++)
    {   // 循环一次就是 1ms
        while( !((SysTick->CTRL)&(1<<16)) );
    }
    // 关闭 SysTick 定时器
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
}

/*------------------ 低精度死循环延时（仅用于调试） ------------------*/

/**
 * @brief 极简死循环延时（单位：大致为若干 CPU 周期）
 * @param nus 循环次数（非精确微秒！）
 * @details
 * - 通过 `__attribute__((optimize("-O0")))` 禁止编译器优化
 * - 实际延时取决于 CPU 主频、编译器、指令流水等
 * - **不推荐用于任何需要精度的场景**
 * @note
 * - 仅用于 Bootloader、初始化阶段或无定时器可用的极端情况
 * - 在 Release 版本中可能因优化失效
 */
__attribute__((optimize("-O0")))							   
void delay(uint32_t nus)
{
	while (nus--);
}

// 使用死循环实现延时
// 注意函数前面防止编译优化的标识不能省略
// __attribute__((optimize("-O0")))							   
// void delay_us(uint32_t nus)
// {
// 	while (nus--)
// 	{
// 		for(uint32_t i = 0; i<10; i++);
// 	}
// }

// 使用死循环实现延时
// 注意函数前面防止编译优化的标识不能省略
// void delay_ms(uint32_t nms)
// {	 		  	  
//     while (nms--)
// 	{
// 		delay_us(1000);
// 	}
// } 


