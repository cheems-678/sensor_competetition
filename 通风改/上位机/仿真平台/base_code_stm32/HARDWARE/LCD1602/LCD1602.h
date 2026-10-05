/**
 * @file LCD1602.h
 * @brief LCD1602 字符显示屏驱动头文件（宏与函数原型）
 *
 * 提供 LCD1602 的命令宏、控制引脚定义以及对外可调用的驱动函数原型。
 */

#ifndef LCD1602_H
#define LCD1602_H

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* Command definitions (HD44780-like) */
#define LCD1602A_CMD1 0X1    /**< Clear display */
#define LCD1602A_CMD2 0X2    /**< Return home */
#define LCD1602A_CMD3 0X4    /**< Entry mode set */
#define LCD1602A_CMD4 0X8    /**< Display on/off control */
#define LCD1602A_CMD5 0X10   /**< Cursor or display shift */
#define LCD1602A_CMD6 0X20   /**< Function set */
#define LCD1602A_CMD7 0X40   /**< Set CGRAM address */
#define LCD1602A_CMD8 0X80   /**< Set DDRAM address */

/* Entry mode flags */
#define LCD1602A_CMD3_ID_SET   (0x1<<1)       /**< AC++ */
#define LCD1602A_CMD3_ID_RESET (~(0x1<<1))    /**< AC-- */
#define LCD1602A_CMD3_S_SET    (0x1<<0)       /**< SCREEN SHIFT */
#define LCD1602A_CMD3_S_RESET  (~(0x1<<0))    /**< SCREEN NO SHIFT */

/* Display control flags */
#define LCD1602A_CMD4_D_SET   (0x1<<2)       /**< DISPLAY ON */
#define LCD1602A_CMD4_D_RESET (~(0x1<<2))    /**< DISPLAY OFF */
#define LCD1602A_CMD4_C_SET   (0x1<<1)       /**< CURSOR DISPLAY */
#define LCD1602A_CMD4_C_RESET (~(0x1<<1))    /**< CURSOR NO DISPLAY */
#define LCD1602A_CMD4_B_SET   (0x1<<0)       /**< BLINK ON */
#define LCD1602A_CMD4_B_RESET (~(0x1<<0))    /**< BLINK OFF */

/* Shift flags */
#define LCD1602A_CMD5_SC_SET   (0x1<<3)
#define LCD1602A_CMD5_SC_RESET (~(0x1<<3))
#define LCD1602A_CMD5_RL_SET   (0x1<<2)
#define LCD1602A_CMD5_RL_RESET (~(0x1<<2))

/* Function set flags */
#define LCD1602A_CMD6_DL_SET   (0x1<<4)
#define LCD1602A_CMD6_DL_RESET (~(0x1<<4))
#define LCD1602A_CMD6_N_SET    (0x1<<3)
#define LCD1602A_CMD6_N_RESET  (~(0x1<<3))
#define LCD1602A_CMD6_F_SET    (0x1<<2)
#define LCD1602A_CMD6_F_RESET  (~(0x1<<2))

/*******************************************************************************/
/** @name 控制/状态 宏 */
/* @{ */
#define BUSY 0x80    /**< Busy flag mask */
#define RS GPIO_Pin_5 /**< Register Select pin */
#define RW GPIO_Pin_6 /**< Read/Write pin */
#define EN GPIO_Pin_7 /**< Enable pin */
/* @} */
/*******************************************************************************/

/**
 * @defgroup LCD1602_Functions LCD1602 函数
 * 驱动暴露的公共函数原型
 * @{
 */
/**
 * @brief 读取 BUSY 标志并在返回前保证 LCD 可接收新命令
 */
void ReadBusy(void);

/**
 * @brief 向 LCD 写入命令字节
 * @param CMD 命令字节
 */
void LCD_WRITE_CMD( unsigned char CMD );

/**
 * @brief 在指定行列写入字符串
 * @param StrData 指向以 '\0' 结尾的字符串
 * @param row 行号（0 或 1）
 * @param col 列偏移
 */
void LCD_WRITE_StrDATA( char *StrData, unsigned char row, unsigned char col );

/**
 * @brief 向 LCD 写入一个数据字节（用于显示）
 * @param ByteData 数据字节
 */
void LCD_WRITE_ByteDATA( unsigned char ByteData );

/**
 * @brief 初始化 LCD 驱动（包含 GPIO 初始化）
 */
void LCD_INIT(void);

/**
 * @brief 初始化用于 LCD 的 GPIO 引脚
 */
void GPIO_INIT(void);

/**
 * @brief 将用户自定义图像数据写入 CGRAM
 * @param pos 自定义槽位 0-7
 * @param ImgInfo 指向图模数据（以 '\0' 结束）
 */
void WUserImg(unsigned char pos,unsigned char *ImgInfo);

/** @} */

#endif
