/**
 * @file    bmp.h
 * @brief   BMP 图像数据定义头文件
 *
 * 此头文件包含了几个预定义的位图图像（BMP）数据数组，适用于嵌入式设备上的OLED显示器。
 * 每个位图都以无符号字符数组的形式表示，可以直接用于显示。
 */
#ifndef __BMP_H
#define __BMP_H

extern unsigned char BMP1[];

extern unsigned char BMP2[] ;

//*"龙.bmp",0*/
extern unsigned char BMP_LONG[];

/*"国旗.bmp",0*/
extern unsigned char BMP_GUOQI[];

#endif  /* __BMP_H */