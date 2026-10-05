/**
 * @file    lcd.h
 * @brief   LCD 显示屏驱动头文件
 * @details 包含 ILI9xxx/ILI93xx 等多种控制器的驱动接口定义
 * @version V1.0
 */

#ifndef __LCD_H
#define __LCD_H		
#include "sys.h"	 
#include "stdlib.h"


/**
 * @defgroup LCD_Config LCD 配置与定义
 * @{
 */

/**
 * @name 屏幕显示方向与 IO 速度配置
 * @{
 */
/** @brief 是否使用横屏 (0=竖屏, 1=横屏) */
#define USE_HORIZONTAL  0
/** @brief 是否使用快速 IO (0=普通IO, 1=快速IO；快速IO 刷屏速率可达 28fps) */
#define LCD_FAST_IO     0
/** @} */


 
/**
 * @name 全局变量与屏幕尺寸
 * @{
 */
/** @brief 画笔/前景色 */
extern u16  POINT_COLOR;
/** @brief 背景色 */
extern u16  BACK_COLOR;

/** @brief LCD 屏幕宽度 (单位: 像素) */
#if USE_HORIZONTAL==1	
#define LCD_W 320
#else
#define LCD_W 240
#endif

/** @brief LCD 屏幕高度 (单位: 像素) */
#if USE_HORIZONTAL==1
#define LCD_H 240
#else
#define LCD_H 320
#endif
/** @} */
/**
 * @name GPIO 端口定义
 * @details 根据 LCD_FAST_IO 宏定义自动选择快速 IO 或普通 IO
 * @{
 */
#if LCD_FAST_IO==1
/** @brief 芯片片选信号设置 (PA4, 快速 IO) */
#define	LCD_CS_SET  GPIOA->BSRR=1<<4
/** @brief 数据/命令信号设置 (PA1, 快速 IO) */
#define	LCD_RS_SET	GPIOA->BSRR=1<<1
/** @brief 写信号设置 (PA15, 快速 IO) */
#define	LCD_WR_SET	GPIOA->BSRR=1<<15
/** @brief 读信号设置 (PA0, 快速 IO) */
#define	LCD_RD_SET	GPIOA->BSRR=1<<0
/** @brief 芯片片选信号清除 (PA4) */
#define	LCD_CS_CLR  GPIOA->BRR=1<<4
/** @brief 数据/命令信号清除 (PA1) */
#define	LCD_RS_CLR	GPIOA->BRR=1<<1
/** @brief 写信号清除 (PA15) */
#define	LCD_WR_CLR	GPIOA->BRR=1<<15
/** @brief 读信号清除 (PA0) */
#define	LCD_RD_CLR	GPIOA->BRR=1<<0
#else
/** @brief 芯片片选信号 (PA4, 普通 IO) */
#define	LCD_CS	PAout(4)
/** @brief 数据/命令信号 (PA1, 普通 IO) */
#define	LCD_RS	PAout(1)
/** @brief 写信号 (PA15, 普通 IO) */
#define	LCD_WR	PAout(15)
/** @brief 读信号 (PA0, 普通 IO) */
#define	LCD_RD	PAout(0)
#endif
/** @brief 数据线输出 (GPIOB 0~15) */
#define DATAOUT(x) GPIOB->ODR=x;
/** @brief 数据线输入 (GPIOB 0~15) */
#define DATAIN     GPIOB->IDR;
/** @} */
/**
 * @name LCD 扫描方向定义
 * @{
 */
/** @brief 从左到右，从上到下 */
#define L2R_U2D  0
/** @brief 从左到右，从下到上 */
#define L2R_D2U  1
/** @brief 从右到左，从上到下 */
#define R2L_U2D  2
/** @brief 从右到左，从下到上 */
#define R2L_D2U  3
/** @brief 从上到下，从左到右 */
#define U2D_L2R  4
/** @brief 从上到下，从右到左 */
#define U2D_R2L  5
/** @brief 从下到上，从左到右 */
#define D2U_L2R  6
/** @brief 从下到上，从右到左 */
#define D2U_R2L  7
/** @brief 默认扫描方向 */
#define DFT_SCAN_DIR  L2R_U2D
/** @} */

/**
 * @name LCD 颜色定义 (16-bit RGB565 格式)
 * @{
 */
/** @brief 白色 */
#define WHITE         	 0xFFFF
/** @brief 黑色 */
#define BLACK         	 0x0000
/** @brief 蓝色 */
#define BLUE         	 0x001F
/** @brief 淡红色 */
#define BRED             0XF81F
/** @brief 绿红色 */
#define GRED 			 0XFFE0
/** @brief 绿蓝色 */
#define GBLUE			 0X07FF
/** @brief 红色 */
#define RED           	 0xF800
/** @brief 品红/洋红 */
#define MAGENTA       	 0xF81F
/** @brief 绿色 */
#define GREEN         	 0x07E0
/** @brief 青色 */
#define CYAN          	 0x7FFF
/** @brief 黄色 */
#define YELLOW        	 0xFFE0
/** @brief 棕色 */
#define BROWN 			 0XBC40
/** @brief 棕红色 */
#define BRRED 			 0XFC07
/** @brief 灰色 */
#define GRAY  			 0X8430
/** @brief 深蓝色 */
#define DARKBLUE      	 0X01CF
/** @brief 浅蓝色 */
#define LIGHTBLUE      	 0X7D7C
/** @brief 灰蓝色 */
#define GRAYBLUE       	 0X5458
/** @brief 浅绿色 */
#define LIGHTGREEN     	 0X841F
/** @brief 浅灰色 (窗体背景色) */
#define LGRAY 			 0XC618
/** @brief 浅灰蓝色 (中间层颜色) */
#define LGRAYBLUE        0XA651
/** @brief 浅棕蓝色 (选择条目的反色) */
#define LBBLUE           0X2B12
/** @} */
/** @} */

/**
 * @defgroup LCD_API LCD 接口函数
 * @{
 */

/**
 * @name LCD 初始化与显示控制
 * @{
 */
/** @brief 初始化 LCD 控制器及相关硬件 */
void LCD_Init(void);
/** @brief 开启 LCD 显示 */
void LCD_DisplayOn(void);
/** @brief 关闭 LCD 显示 */
void LCD_DisplayOff(void);
/** @} */

/**
 * @name LCD 光标与扫描方向
 * @{
 */
/** @brief 清屏并填充指定颜色 */
void LCD_Clear(u16 Color);	 
/** @brief 设置光标位置（用于下一次写 GRAM 的起始坐标） */
void LCD_SetCursor(u16 Xpos, u16 Ypos);
/** @brief 设置 LCD 扫描方向 */
void LCD_Scan_Dir(u8 dir);
/** @} */

/**
 * @name LCD 像素读写与基本绘图
 * @{
 */
/** @brief 在坐标 (x,y) 处画一个像素（使用全局 POINT_COLOR） */
void LCD_DrawPoint(u16 x,u16 y);
/** @brief 读取坐标 (x,y) 处的颜色值 */
u16  LCD_ReadPoint(u16 x,u16 y);
/** @brief 绘制圆（Bresenham 算法） */
void Draw_Circle(u16 x0,u16 y0,u8 r);
/** @brief 绘制直线（Bresenham 算法） */
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2);
/** @brief 绘制矩形边框 */
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2);
/** @brief 在指定矩形区域内填充指定颜色 */
void LCD_Fill(u16 sx,u16 sy,u16 ex,u16 ey,u16 color);
/** @} */

/**
 * @name LCD 文字与图像显示
 * @{
 */
/** @brief 在指定位置显示单个字符 */
void LCD_ShowChar(u16 x,u16 y,u8 num,u8 size,u8 mode);
/** @brief 显示无符号整数（省略前导零） */
void LCD_ShowNum(u16 x,u16 y,u32 num,u8 len,u8 size);
/** @brief 显示固定位数数字（包含前导零） */
void LCD_Show2Num(u16 x,u16 y,u16 num,u8 len,u8 size,u8 mode);
/** @brief 以 16 字体显示 null 结尾的字符串 */
void LCD_ShowString(u16 x,u16 y,const char *p);
/** @brief 显示 16 位真彩色图片 */
void LCD_ShowPicture(u16 StartX, u16 StartY, u16 EndX, u16 EndY, const char *pic);
/** @} */

/**
 * @name LCD 寄存器读写与 GRAM 操作
 * @{
 */
/** @brief 从 LCD 读取 16 位数据 */
u16 LCD_RD_DATA(void);
/** @brief 向指定寄存器写入 16 位数据 */
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue);
/** @brief 读取指定寄存器的 16 位值 */
u16 LCD_ReadReg(u8 LCD_Reg);
/** @brief 准备写入 GRAM 命令 */
void LCD_WriteRAM_Prepare(void);
/** @brief 向 GRAM 写入一个像素的颜色值 */
void LCD_WriteRAM(u16 RGB_Code);
/** @brief 从 GRAM 读取一个像素的颜色值 */
u16 LCD_ReadRAM(void);
/** @brief 将 BGR 格式颜色值转换为 RGB 格式 */
u16 LCD_BGR2RGB(u16 c);
/** @} */

/** @} */

/**
 * @name LCD 数据读写宏定义
 * @{
 */
#if LCD_FAST_IO==1
/** @brief 写入 16 位数据（快速 IO 版本） */
#define LCD_WR_DATA(data){\
LCD_RS_SET;\
LCD_CS_CLR;\
DATAOUT(data);\
LCD_WR_CLR;\
LCD_WR_SET;\
LCD_CS_SET;\
}
#else
/** @brief 写入 16 位数据（普通 IO 版本） */
#define LCD_WR_DATA(data){\
LCD_RS=1;\
LCD_CS=0;\
DATAOUT(data);\
LCD_WR=0;\
LCD_WR=1;\
LCD_CS=1;\
}
#endif
/** @} */

/**
 * @name LCD 寄存器地址定义 (ILI9320/9325 等)
 * @{
 */  
#define R0             0x00
#define R1             0x01
#define R2             0x02
#define R3             0x03
#define R4             0x04
#define R5             0x05
#define R6             0x06
#define R7             0x07
#define R8             0x08
#define R9             0x09
#define R10            0x0A
#define R12            0x0C
#define R13            0x0D
#define R14            0x0E
#define R15            0x0F
#define R16            0x10
#define R17            0x11
#define R18            0x12
#define R19            0x13
#define R20            0x14
#define R21            0x15
#define R22            0x16
#define R23            0x17
#define R24            0x18
#define R25            0x19
#define R26            0x1A
#define R27            0x1B
#define R28            0x1C
#define R29            0x1D
#define R30            0x1E
#define R31            0x1F
#define R32            0x20
#define R33            0x21
#define R34            0x22
#define R36            0x24
#define R37            0x25
#define R40            0x28
#define R41            0x29
#define R43            0x2B
#define R45            0x2D
#define R48            0x30
#define R49            0x31
#define R50            0x32
#define R51            0x33
#define R52            0x34
#define R53            0x35
#define R54            0x36
#define R55            0x37
#define R56            0x38
#define R57            0x39
#define R59            0x3B
#define R60            0x3C
#define R61            0x3D
#define R62            0x3E
#define R63            0x3F
#define R64            0x40
#define R65            0x41
#define R66            0x42
#define R67            0x43
#define R68            0x44
#define R69            0x45
#define R70            0x46
#define R71            0x47
#define R72            0x48
#define R73            0x49
#define R74            0x4A
#define R75            0x4B
#define R76            0x4C
#define R77            0x4D
#define R78            0x4E
#define R79            0x4F
#define R80            0x50
#define R81            0x51
#define R82            0x52
#define R83            0x53
#define R96            0x60
#define R97            0x61
#define R106           0x6A
#define R118           0x76
#define R128           0x80
#define R129           0x81
#define R130           0x82
#define R131           0x83
#define R132           0x84
#define R133           0x85
#define R134           0x86
#define R135           0x87
#define R136           0x88
#define R137           0x89
#define R139           0x8B
#define R140           0x8C
#define R141           0x8D
#define R143           0x8F
#define R144           0x90
#define R145           0x91
#define R146           0x92
#define R147           0x93
#define R148           0x94
#define R149           0x95
#define R150           0x96
#define R151           0x97
#define R152           0x98
#define R153           0x99
#define R154           0x9A
#define R157           0x9D
#define R192           0xC0
#define R193           0xC1
#define R229           0xE5
/** @} */

/** @} */
#endif  
	 
	 



