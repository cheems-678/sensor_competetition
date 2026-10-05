/**
 ******************************************************************************
 * @file    keyboard16.h
 * @brief   4x4 矩阵键盘驱动头文件
 *
 * 定义 16 键矩阵键盘的硬件引脚映射、键值编码及功能接口。
 * 默认使用 STM32F1 的以下 GPIO 引脚：
 * - 行线（输出）：PB7 (h1), PB6 (h2), PB5 (h3), PA8 (h4)
 * - 列线（输入）：PB15 (l1), PB14 (l2), PB13 (l3), PB12 (l4)
 *
 * @note    - 按键布局逻辑（行×列）：
 *            ┌─────┬─────┬─────┬─────┐
 *            │ K1  │ K2  │ K3  │ K4  │ ← h1 (PB7)
 *            ├─────┼─────┼─────┼─────┤
 *            │ K5  │ K6  │ K7  │ K8  │ ← h2 (PB6)
 *            ├─────┼─────┼─────┼─────┤
 *            │ K9  │ K10 │ K11 │ K12 │ ← h3 (PB5)
 *            ├─────┼─────┼─────┼─────┤
 *            │ K13 │ K14 │ K15 │ K16 │ ← h4 (PA8)
 *            └─────┴─────┴─────┴─────┘
 *              ↑     ↑     ↑     ↑
 *             l1    l2    l3    l4
 *          - 列线需配置为下拉输入，行线输出高电平时可驱动对应列
 *          - 按键按下时，行列导通，列线被拉高（需确保驱动能力或外接上拉）
 ******************************************************************************
 */
#ifndef __KEYBOARD16_H
#define __KEYBOARD16_H	 
#include "sys.h"

/**
 * @defgroup KEYBOARD16_GPIO_Macros 矩阵键盘 GPIO 控制宏
 * @{
 * @brief  行输出与列输入引脚定义
 *
 * 行线（h1~h4）用于主动驱动，列线（l1~l4）用于检测按键状态。
 * 实际硬件连接必须与宏定义一致，否则扫描将失效。
 */
#define h1 PBout(7) /* pin1 */
#define h2 PBout(6) /* pin2 */
#define h3 PBout(5) /* pin3 */
#define h4 PAout(8) /* pin4 */

#define l1 PBin(15) /* pin5 */
#define l2 PBin(14) /* pin6 */
#define l3 PBin(13) /* pin7 */
#define l4 PBin(12) /* pin8 */


/** @} */ // end of KEYBOARD16_GPIO_Macros

/**
 * @defgroup KEYBOARD16_Key_Values 按键键值定义
 * @{
 * @brief  每个按键对应唯一的 8 位标识码（0x01 ~ 0x10）
 *
 * 按键编号从左到右、从上到下依次为 K1 ~ K16。
 * 这些值由 \ref KEYBOARD16_Scan() 返回，并用于 \ref key16_process() 分发处理。
 */
#define KEY1_PRES	0x01
#define KEY2_PRES	0x02
#define KEY3_PRES	0x03		
#define KEY4_PRES	0x04		
#define KEY5_PRES	0x05	
#define KEY6_PRES	0x06	
#define KEY7_PRES	0x07		
#define KEY8_PRES   0x08
#define KEY9_PRES   0x09
#define KEY10_PRES  0x0a	
#define KEY11_PRES  0x0b	
#define KEY12_PRES  0x0c	
#define KEY13_PRES  0x0d			
#define KEY14_PRES  0x0e			
#define KEY15_PRES  0x0f	
#define KEY16_PRES  0x10
/** @} */ // end of KEYBOARD16_Key_Values

/**
 * @defgroup KEYBOARD16_API 矩阵键盘驱动 API
 * @{
 */

/**
 * @brief  初始化矩阵键盘的 GPIO 引脚
 *
 * 配置：
 * - 行线（PB7/PB6/PB5/PA8）为推挽输出（50MHz）
 * - 列线（PB15~PB12）为下拉输入
 *
 * @note   必须在调用 \ref KEYBOARD16_Scan() 前执行一次。
 */
void    KEYBOARD16_Init(void);
/**
 * @brief  扫描矩阵键盘并返回有效按键值
 * @retval 0x01~0x10: 对应 K1~K16 被按下（一次有效触发）
 * @retval 0x00: 无按键动作或按键未释放
 *
 * @note   - 内部实现逐行扫描 + 软件消抖（基于释放检测）
 *         - 不支持多键同时按下（仅返回最先检测到的键）
 *         - 返回值应赋给 \c KEY16 或立即处理
 */
uint8_t KEYBOARD16_Scan(void);  
/**
 * @brief  处理当前按键事件（通常配合串口调试）
 *
 * 根据全局变量 \c KEY16 的值执行相应操作（如打印、菜单跳转等）。
 * 执行完毕后自动清零 \c KEY16。
 *
 * @note   示例中使用 \c printf 输出调试信息，实际应用中可替换为业务逻辑。
 */
void    key16_process(void);
/** @} */ // end of KEYBOARD16_API

#endif  /* __KEYBOARD16_H */