/**
 ******************************************************************************
 * @file    chinese.c
 * @brief   中文点阵字符显示驱动实现
 *
 * 实现了在 LCD 上绘制 16×16 和 24×24 点阵中文字符的功能。
 * 支持自定义前景色与背景色，适用于 OLED、TFT-LCD 等图形屏。
 *
 * @note    字模数据需按列优先、高位在前格式提供（如 PCtoLCD2002 生成）。
 *          每个 16×16 字符由 32 字节组成（2 列/字节 × 16 行），
 *          每个 24×24 字符由 72 字节组成（3 列/字节 × 24 行）。
 * @attention  本平台中文格式为UTF-8
 ******************************************************************************
 */
#include "chinese.h"
#include "lcd.h"	   	  
			    
/**
 * @brief  在 LCD 指定坐标绘制单个像素点
 * @param  x: 横坐标（0 ～ LCD_W - 1）
 * @param  y: 纵坐标（0 ～ LCD_H - 1）
 * @param  color: 像素颜色（RGB565 格式）
 *
 * 临时保存全局前景色 \c POINT_COLOR，绘制后恢复原值，
 * 避免影响其他绘图操作。
 */
void LCD_Draw_Point(u16 x,u16 y,u16 color)
{
	u16 temp;
	temp=POINT_COLOR;
	POINT_COLOR=color;
    LCD_DrawPoint(x,y);
	POINT_COLOR=temp;
}
/**
 * @brief  在 LCD 上显示 16×16 点阵中文字符串
 * @param  x: 起始横坐标
 * @param  y: 起始纵坐标
 * @param  text: 指向字模数组的指针，格式为 \c text[][16]
 * @param  count: 要显示的字符数量
 * @param  color: 前景色（文字颜色）
 *
 * @note   字模组织方式：
 *         - 每个字符占 **32 字节**
 *         - 前 16 字节：text[index*2][0..15]
 *         - 后 16 字节：text[index*2+1][0..15]
 *         - 每字节表示一列的高 8 行或低 8 行（高位在上）
 *
 * @warning 确保 \c x + count*16 <= LCD 宽度，\c y + 16 <= LCD 高度，
 *          否则可能导致越界写屏。
 */
void LCD_ShowChinese16(u16 x, u16 y, const u8 text[][16], u16 count, u16 color)
{   			    
	u8 temp,t,t1;
	u16 y0=y;			
	int index;
	for(index=0;index<count;index++)
	{
		for(t=0;t<32;t++)	// 每个 16x16 字符共 32 字节
		{   
			if(t<16)
				temp=text[index*2][t];      // 前 16 字节（高半部分）
			else 
				temp=text[index*2+1][t-16]; // 后 16 字节（低半部分）                          
			for(t1=0;t1<8;t1++)
			{
				if(temp&0x80)
					LCD_Draw_Point(x,y,color);		// 绘制前景色点
				else 
					LCD_Draw_Point(x,y,BACK_COLOR);  // 绘制背景色点（留白）

				temp<<=1;
				y++;
				if((y-y0)==16)
				{
					y=y0;
					x++;
					break;
				}
			}  	 
		}      
	}	
}

/**
 * @brief  在 LCD 上显示 24×24 点阵中文字符串
 * @param  x: 起始横坐标
 * @param  y: 起始纵坐标
 * @param  text: 指向字模数组的指针，格式为 \c text[][24]
 * @param  count: 要显示的字符数量
 * @param  color: 前景色（文字颜色）
 * @param  back_color: 背景色（非文字区域填充色）
 *
 * @note   字模组织方式：
 *         - 每个字符占 **72 字节**
 *         - 第 0～23 字节：text[index*3][0..23]
 *         - 第 24～47 字节：text[index*3+1][0..23]
 *         - 第 48～71 字节：text[index*3+2][0..23]
 *         - 每字节控制一列中的连续 8 个像素（高位在上）
 *
 * @warning 确保屏幕边界不被越界。24×24 字符占用较大空间。
 */
void LCD_ShowChinese24(u16 x, u16 y, const u8 text[][24], u16 count, u16 color, u16 back_color)
{   			    
	u8 temp,t,t1;
	u16 y0=y;			
	int index;
	for(index=0;index<count;index++)
	{
    for(t=0;t<72;t++)	// 每个 24x24 字符共 72 字节
    {   
		if(t<24)temp=text[index*3][t];           // 第 1 段（0～23）
		else if(t<48)temp=text[index*3+1][t-24]; // 第 2 段（24～47）                     
        else temp=text[index*3+2][t-48];         // 第 3 段（48～71）
	    for(t1=0;t1<8;t1++)
		{
			if(temp&0x80)LCD_Draw_Point(x,y,color); // 前景色
			else LCD_Draw_Point(x,y,back_color);   // 背景色

			temp<<=1;
			y++;
			if((y-y0)==24)
			{
				y=y0;
				x++;
				break;
			}
		}  	 
    }                     
	}	
}
