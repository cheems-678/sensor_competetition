/**
 * @file    pt8211.c
 * @brief   PT8211 双通道 16 位 DAC 驱动实现（GPIO 模拟 I²S/PCM 接口）
 *
 * 本文件通过 GPIO 模拟 PT8211 所需的三线制串行音频接口（WS/BCK/DIN），
 * 实现左右声道 16 位有符号音频数据输出。
 *
 * @note
 * - PT8211 是一款双通道 16 位 DAC，常用于音频播放
 * - 接口信号：
 *     - WS (Word Select)：声道选择（0=右声道，1=左声道）
 *     - BCK (Bit Clock)：位时钟，上升沿锁存 DIN 数据
 *     - DIN (Data In)：串行音频数据（MSB 先传）
 * - 数据格式：16 位有符号整数（补码），范围 [-32768, +32767]
 * - 输出电压：Vout = VCC/2 + (data / 65536) * VCC
 * - 引脚分配：
 *     - WS  → PB12
 *     - BCK → PB13
 *     - DIN → PB14
 * - 时序要求：BCK 频率应 ≤ 2 MHz（此处使用 delay_us(1) 确保稳定）
 *
 */
#include "pt8211.h"
#include "delay.h"

/*------------------ PT8211 引脚定义 ------------------*/

/**
 * @brief PT8211 字选择信号（WS）控制宏
 * @details 0 = 右声道，1 = 左声道
 */
#define PT8211_WS 	PBout(12)
/**
 * @brief PT8211 位时钟信号（BCK）控制宏
 */
#define PT8211_BCK  PBout(13)
/**
 * @brief PT8211 串行数据输入（DIN）控制宏
 */
#define PT8211_DIN	PBout(14)

/*------------------ 函数实现 ------------------*/

/**
 * @brief 初始化 PT8211 所需的 GPIO 引脚
 * @details 配置 PB12 (WS)、PB13 (BCK)、PB14 (DIN) 为推挽输出模式，初始电平为高
 */
void PT8211_Init(void)	   
{                 
		GPIO_InitTypeDef  GPIO_InitStructure;

		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	 //使能端口时钟

		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14;				 //PB12 PB13 PB14 端口配置
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 		 //推挽输出
		GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
		GPIO_Init(GPIOB, &GPIO_InitStructure);
		GPIO_SetBits(GPIOB,GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14);						
}

/**
 * @brief 向 PT8211 输出左右声道音频数据
 * @param right_ch 右声道 16 位有符号音频样本（-32768 ～ +32767）
 * @param lift_ch  左声道 16 位有符号音频样本
 *
 * @details
 * - 输入有符号整数自动转换为 16 位补码格式
 * - 数据按 MSB 优先顺序，在 BCK 上升沿锁存
 * - 先发送右声道（WS=0），再发送左声道（WS=1）
 * - 每位之间插入 1μs 延迟以满足时序要求（BCK ≤ 500 kHz）
 *
 * @note
 * - 若输入值超出 16 位范围，高位将被截断（仅取低 16 位）
 * - 负数通过补码转换：`value < 0 ? 0x10000 + value : value`
 * - 此函数为阻塞式，执行期间无法处理其他任务
 */
void PT8211_Output(int16_t right_ch, int16_t lift_ch)
{
		int i = 0;
		//将负数转换为补码
		if(right_ch<0)
				right_ch = 0x10000 + right_ch;
		if(lift_ch<0)
				lift_ch = 0x10000 + lift_ch;
		
		
		PT8211_WS = 0;	//设置为右声道
		for(i = 15; i >=0; i--)
		{
				PT8211_BCK = 0;
				PT8211_DIN = (right_ch >> i)&0x1;
				PT8211_BCK = 1;
				delay_us(1);
		}
		
		PT8211_WS = 1;	//设置为左声道
		for(i = 15; i >=0; i--)
		{
				PT8211_BCK = 0;
				PT8211_DIN = (lift_ch >> i)&0x1;
				PT8211_BCK = 1;
				delay_us(1);
		}
}
