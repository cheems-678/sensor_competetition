/**
 ******************************************************************************
 * @file    chfont.h
 * @brief   中文字体资源外部声明头文件
 *
 * 本文件声明了多个用于 OLED/LCD 显示的中文字模数组。
 * 所有字模均为点阵格式：
 * - 16×16 点阵：每个字符占 16 字节（每列 8 位 × 16 列）
 * - 24×24 点阵：每个字符占 24 字节（每列 8 位 × 24 列）
 *
 * @note    实际数据定义在  chfont.c 中。
 * @attention  本平台中文格式为UTF-8
 ******************************************************************************
 */
#ifndef __CHFONT_H
#define __CHFONT_H	

/**
 * @defgroup FontResources 中文字模资源
 * @{
 * 预定义的中文/符号字模数组，适用于 16x16 或 24x24 点阵显示屏（如 OLED）。
 * 
 * @note 所有数组均为 \c const，存储在 Flash 中，无需占用 RAM。
 */

/**
 * @brief 通用 16×16 点阵中文字库
 * 
 * 包含若干常用中文字符或符号（具体字符需参考 \c chfont.c 中的注释）。
 * 数组大小由编译器自动推断。
 */
extern const u8 tfont16[][16];

/**
 * @brief “温度”相关 16×16 字符（如“温”、“度”、括号等）
 * 
 * 可用于显示“温度(℃)”等文本。
 */
extern const u8 text_temperature[][16];

/**
 * @brief “湿度”相关 16×16 字符（如“湿”、“度”、百分号等）
 * 
 * 可用于显示“湿度(%)”等文本。
 */
extern const u8 text_humidity[][16];

/**
 * @brief “空气质量”相关 16×16 字符（如“空”、“气”、“质”、“量”）
 */
extern const u8 text_airQuality[][16];

/**
 * @brief “气压”相关 16×16 字符（如“气”、“压”、字母 P、括号等）
 * 
 * 可用于显示“气压(Pa)”等文本。
 */
extern const u8 text_airPressure[][16];

/**
 * @brief “海拔”相关 16×16 字符（如“海”、“拔”、字母 m、括号等）
 * 
 * 可用于显示“海拔(m)”等文本。
 */
extern const u8 text_altitude[][16];

/**
 * @brief “GYMCU680” 24×24 点阵字符串
 * 
 * 共 8 个字符：'G', 'Y', 'M', 'C', 'U', '6', '8', '0'
 * 每个字符 24 字节，适合标题显示。
 */
extern const u8 text_GYMCU680[][24];

/**
 * @brief 底部提示文本 1（24×24 点阵）
 * 
 * 内容可能为“返回”、“菜单”等操作提示（具体含义需结合 \c chfont.c）。
 */
extern const u8 text_bottom1[][24];

/**
 * @brief 底部提示文本 2（24×24 点阵）
 * 
 * 内容可能为“上一页”、“下一页”等（具体含义需结合 \c chfont.c）。
 */
extern const u8 text_bottom2[][24];

/** @} */ // end of FontResources group

#endif /* __CHFONT_H */
