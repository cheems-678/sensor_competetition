#ifndef __OLEDFONT_H
#define __OLEDFONT_H

#include "stdint.h"

#define GB16_NUM 8	// 16*16点阵	有几个汉字就写几
#define GB32_NUM 8	// 32*32点阵	有几个汉字就写几

/*****************************常用ASCII表 16*8的点阵************************************/
extern const uint8_t ASCII_1608[95][16];
typedef struct 
{
	uint8_t Index[3];	
	uint8_t Msk[32];
}typFNT_GB16; 
typedef struct 
{
	uint8_t Index[3];	
	uint8_t Msk[128];
}typFNT_GB32; 
// 字体取模：宋体，阴码，列行式，逆向（低位在前）、C51格式
extern char Hzk[][32];
//字体取模(16*16点阵)：宋体，阴码，列行式，逆向（低位在前）
extern const typFNT_GB16 Tfont16[GB16_NUM];
//字体取模（32*32点阵：宋体，阴码，列行式，逆向（低位在前）
extern const typFNT_GB32 Tfont32[GB32_NUM];

#endif
