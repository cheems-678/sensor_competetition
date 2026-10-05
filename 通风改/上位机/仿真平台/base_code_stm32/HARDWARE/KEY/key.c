/**
 ******************************************************************************
 * @file    key.c
 * @brief   按键输入驱动实现（支持下拉输入模式）
 *
 * 实现单个或多个独立按键的初始化与扫描功能。
 * 默认示例使用 PA1 作为按键输入引脚，配置为 **下拉输入**：
 * - 按键未按下：PA1 = 低电平（0）
 * - 按键按下：PA1 = 高电平（1，通过外部上拉或 VCC 直连）
 *
 * @note    - 按键需一端接地，另一端接 PA1 和 VCC（或通过上拉电阻接 VCC）
 *          - 扫描函数 \c Key_Scan() 包含简单软件消抖（等待释放）
 *          - 适用于短按检测，不支持长按/连按
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "key.h"
#include "sys.h" 

/**
 * @brief  初始化按键 GPIO 引脚（默认 PA1）
 *
 * 配置 PA1 为下拉输入模式（GPIO_Mode_IPD）：
 * - 内部弱下拉确保无按键时为低电平
 * - 按键按下时，引脚被拉高至 VCC（需外部电路支持）
 *
 * @note   若使用其他引脚或上拉模式，需修改此函数或扩展为参数化版本。
 */
void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    // 开启时钟
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA, ENABLE);
    // 设置 PA1 引脚为下拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1; //选择对应的引脚
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &GPIO_InitStructure);  
}

/**
 * @brief  扫描指定按键状态（带简单消抖）
 * @param  GPIOx: GPIO 端口（如 GPIOA）
 * @param  GPIO_Pin: 引脚号（如 GPIO_Pin_1）
 * @retval KEY_ON (1): 检测到按键按下并已释放（一次有效触发）
 * @retval KEY_OFF (0): 无按键动作
 *
 * 工作流程：
 * 1. 检测引脚是否为高电平（KEY_ON）
 * 2. 若是，则进入等待循环，直到按键释放（防止多次触发）
 * 3. 返回 KEY_ON 表示一次完整按键事件
 *
 * @note   - 此为阻塞式消抖，期间 CPU 无法处理其他任务
 *         - 适用于低频按键操作（如菜单选择）
 *         - 若需非阻塞或高级功能（长按、双击），建议使用定时器中断+状态机
 *
 * @warning 按键必须为“按下=高电平”逻辑。若硬件为“按下=低电平”，
 *          应将 \c KEY_ON 定义为 0，并调整 GPIO 模式为上拉输入。
 */
uint8_t Key_Scan(GPIO_TypeDef* GPIOx,uint16_t GPIO_Pin)
{			
    // 检测按键是否按下（高电平）
	if(GPIO_ReadInputDataBit(GPIOx,GPIO_Pin) == KEY_ON )  
	{	 
        // 等待按键释放（简单消抖 + 防重复触发）
		while(GPIO_ReadInputDataBit(GPIOx,GPIO_Pin) == KEY_ON);   
		return 	KEY_ON;	 
	}
	else
		return KEY_OFF;
}
