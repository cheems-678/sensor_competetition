/**
 * @file    tim.c
 * @brief   STM32F10x 定时器驱动：TIM1（高级）与 TIM2（通用）初始化
 *
 * 本文件实现两个定时器的周期性中断配置：
 * - **TIM1**：高级定时器，带重复计数器（Repetition Counter），用于低频高精度中断；
 * - **TIM2**：32 位通用定时器，用于常规周期中断。
 *
 * @note
 * - 系统主频假设为 72 MHz；
 * - 两定时器均配置为 **10 kHz 计数频率**（PSC = 7199）；
 * - 自动重装载值（ARR）= 10 → 中断周期 = (10 + 1) / 10kHz = **1.1 ms**；
 * - TIM1 额外启用重复计数器（RCR = 8），实际中断周期 = 1.1 ms × (8 + 1) = **9.9 ms**。
 *
 * @warning
 * - TIM1 是高级定时器，其更新事件受 RCR 控制，**每 (RCR+1) 次溢出才触发一次中断**；
 * - 若需精确 1.1 ms 中断，请使用 TIM2 或将 TIM1 的 RCR 设为 0；
 * - 中断优先级相同（抢占=0, 子=3），若同时触发，按向量表顺序处理。
 */
#include "sys.h"
#include "pwm.h"
#include "stm32f10x_conf.h"

/*------------------ TIM1 初始化（高级定时器） ------------------*/

/**
 * @brief 初始化 TIM1 为周期性更新中断（带重复计数器）
 * @details
 * - 时钟源：APB2（72 MHz）
 * - 预分频器 PSC = 7199 → 计数频率 = 72MHz / 7200 = **10 kHz**
 * - 自动重装载值 ARR = 10 → 单次计数周期 = 11 个 tick = **1.1 ms**
 * - 重复计数器 RCR = 8 → **每 9 次溢出触发一次更新中断**（9.9 ms 周期）
 * - 中断通道：TIM1_UP_IRQn
 * - 优先级：抢占 0，子优先级 3（高优先级）
 *
 * @note
 * - 适用于需要较长中断周期但避免大 ARR 值的场景；
 * - 更新中断服务函数需在外部定义：void TIM1_UP_IRQHandler(void);
 */
void TIM1_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE); //时钟使能

	TIM_TimeBaseStructure.TIM_Period = 10; 
	TIM_TimeBaseStructure.TIM_Prescaler =(7200-1);               // 设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率  
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;                 // 设置时钟分割:TDTS = Tck_tim
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  // TIM向上计数模式
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 8;             // 重复计数器，高级定时器更新计数 8 次才中断一次
	                                                             // 可用于超长时间定时中断
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure); //根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位
 
	TIM_ITConfig(      // 使能或者失能指定的TIM中断
		TIM1,          // TIM1
		TIM_IT_Update, // TIM 中断源
		ENABLE         // 使能
		);
	NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;         // TIM1 中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  // 先占优先级0级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;         // 响应优先级3级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            //IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure);  //根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

	TIM_Cmd(TIM1, ENABLE);  //使能TIMx外设
    
}

/*------------------ TIM2 初始化（通用定时器） ------------------*/

/**
 * @brief 初始化 TIM2 为周期性更新中断
 * @details
 * - 时钟源：APB1（72 MHz，STM32F10x 中 APB1 有 x2 倍频 → TIM2CLK = 72 MHz）
 * - 预分频器 PSC = 7199 → 计数频率 = 72MHz / 7200 = **10 kHz**
 * - 自动重装载值 ARR = 10 → 中断周期 = (10 + 1) / 10kHz = **1.1 ms**
 * - 中断通道：TIM2_IRQn
 * - 优先级：抢占 0，子优先级 3（与 TIM1 相同）
 *
 * @note
 * - TIM2 为 32 位定时器（实际在 F10x 中为 16 位，但可级联，此处按 16 位使用）；
 * - 适用于高频率周期任务（如状态轮询、LED 闪烁等）；
 * - 更新中断服务函数需在外部定义：void TIM2_IRQHandler(void);
 */
void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); //时钟使能

	TIM_TimeBaseStructure.TIM_Period = 10; 
	TIM_TimeBaseStructure.TIM_Prescaler =(7200-1); //设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率  
	TIM_TimeBaseStructure.TIM_ClockDivision = 0; //设置时钟分割:TDTS = Tck_tim
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  //TIM向上计数模式
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure); //根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位
 
	TIM_ITConfig(      // 使能或者失能指定的TIM中断
		TIM2,          // TIM2
		TIM_IT_Update, // TIM 中断源
		ENABLE         // 使能
		);
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;  //TIM2中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  // 先占优先级0级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;         // 响应优先级3级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            //IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure);  //根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

	TIM_Cmd(TIM2, ENABLE);  //使能TIMx外设
    
}
