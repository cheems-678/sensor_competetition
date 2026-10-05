/**
 * @file    pwm.c
 * @brief   STM32F10x 定时器3（TIM3）四通道 PWM 输出驱动
 *
 * 本文件配置 TIM3 生成 4 路 PWM 信号，输出到以下引脚：
 * - CH1 → PA6
 * - CH2 → PA7
 * - CH3 → PB0
 * - CH4 → PB1
 *
 * @note
 * - 使用 **PWM 模式 2**（TIM_OCMode_PWM2）：  
 *   当计数器 < CCR 时输出 **低电平**，否则输出 **高电平**（即占空比 = (ARR - CCR) / ARR）；
 *   若需常规 PWM（CCR 越大占空比越高），请改用 TIM_OCMode_PWM1；
 * - 定时器配置：
 *   - 预分频器（PSC）= 72 → 计数频率 = 72MHz / (72+1) ≈ 986.3 kHz
 *   - 自动重装载值（ARR）= 256 → PWM 周期 ≈ 256 / 986.3kHz ≈ **260 μs**（约 3.85 kHz）
 * - 所有通道默认占空比为 0%（因 CCR 初始为 0）；
 * - 用户可通过 `TIM_SetCompareX(TIM3, value)` 动态调整占空比。
 *
 * @warning
 * - PA6/PA7/PB0/PB1 必须配置为复用推挽输出（AF_PP）；
 * - AFIO 时钟必须使能（用于重映射，但本例使用默认映射）；
 * - 当前未启用 TIM3 更新中断（如需周期回调，请取消注释相关代码）。
 */
#include "sys.h"
#include "pwm.h"
#include "stm32f10x_conf.h"


/*------------------ PWM 初始化 ------------------*/

/**
 * @brief 初始化 TIM3 为四通道 PWM 输出（PA6/PA7/PB0/PB1）
 * @details
 * - 配置 GPIO 为复用推挽输出（50 MHz）
 * - 设置 TIM3 时基：PSC=72, ARR=256 → ～3.85 kHz PWM
 * - 使用 PWM 模式 2（高电平有效逻辑需注意）
 * - 使能自动重装载预装载（ARR 和 CCR 寄存器缓冲）
 * @note
 * - 调用后所有通道输出低电平（CCR=0）；
 * - 占空比通过 TIM_SetCompare1～4() 动态设置；
 * - 示例：TIM_SetCompare1(TIM3, 128); // CH1 占空比 ≈ 50%（模式2下）
 */
void PWM_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
    TIM_OCInitTypeDef  TIM_OCInitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;  

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB  | RCC_APB2Periph_AFIO, ENABLE); 
	                                                                    
         
    // 初始化 GPIO   PA6 PA7 PB0 PB1  为复用推挽
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6; //选择对应的引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure); //初始化端口   

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;   //选择对应的引脚   
    GPIO_Init(GPIOA, &GPIO_InitStructure);      //初始化端口 

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;   //选择对应的引脚   
    GPIO_Init(GPIOB, &GPIO_InitStructure);      //初始化端口

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;   //选择对应的引脚   
    GPIO_Init(GPIOB, &GPIO_InitStructure);      //初始化端口   

    // 初始化TIM3
	TIM_TimeBaseStructure.TIM_Period = 256; //设置自动重装值
	TIM_TimeBaseStructure.TIM_Prescaler =72; //设置定时器预分频 
	TIM_TimeBaseStructure.TIM_ClockDivision = 0; //时钟分割
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;//上升计数模式
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure); //初始化定时器
	
    // 允许TIM3中断
    // TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE );
    
    // 初始化 TIM3 channel1、2、3、4 为 PWM 模式
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2; //选择 PWM 模式2
 	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; //使能比较输出
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; //设置输出极性
	TIM_OC1Init(TIM3, &TIM_OCInitStructure);  //初始化 PWM
    TIM_OC2Init(TIM3, &TIM_OCInitStructure);  //初始化 PWM
    TIM_OC3Init(TIM3, &TIM_OCInitStructure);  //初始化 PWM
    TIM_OC4Init(TIM3, &TIM_OCInitStructure);  //初始化 PWM

    // 使能 TIM3 在 CCR2 上的预装载寄存器
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
    
    TIM_ARRPreloadConfig(TIM3, ENABLE);

    // 使能定时器
    TIM_Cmd(TIM3, ENABLE);  //
    
}

