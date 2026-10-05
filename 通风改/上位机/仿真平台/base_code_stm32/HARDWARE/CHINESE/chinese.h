/**
 ******************************************************************************
 * @file    chinese.h
 * @brief   中文点阵显示驱动头文件
 *
 * 声明用于在图形 LCD 上显示 16×16 和 24×24 点阵中文字符的接口函数。
 * 需配合字模数据（如 \c chfont.h 中定义）和底层 LCD 驱动（如 \c lcd.h）使用。
 *
 * @note    字模格式需符合 PCtoLCD2002 工具输出的“列行式、高位在前”标准。
 * @attention  本平台中文格式为UTF-8
 ******************************************************************************
 */
#ifndef __CHINESE_H
#define __CHINESE_H	 
#include "sys.h"   
/**
 * @brief  显示 16×16 点阵中文字符串
 * @param  x: 起始横坐标（像素）
 * @param  y: 起始纵坐标（像素）
 * @param  text: 指向字模数组的指针，类型为 \c const u8[][16]
 * @param  count: 要显示的字符个数
 * @param  color: 文字前景色（RGB565 格式）
 *
 * @note   - 每个字符占用 32 字节（2 行 × 16 字节），按 \c text[index*2] 和 \c text[index*2+1] 组织
 *         - 背景色固定使用全局变量 \c BACK_COLOR
 *         - 适用于 OLED、TFT 等支持像素级操作的显示屏
 *
 * @see    LCD_ShowChinese24() for 24x24 version with custom background color
 */

void LCD_ShowChinese16(u16 x, u16 y, const u8 text[][16], u16 count, u16 color);
/**
 * @brief  显示 24×24 点阵中文字符串（支持自定义背景色）
 * @param  x: 起始横坐标（像素）
 * @param  y: 起始纵坐标（像素）
 * @param  text: 指向字模数组的指针，类型为 \c const u8[][24]
 * @param  count: 要显示的字符个数
 * @param  color: 文字前景色（RGB565）
 * @param  back_color: 背景色（RGB565），用于填充非文字区域
 *
 * @note   - 每个字符占用 72 字节（3 行 × 24 字节），按 \c text[index*3], \c text[index*3+1], \c text[index*3+2] 组织
 *         - 更适合标题、大字号信息显示
 *
 * @warning 确保显示区域不超出 LCD 边界，否则可能导致异常或花屏。
 */
void LCD_ShowChinese24(u16 x, u16 y, const u8 text[][24], u16 count, u16 color, u16 back_color);

#endif /* __CHINESE_H */
