/**
 * @file spi_oled.h
 * @brief SPI 接口 OLED 显示屏驱动头文件
 *
 * 提供 SPI OLED（SSD1306/1106 等）的硬件抽象与驱动 API。
 * 硬件连接：PA5（CLK）、PA7（MOSI）、PB0（DC）、PB1（CS）、PB2（RST）
 */

#ifndef __SPI_OLED_H
#define __SPI_OLED_H
#include "sys.h"
#include "stdlib.h"

//-----------------OLED 端口定义 ----------------

/** @name OLED SPI 线宏定义
 *  @{ */

#define SPI_OLED_SCLK_Clr() GPIO_ResetBits(GPIOA,GPIO_Pin_5) /**< CLK 低电平 */
#define SPI_OLED_SCLK_Set() GPIO_SetBits(GPIOA,GPIO_Pin_5)   /**< CLK 高电平 */

#define SPI_OLED_SDIN_Clr() GPIO_ResetBits(GPIOA,GPIO_Pin_7) /**< DIN 低电平 */
#define SPI_OLED_SDIN_Set() GPIO_SetBits(GPIOA,GPIO_Pin_7)   /**< DIN 高电平 */

/** @} */

/** @name OLED 控制线宏定义
 *  @{ */

#define SPI_OLED_RST_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_2) /**< RES 低电平（复位） */
#define SPI_OLED_RST_Set() GPIO_SetBits(GPIOB,GPIO_Pin_2)   /**< RES 高电平 */

#define SPI_OLED_DC_Clr() GPIO_ResetBits(GPIOB,GPIO_Pin_0)  /**< DC 低电平（命令模式） */
#define SPI_OLED_DC_Set() GPIO_SetBits(GPIOB,GPIO_Pin_0)    /**< DC 高电平（数据模式） */

#define SPI_OLED_CS_Clr()  GPIO_ResetBits(GPIOB,GPIO_Pin_1) /**< CS 低电平（片选） */
#define SPI_OLED_CS_Set()  GPIO_SetBits(GPIOB,GPIO_Pin_1)   /**< CS 高电平 */

/** @} */

/** @name 数据/命令选择
 *  @{ */

#define OLED_CMD  0  /**< 写命令 */
#define OLED_DATA 1  /**< 写数据 */

/** @} */

/** @defgroup SPI_OLED_Functions SPI OLED 驱动函数
 *  @{ */

/**
 * @brief 初始化 SPI OLED 驱动
 *
 * 配置 SPI1 硬件、GPIO 引脚（PB0/1/2）及显示屏参数
 */
void SPI_OLED_Init(void);

/**
 * @brief 设置 SPI1 波特率分频系数
 * @param SPI_BaudRatePrescaler SPI 分频值（2、8、16、256 等）
 */
void SPI1_SetSpeed(uint8_t SPI_BaudRatePrescaler);

/**
 * @brief 通过 SPI1 写一字节并读取返回值
 * @param TxData 发送字节
 * @return 接收到的字节；发生超时或错误时返回 0
 */
uint8_t SPI1_WriteByte(uint8_t TxData);

/**
 * @brief 向 OLED 写入命令或数据
 * @param dat 命令/数据字节
 * @param cmd 模式选择（OLED_CMD 或 OLED_DATA）
 */
void SPI_OLED_WR_Byte(uint8_t dat,uint8_t cmd);

/**
 * @brief 设置显示光标位置（字节寻址）
 * @param x 列地址（0-127）
 * @param y 页地址（0-7，每页 8 像素）
 * @note 超出范围的值将导致显示位置异常，请在调用前保证参数有效。
 */
void SPI_OLED_Set_Pos(unsigned char x, unsigned char y);

/**
 * @brief 打开 OLED 显示
 */
void SPI_OLED_Display_On(void);

/**
 * @brief 关闭 OLED 显示
 */
void SPI_OLED_Display_Off(void);

/**
 * @brief 清屏（填充全黑）
 */
void SPI_OLED_Clear(void);

/**
 * @brief 显示汉字
 * @param x 起始列（0-127）
 * @param y 起始页（0-7）
 * @param no 汉字索引
 */
void SPI_OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no);

/**
 * @brief 绘制 BMP 位图（128×64）
 * @param x0 起始列
 * @param y0 起始页
 * @param x1 结束列
 * @param y1 结束页
 * @param BMP[] 位图数据指针
 */
void SPI_OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[]);

/**
 * @brief 显示单个 ASCII 字符
 * @param x 列地址
 * @param y 页地址
 * @param num 字符 ASCII 码（减 0x20 得数组索引）
 */
void SPI_OLED_ShowChar(uint16_t x,uint16_t y, uint8_t num);

/**
 * @brief 显示 ASCII 字符串
 * @param x 起始列
 * @param y 起始页
 * @param p 字符串指针（以 '\0' 结尾）
 */
void SPI_OLED_ShowString(uint16_t x,uint16_t y,char *p);

/**
 * @brief 显示 32 位有符号整数
 * @param x 起始列
 * @param y 起始页
 * @param num 整数值
 * @param len 显示最少位数（不足补 0）
 */
void SPI_OLED_ShowInt32Num(uint16_t x,uint16_t y, int32_t num, uint8_t len);

/**
 * @brief 显示单个 16×16 汉字
 * @param x 起始列
 * @param y 起始页
 * @param s 汉字索引或字符串
 */
void SPI_OLED_DrawFont16(uint16_t x, uint16_t y, char *s);

/**
 * @brief 显示单个 32×32 汉字
 * @param x 起始列
 * @param y 起始页
 * @param s 汉字索引或字符串
 */
void SPI_OLED_DrawFont32(uint16_t x, uint16_t y, char *s);

/**
 * @brief 显示字符串（支持中英文混显）
 * @param x 起始列
 * @param y 起始页
 * @param str 字符串指针
 * @param size 字体大小（16 或 32）
 */
void SPI_OLED_Show_Str(uint16_t x, uint16_t y, char *str,uint8_t size);

/** @} */

#endif  
