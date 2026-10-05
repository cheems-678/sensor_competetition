/**
 * @file    oled_iic.h
 * @brief   基于硬件 I²C 接口的 SSD1306 OLED 显示驱动头文件
 * @version V3.5.0
 *
 * 本头文件定义了通过 GPIO 模拟 I²C（软件 I²C）驱动 128×64 OLED 屏幕所需的
 * 宏定义、引脚操作、显示模式及函数声明。
 *
 * @note
 * - 实际使用的是 **软件模拟 I²C**（尽管标题称“硬件 IIC”，但接线为普通 GPIO）
 * - 引脚分配：SCL → PB6，SDA → PB7
 * - 屏幕分辨率：128 (宽) × 64 (高)，采用页地址模式（Page Addressing Mode）
 * - 汉字取模格式：阴码、列行式、逆向（适用于 SSD1306 内存布局）
 */

#ifndef __OLED_IIC_H
#define	__OLED_IIC_H
#include "sys.h"
#include "stdlib.h"	    	


/*------------------ 全局常量定义 ------------------*/

/**
 * @brief OLED 屏幕最大列数（宽度）
 * @details 用于自动换行判断，通常为 128
 */
#define Max_Column	128

				   
/*------------------ I²C 引脚操作宏 ------------------*/

/**
 * @name I²C 时钟线 (SCL) 控制宏
 * @{
 */
#define OLED_SCLK_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_6)//SCL
#define OLED_SCLK_Set() GPIO_SetBits(GPIOB,GPIO_Pin_6)

/** @} */

/**
 * @name I²C 数据线 (SDA) 控制宏
 * @{
 */
#define OLED_SDIN_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_7)//SDA
#define OLED_SDIN_Set() GPIO_SetBits(GPIOB,GPIO_Pin_7)

/*------------------ OLED 工作模式定义 ------------------*/

/**
 * @brief OLED 命令模式标识
 * @details 用于区分写入的是命令还是显示数据
 */ 
#define OLED_CMD  0	//命令模式

/**
 * @brief OLED 数据模式标识
 * @details 表示后续字节为显存数据
 */
#define OLED_DATA 1	//数据模式

/*------------------ 函数声明 ------------------*/

/**
 * @defgroup OLED_IIC_Driver OLED 软件 I²C 驱动接口
 * @{
 * 
 * 提供完整的 OLED 初始化、控制、文本/图形绘制功能。
 * 所有坐标系约定：
 * - x ∈ [0, 127]：列地址（水平方向）
 * - y ∈ [0, 7]  ：页地址（垂直方向，每页 8 像素高）
 */

/*------ I²C 底层通信 ------*/
void IIC_Start();       ///< 发送 I²C 起始信号
void IIC_Stop();        ///< 发送 I²C 停止信号
void IIC_Wait_Ack();    ///< 等待从设备 ACK（SSD1306 实际无 ACK，仅占位）
void Write_IIC_Command(unsigned char IIC_Command);      ///< 写入一个字节数据（MSB 优先）
void Write_IIC_Data(unsigned char IIC_Data);            ///< 向 OLED 写入命令
void Write_IIC_Byte(unsigned char IIC_Byte);            ///< 向 OLED 写入显示数据
void IIC_OLED_WR_Byte(uint8_t dat,uint8_t cmd);          ///< 统一写入接口（cmd=0:命令, cmd=1:数据）
void IIC_OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t);     ///绘制单个像素点

/*------ OLED 控制与初始化 ------*/
void IIC_OLED_Init(void);                          ///< 初始化 OLED（GPIO + SSD1306 寄存器配置）
void IIC_OLED_Clear(void);                         ///< 清空整个屏幕（填充 0）
void IIC_OLED_Display_On(void);                    ///< 开启显示（退出休眠）
void IIC_OLED_Display_Off(void);                   ///< 关闭显示（进入低功耗休眠）

void IIC_OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr);         ///< 显示单个 ASCII 字符（16×8）
void IIC_OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len);    ///< 显示整数（右对齐）
void IIC_OLED_ShowString(uint8_t x,uint8_t y, char *p);	        ///< 显示 ASCII 字符串
void IIC_OLED_Set_Pos(unsigned char x, unsigned char y);        ///< 设置显存写入起始位置（x: 列, y: 页）
void IIC_OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no);      ///< 显示预定义汉字（Hzk 数组索引）
void IIC_OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[]);   ///< 显示位图（指定区域）
void IIC_OLED_DrawFont16(uint16_t x, uint16_t y, char *s);      ///< 显示 16×16 中文
void IIC_OLED_DrawFont32(uint16_t x, uint16_t y, char *s);      ///< 显示 32×32 中文
void IIC_OLED_Show_Str(uint16_t x, uint16_t y, char *str,uint8_t size);     ///< 混合显示中英文
/** @} */ // end of OLED_IIC_Driver

#endif /* __OLED_IIC_H */ 
