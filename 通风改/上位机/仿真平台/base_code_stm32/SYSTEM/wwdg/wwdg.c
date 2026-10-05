/**
 * @file    wwdg.c
 * @brief   STM32F10x 窗口看门狗（WWDG）驱动实现
 *
 * 本文件实现窗口看门狗（Window Watchdog, WWDG）的初始化与喂狗功能。
 * WWDG 特点：
 * - 基于递减计数器，超时将触发系统复位；
 * - 具有“窗口”机制：**只能在计数器值介于 [0x40, Window] 之间时喂狗**；
 * - 支持提前唤醒中断（EWI），可在复位前执行紧急操作；
 * - 时钟源为 PCLK1 / 4096 / 2^fprer（最低 4096 分频）。
 *
 * @note
 * - WWDG 适用于需要高可靠性且防止程序“过早”或“过晚”喂狗的场景；
 * - 中断服务函数需用户在外部定义：void WWDG_IRQHandler(void)；
 * - 典型喂狗周期应小于 (T[6:0] - 0x40) × t_wwdg_tick。
 *
 * @warning
 * - 一旦使能 WWDG，**无法关闭**，只能通过复位停止；
 * - 喂狗操作必须在窗口期内完成，否则立即复位；
 * - 中断优先级配置基于 NVIC 分组 2（2 抢占 + 2 子优先级）。
 */
#include "wwdg.h"

/*------------------ WWDG 初始化 ------------------*/

/**
 * @brief 初始化窗口看门狗（WWDG）
 * @param tr     初始计数值 T[6:0]（有效范围：0x40 ～ 0x7F）
 * @param wr     窗口上限值 W[6:0]（必须满足：0x40 ≤ wr < tr）
 * @param fprer  预分频系数（WDGTB[1:0]）：
 *               - 0: /1 → Fwwdg = PCLK1 / 4096
 *               - 1: /2 → Fwwdg = PCLK1 / 8192
 *               - 2: /4 → Fwwdg = PCLK1 / 16384
 *               - 3: /8 → Fwwdg = PCLK1 / 32768
 * @details
 * - WWDG 时钟频率：Fwwdg = PCLK1 / (4096 × 2^fprer)
 * - 最小超时时间 ≈ (tr - 0x40 + 1) / Fwwdg
 * - 示例：PCLK1=36MHz, fprer=2 → Fwwdg≈2.2 kHz → 最小超时≈(0x50-0x40)/2200≈4.5ms
 * @note
 * - 必须先配置 RCC 使能 WWDG 时钟；
 * - 自动清除 EWI 标志并启用中断；
 * - 用户需实现 WWDG_IRQHandler 处理提前唤醒事件。
 */
void WWDG_Init(u8 tr,u8 wr,u32 fprer)
{ 
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);  //   WWDG时钟使能

	WWDG_SetPrescaler(fprer);////设置IWDG预分频值

	WWDG_SetWindowValue(wr);//设置窗口值

	WWDG_Enable(tr&WWDG_CNT);	 //使能看门狗 ,	设置 counter .                  

	WWDG_ClearFlag();//清除提前唤醒中断标志位 

	WWDG_NVIC_Init();//初始化窗口看门狗 NVIC

	WWDG_EnableIT(); //开启窗口看门狗中断
} 

/*------------------ 喂狗函数 ------------------*/

/**
 * @brief 重载 WWDG 计数器（喂狗）
 * @param cnt 新的计数值（建议：0x40 ≤ cnt ≤ 0x7F）
 * @details
 * - 此操作必须在 **窗口期** 内执行（即当前计数值 ∈ [0x40, wr]）；
 * - 若在窗口外调用，将立即触发系统复位；
 * - 通常在 WWDG_IRQHandler 中调用以延长超时。
 * @warning
 * - 喂狗不是“重置”，而是**写入新计数值**，仍会继续递减；
 * - 不要频繁喂狗，否则失去看门狗意义。
 */
void WWDG_Set_Counter(u8 cnt)
{
    WWDG_Enable(cnt);//使能看门狗 ,	设置 counter .	 
}

/*------------------ NVIC 配置 ------------------*/

/**
 * @brief 配置 WWDG 提前唤醒中断（EWI）的 NVIC
 * @details
 * - 中断通道：WWDG_IRQn
 * - 抢占优先级：2
 * - 子优先级：3
 * - 假设系统已调用 NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2)
 * @note
 * - EWI 在计数器递减至 0x40 时触发，**此时仍有一次喂狗机会**；
 * - 若未及时喂狗，下一次递减到 0x3F 将触发系统复位。
 */
void WWDG_NVIC_Init()
{
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = WWDG_IRQn;    //WWDG中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;   //抢占2，子优先级3，组2	
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;	 //抢占2，子优先级3，组2	
    NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE; 
	NVIC_Init(&NVIC_InitStructure);//NVIC初始化
}

