/**
 ******************************************************************************
 * @file    ds1302.h
 * @brief   DS1302 实时时钟（RTC）驱动头文件
 *
 * 声明 DS1302 芯片的寄存器地址、GPIO 控制宏及时间读写接口。
 * 默认使用 STM32F1 的 GPIOB 引脚：
 * - PB6  → RST（复位/片选）
 * - PB10 → SCLK（串行时钟）
 * - PB11 → I/O（双向数据）
 *
 * @note    - 所有时间值以十进制形式传入/返回（内部自动处理 BCD 编码）
 *          - 写入时间前需确保已关闭写保护（驱动内部已处理）
 *          - 寄存器地址定义符合 DS1302 数据手册（命令字节格式）
 ******************************************************************************
 */
#ifndef _DS1302_H
#define _DS1302_H

#include "stm32f10x.h"

/**
 * @defgroup DS1302_Registers DS1302 寄存器地址定义
 * @{
 * DS1302 使用 8 位命令字节，其中低 7 位为寄存器地址，bit0 表示读(1)/写(0)。
 * 以下定义的是**寄存器索引**（实际使用时需左移 1 位并设置 R/W 位）。
 *
 * @note   本驱动在 DS1302_ReadReg / DS1302_WriteReg 中自动完成命令组装。
 */
#define SEC             0x40  ///< 秒寄存器（BCD）
#define MIN             0x41  ///< 分钟寄存器（BCD）
#define HR              0x42  ///< 小时寄存器（24小时制，BCD）
#define DATE            0x43  ///< 日期寄存器（1～31，BCD）
#define MONTH           0x44  ///< 月份寄存器（1～12，BCD）
#define DAY             0x45  ///< 星期寄存器（1=Sunday, ..., 7=Saturday）
#define YEAR            0x46  ///< 年份寄存器（00～99，BCD）
#define CONTROL         0x47  ///< 控制寄存器（bit7=写保护使能）
#define TRACKLE_CHARGER 0x48  ///< 慢充电配置寄存器（用于后备电池充电）
#define CLOCK_BURST     0x5F  ///< 时钟突发模式地址（连续读/写 8 字节时钟数据）
#define RAM0            0x60  ///< 静态 RAM 第 0 字节
#define RAM1            0x61  ///< 静态 RAM 第 1 字节
/** @} */ // end of DS1302_Registers

/**
 * @defgroup DS1302_GPIO_Macros DS1302 GPIO 控制宏（PB6/PB10/PB11）
 * @{
 * 直接操作 STM32 GPIOB 寄存器，实现高速、精确的三线时序控制。
 */
#define RST_HIGH      GPIO_SetBits(GPIOB, GPIO_Pin_6)       ///< 拉高 RST（片选有效）
#define RST_LOW       GPIO_ResetBits(GPIOB, GPIO_Pin_6)     ///< 拉低 RST（结束通信）

#define CLK_HIGH      GPIO_SetBits(GPIOB, GPIO_Pin_10)       ///< 拉高 SCLK
#define CLK_LOW       GPIO_ResetBits(GPIOB, GPIO_Pin_10)     ///< 拉低 SCLK

#define DAT_HIGH      GPIO_SetBits(GPIOB, GPIO_Pin_11)       ///< DAT 输出高电平
#define DAT_LOW       GPIO_ResetBits(GPIOB, GPIO_Pin_11)     ///< DAT 输出低电平

#define DAT           GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11)     ///< 读取 DAT 输入电平

/** @} */ // end of DS1302_GPIO_Macros

/**
 * @defgroup DS1302_API DS1302 驱动 API
 * @{
 */

/**
 * @brief  初始化 DS1302 所需的 GPIO 引脚
 *
 * 配置 PB6（RST）、PB10（SCLK）、PB11（DAT）为推挽输出，并初始化为低电平。
 * 必须在调用其他函数前执行一次。
 */
void DS1302_Init(void);

/**
 * @brief  从 DS1302 指定寄存器读取原始 BCD 数据（内部使用）
 * @param  addr: 寄存器地址（如 \c SEC, \c MIN 等）
 * @param[out] value: 指向接收缓冲区的指针
 *
 * @note   应用层通常应使用 \c DS1302_GetXxx() 系列函数获取十进制时间。
 */
void DS1302_ReadReg(u8 addr, u8 *value);

/**
 * @brief  获取当前年份（00～99）
 * @param[out] year: 返回十进制年份（如 25 表示 2025 年）
 */
void DS1302_GetYear(u8 *year);

/**
 * @brief  获取当前月份（1～12）
 * @param[out] month: 返回十进制月份
 */
void DS1302_GetMonth(u8 *month);

/**
 * @brief  获取当前日期（1～31）
 * @param[out] date: 返回十进制日
 */
void DS1302_GetDate(u8 *date);

/**
 * @brief  获取当前小时（0～23，24 小时制）
 * @param[out] hour: 返回十进制小时
 */
void DS1302_GetHour(u8 *hour);

/**
 * @brief  获取当前分钟（0～59）
 * @param[out] minute: 返回十进制分钟
 *
 */
void DS1302_GetMinite(u8 *minute);

/**
 * @brief  获取当前秒数（0～59）
 * @param[out] second: 返回十进制秒
 */
void DS1302_GetSecond(u8 *second);

/**
 * @brief  设置 DS1302 当前时间
 * @param  yr: 年（00～99，如 25 表示 2025）
 * @param  mon: 月（1～12）
 * @param  date: 日（1～31）
 * @param  hr: 小时（0～23）
 * @param  min: 分钟（0～59）
 * @param  sec: 秒（0～59）
 *
 * @note   - 自动处理 BCD 编码转换
 *         - 内部自动关闭/启用写保护（CONTROL 寄存器）
 *         - 设置完成后时钟自动运行（SEC 寄存器 CH 位清零）
 */
void DS1302_SetTime(u8 yr, u8 mon, u8 date, u8 hr, u8 min, u8 sec);

/** @} */ // end of DS1302_API

#endif  /* _DS1302_H */
