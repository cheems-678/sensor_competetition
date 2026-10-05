/**
 ******************************************************************************
 * @file    keyboard20.h
 * @brief   4x5 矩阵键盘驱动头文件（20按键）
 *
 * 定义 20 键矩阵键盘的硬件引脚映射、键值编码及功能接口。
 * 默认使用 STM32F1 的以下 GPIO 引脚：
 * - 行线（输出）：
 *     - H1: PB1  (第1行)
 *     - H2: PB0  (第2行)
 *     - H3: PA7  (第3行)
 *     - H4: PA6  (第4行)
 *     - H5: PA5  (第5行)
 * - 列线（输入）：L1~L4 = PA1~PA4（下拉输入）
 *
 * @note    - 按键布局逻辑（5行 × 4列）：
 *            ┌─────┬─────┬─────┬─────┐
 *            │ F1  │ F2  │  #  │  *  │ ← H1 (PB1)
 *            ├─────┼─────┼─────┼─────┤
 *            │  1  │  2  │  3  │ UP  │ ← H2 (PB0)
 *            ├─────┼─────┼─────┼─────┤
 *            │  4  │  5  │  6  │DOWN │ ← H3 (PA7)
 *            ├─────┼─────┼─────┼─────┤
 *            │  7  │  8  │  9  │ Esc │ ← H4 (PA6)
 *            ├─────┼─────┼─────┼─────┤
 *            │LEFT │  0  │RIGHT│ Ent │ ← H5 (PA5)
 *            └─────┴─────┴─────┴─────┘
 *              ↑     ↑     ↑     ↑
 *             L1    L2    L3    L4
 *          - 列线配置为下拉输入，行线输出高电平时可驱动对应列
 *          - 按键按下时，行列导通，列线被拉高（建议列线上接 10kΩ 上拉电阻）
 ******************************************************************************
 */
#ifndef __KEYBOARD20_H
#define __KEYBOARD20_H	 
#include "sys.h"

/**
 * @defgroup KEYBOARD20_GPIO_Macros 矩阵键盘 GPIO 控制宏
 * @{
 * @brief  行输出与列输入引脚定义
 *
 * 行线（H1~H5）用于主动驱动扫描，列线（L1~L4）用于检测按键状态。
 * 实际硬件连接必须与宏定义一致，否则扫描将失效。
 */
#define L1 PAin(1) /* pin1 */
#define L2 PAin(2) /* pin2 */
#define L3 PAin(3) /* pin3 */
#define L4 PAin(4) /* pin4 */

#define H5 PAout(5) /* pin5 */
#define H4 PAout(6) /* pin6 */
#define H3 PAout(7) /* pin7 */
#define H2 PBout(0) /* pin8 */
#define H1 PBout(1) /* pin9 */
/** @} */ // end of KEYBOARD20_GPIO_Macros

/**
 * @defgroup KEYBOARD20_Key_Values 按键键值定义
 * @{
 * @brief  每个按键对应唯一的 8 位标识码（0x01 ~ 0x14）
 *
 * 按键功能从上到下、从左到右依次为：
 * F1, F2, #, *, 1~9, 0, 方向键（UP/DOWN/LEFT/RIGHT）, Esc, Ent。
 * 这些值由 \ref KEYBOARD20_Scan() 返回，并用于 \ref key20_process() 分发处理。
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
#define KEY17_PRES  0x11
#define KEY18_PRES  0x12	
#define KEY19_PRES  0x13
#define KEY20_PRES  0x14	
/** @} */ // end of KEYBOARD20_Key_Values

/**
 * @brief  全局按键标志变量（当前未在 .c 中使用，保留供扩展）
 *
 * @note   当前驱动使用局部静态变量实现消抖，此变量暂未使用。
 *         若需跨模块共享按键状态，可在此基础上扩展。
 */
extern uint8_t key_flag;

/**
 * @defgroup KEYBOARD20_API 矩阵键盘驱动 API
 * @{
 */

/**
 * @brief  初始化 5×4 矩阵键盘的 GPIO 引脚
 *
 * 配置：
 * - 行线（PA5/6/7, PB0/1）为推挽输出（50MHz）
 * - 列线（PA1~PA4）为下拉输入
 *
 * @note   必须在调用 \ref KEYBOARD20_Scan() 前执行一次。
 */
void    KEYBOARD20_Init(void);

/**
 * @brief  扫描矩阵键盘并返回有效按键值
 * @retval 0x01~0x14: 对应 F1/Ent 等20个按键（一次有效触发）
 * @retval 0x00: 无按键动作或按键未释放
 *
 * @note   - 内部实现逐行扫描 + 软件消抖（基于释放检测）
 *         - 不支持多键同时按下（仅返回最先检测到的键）
 *         - 返回值通常赋给全局变量 \c KEY20（在 \c keyboard20.c 中定义）
 */
uint8_t KEYBOARD20_Scan(void);  

/**
 * @brief  处理当前按键事件（默认通过串口打印调试信息）
 *
 * 根据最近一次按键值执行相应操作（如菜单跳转、参数输入等）。
 * 示例中使用 \c printf 输出按键功能名称，实际应用中可替换为业务逻辑。
 *
 * @note   该函数会清零内部按键状态，避免重复处理。
 */
void    key20_process(void);
/** @} */ // end of KEYBOARD20_API
#endif  /* __KEYBOARD20_H */