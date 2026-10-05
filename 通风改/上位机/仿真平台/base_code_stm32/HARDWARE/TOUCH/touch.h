/**
 * @file    touch.h
 * @brief   电阻式触摸屏驱动接口（基于 ADS7843/ADS7846 芯片）
 *
 * 此文件声明了用于控制电阻式触摸屏的接口函数与数据结构，
 * 包括初始化、坐标读取、中断处理、屏幕校准等功能。
 */
#ifndef __TOUCH_H__
#define __TOUCH_H__
#include "sys.h"

/** @name 按键状态 */
/** @{ */
#define Key_Down 0x01 ///< 触摸按下
#define Key_Up   0x00 ///< 触摸抬起
/** @} */

/**
 * @brief 笔杆状态结构体
 * @details 存储原始与转换后的触摸坐标、按键状态以及校准参数
 */
typedef struct 
{
	u16 X0;//原始坐标
	u16 Y0;
	u16 X; //最终/暂存坐标
	u16 Y;						   	    
	u8  Key_Sta;//笔的状态			  
//触摸屏校准参数
	float xfac;
	float yfac;
	short xoff;
	short yoff;
//新增的参数,当触摸屏的左右上下完全颠倒时需要用到.
//touchtype=0的时候,适合左右为X坐标,上下为Y坐标的TP.
//touchtype=1的时候,适合左右为Y坐标,上下为X坐标的TP.
	u8 touchtype;
}Pen_Holder;	   

extern Pen_Holder Pen_Point; ///< 全局笔状态变量

/**
 * @name 触摸屏硬件连接引脚定义
 * @{
 */   
#define PEN  PAin(11)  //PA11 INT
#define DOUT PAin(6)   //PA6  MISO
#define TDIN PAout(7)  //PA7  MOSI
#define TCLK PAout(5)  //PA5  SCLK
#define TCS  PAout(12) //PA12 CS    
/** @} */

/**
 * @name ADS7843/ADS7846 控制指令集
 * @{
 */
extern u8 CMD_RDX;
extern u8 CMD_RDY;
   											 
#define TEMP_RD	0XF0  //0B11110000即用差分方式读Y坐标    
/** @} */   

/**
 * @brief 初始化触摸屏硬件与中断
 */			  
void Touch_Init(void);		 //初始化

/**
 * @brief 单次读取原始坐标（X, Y），带简单滤波
 * @param x 指向 X 坐标存储地址
 * @param y 指向 Y 坐标存储地址
 * @return 0: 失败；1: 成功
 */
u8 Read_ADS(u16 *x,u16 *y);	 //带舍弃的双方向读取

/**
 * @brief 双次采样校验读取坐标（提高准确性）
 * @param x 指向 X 坐标存储地址
 * @param y 指向 Y 坐标存储地址
 * @return 0: 失败；1: 成功
 * @note 当前实现仅单次采样，若需启用双采样，请修改源码
 */
u8 Read_ADS2(u16 *x,u16 *y); //带加强滤波的双方向坐标读取

/**
 * @brief 读取单轴坐标（带滤波）
 * @param xy 读取指令（CMD_RDX 或 CMD_RDY）
 * @return 滤波后的 ADC 值
 */
u16 ADS_Read_XY(u8 xy);		 //带滤波的坐标读取(单方向)

/**
 * @brief 从 ADS7843 读取 ADC 转换结果
 * @param CMD 读取命令（CMD_RDX/CMD_RDY）
 * @return 12 位有效 ADC 值（范围 0～4095）
 */
u16 ADS_Read_AD(u8 CMD);	 //读取AD转换值

/**
 * @brief 向 ADS7843 写入一个字节数据
 * @param num 要发送的 8 位数据（MSB 先发）
 */
void ADS_Write_Byte(u8 num); //向控制芯片写入一个数据

/**
 * @brief 在 LCD 上绘制校准十字光标
 * @param x 横坐标
 * @param y 纵坐标
 */
void Drow_Touch_Point(u8 x,u16 y);//画一个坐标叫准点

/**
 * @brief 绘制 2×2 像素的大点（用于触摸反馈）
 * @param x 横坐标
 * @param y 纵坐标
 */
void Draw_Big_Point(u8 x,u16 y);  //画一个大点

/**
 * @brief 执行四点触摸屏校准
 */
void Touch_Adjust(void);          //触摸屏校准

/**
 * @brief 使能/禁用 PEN 中断
 * @param en 1: 使能；0: 禁用
 */
void Pen_Int_Set(u8 en); 		  //PEN中断使能/关闭

/**
 * @brief 将原始 ADC 值转换为 LCD 像素坐标
 */
void Convert_Pos(void);

#endif // __TOUCH_H__
