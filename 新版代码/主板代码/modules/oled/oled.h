/**
 * @file    oled.h
 * @brief   0.96 寸 OLED 显示屏驱动 (SSD1306, 128×64, I2C 接口)
 * @note    本驱动适用于经典的 0.96 寸 OLED 模块 (SSD1306 控制器):
 *          - 分辨率: 128×64 像素
 *          - 接口: I2C (地址 0x3C)
 *          - 颜色: 白色/蓝色/双色 (单色, 像素亮/灭)
 *          
 *          模块接线:
 *          VCC → 3.3V
 *          GND → GND
 *          SCL → PB6 (I2C1_SCL)
 *          SDA → PB7 (I2C1_SDA)
 *          
 *          注意: I2C 地址 0x3C, 与 BH1750 (0x23) / PN532 (0x48) /
 *          MAX30100 (0x57) / APDS-9960 (0x39) 不冲突,
 *          可共用 I2C1 总线。
 *          
 *          本驱动实现:
 *          - 全尺寸帧缓冲 (1024 字节, 128×64/8)
 *          - 像素级绘图 (点/线/矩形/圆/三角形)
 *          - ASCII 字符显示 (6×8 和 8×16 字体)
 *          - 数字和字符串显示
 *          - 反色/旋转/滚动
 */
#ifndef __OLED_H
#define __OLED_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                      显示屏参数                                    *
 *==================================================================*/
#define OLED_WIDTH          128             /* 屏幕宽度 (像素)        */
#define OLED_HEIGHT         64              /* 屏幕高度 (像素)        */
#define OLED_PAGES          (OLED_HEIGHT / 8)  /* 页数 (8)            */
#define OLED_BUFFER_SIZE    (OLED_WIDTH * OLED_HEIGHT / 8)  /* 1024  */

/*==================================================================*
 *                      I2C 引脚配置 (共用 I2C1)                     *
 *==================================================================*/
#define OLED_I2C             I2C1
#define OLED_SCL_PORT        GPIOB
#define OLED_SCL_PIN         GPIO_Pin_6
#define OLED_SDA_PORT        GPIOB
#define OLED_SDA_PIN         GPIO_Pin_7

/*==================================================================*
 *                      I2C 设备地址                                 *
 *==================================================================*/
#define OLED_ADDR            0x3C           /* 7 位 I2C 地址          */
#define OLED_ADDR_WRITE      ((uint8_t)(OLED_ADDR << 1 | 0))
#define OLED_ADDR_READ       ((uint8_t)(OLED_ADDR << 1 | 1))

/*==================================================================*
 *                      I2C 控制字节                                  *
 *==================================================================*/
#define OLED_CTRL_CMD        0x00           /* 命令模式: Co=0, D/C=0  */
#define OLED_CTRL_DATA       0x40           /* 数据模式: Co=0, D/C=1  */

/*==================================================================*
 *                      颜色定义                                      *
 *==================================================================*/
#define OLED_BLACK           0              /* 像素熄灭/黑色           */
#define OLED_WHITE           1              /* 像素点亮/白色           */

/*==================================================================*
 *                      字体大小                                      *
 *==================================================================*/
#define OLED_FONT_6X8        0              /* 6×8 像素 (每行 21 字)  */
#define OLED_FONT_8X16       1              /* 8×16 像素 (每行 16 字) */

/*==================================================================*
 *                      默认 I2C 速度                                 *
 *==================================================================*/
#define OLED_I2C_SPEED       400000         /* 400kHz 快速模式         */

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================*
 *                         API 函数声明                              *
 *==================================================================*/

/**
 * @brief   初始化 OLED 显示屏
 * @note    执行流程:
 *          1. 初始化 I2C1 (PB6/PB7, 400kHz)
 *          2. SSD1306 硬件初始化序列:
 *             - 关闭显示
 *             - 设置时钟分频/振荡器频率
 *             - 设置复用比 (MUX)
 *             - 设置显示偏移
 *             - 设置显示起始行
 *             - 设置电荷泵使能
 *             - 设置地址模式 (水平)
 *             - 设置段重映射 (左右翻转)
 *             - 设置 COM 扫描方向 (上下翻转)
 *             - 设置 COM 引脚硬件配置
 *             - 设置对比度
 *             - 设置预充电周期
 *             - 设置 VCOMH 电压
 *             - 设置显示模式 (正常)
 *             - 清除显示缓冲区
 *             - 打开显示
 *          3. 清屏并显示
 * @return  0: 成功, 1: 失败
 */
uint8_t OLED_Init(void);

/**
 * @brief   将帧缓冲区的数据全部刷到 OLED
 * @note    通过 I2C 批量发送 1024 字节 (128×64/8) 到 GDDRAM。
 *          每次修改像素或绘制图形后, 需调用此函数才能看到效果。
 *          
 *          发送方式: 水平寻址模式, 一次发送所有页。
 */
void OLED_Refresh(void);

/**
 * @brief   清空帧缓冲区 (所有像素熄灭)
 * @note    仅清空缓冲区, 不刷新屏幕。
 *          需调用 OLED_Refresh() 才能看到效果。
 * @param   color  OLED_BLACK: 全部熄灭, OLED_WHITE: 全部点亮
 */
void OLED_Clear(uint8_t color);

/**
 * @brief   设置/清除单个像素
 * @param   x       X 坐标 (0~127)
 * @param   y       Y 坐标 (0~63)
 * @param   color   OLED_BLACK: 熄灭, OLED_WHITE: 点亮
 */
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t color);

/**
 * @brief   读取单个像素的状态
 * @param   x   X 坐标 (0~127)
 * @param   y   Y 坐标 (0~63)
 * @return  OLED_BLACK 或 OLED_WHITE
 */
uint8_t OLED_GetPixel(uint8_t x, uint8_t y);

/*========================= 基本图形 ==============================*/

/**
 * @brief   绘制线段 (Bresenham 算法)
 * @param   x1, y1  起点坐标
 * @param   x2, y2  终点坐标
 * @param   color   颜色
 */
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2,
                   uint8_t color);

/**
 * @brief   绘制矩形边框
 * @param   x, y    左上角坐标
 * @param   w, h    宽度和高度
 * @param   color   颜色
 */
void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                   uint8_t color);

/**
 * @brief   绘制填充矩形
 * @param   x, y    左上角坐标
 * @param   w, h    宽度和高度
 * @param   color   颜色
 */
void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                   uint8_t color);

/**
 * @brief   绘制圆角矩形边框
 * @param   x, y    左上角坐标
 * @param   w, h    宽度和高度
 * @param   r       圆角半径
 * @param   color   颜色
 */
void OLED_DrawRoundRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                        uint8_t r, uint8_t color);

/**
 * @brief   绘制三角形边框
 * @param   x1, y1, x2, y2, x3, y3  三个顶点
 * @param   color   颜色
 */
void OLED_DrawTriangle(uint8_t x1, uint8_t y1,
                       uint8_t x2, uint8_t y2,
                       uint8_t x3, uint8_t y3,
                       uint8_t color);

/**
 * @brief   绘制填充三角形
 * @param   x1, y1, x2, y2, x3, y3  三个顶点
 * @param   color   颜色
 */
void OLED_FillTriangle(uint8_t x1, uint8_t y1,
                       uint8_t x2, uint8_t y2,
                       uint8_t x3, uint8_t y3,
                       uint8_t color);

/**
 * @brief   绘制圆形边框
 * @param   cx, cy  圆心坐标
 * @param   r       半径
 * @param   color   颜色
 */
void OLED_DrawCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color);

/**
 * @brief   绘制填充圆
 * @param   cx, cy  圆心坐标
 * @param   r       半径
 * @param   color   颜色
 */
void OLED_FillCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color);

/*========================= 文本显示 ==============================*/

/**
 * @brief   设置字体大小
 * @param   font   OLED_FONT_6X8 或 OLED_FONT_8X16
 */
void OLED_SetFont(uint8_t font);

/**
 * @brief   设置文本光标位置
 * @param   x   X 坐标 (像素)
 * @param   y   Y 坐标 (像素)
 */
void OLED_SetCursor(uint8_t x, uint8_t y);

/**
 * @brief   获取当前光标 X 坐标
 */
uint8_t OLED_GetCursorX(void);

/**
 * @brief   获取当前光标 Y 坐标
 */
uint8_t OLED_GetCursorY(void);

/**
 * @brief   在指定位置显示一个 ASCII 字符
 * @param   x, y    左上角坐标
 * @param   ch      要显示的字符 (ASCII 0x20~0x7E)
 * @param   font    字体: OLED_FONT_6X8 或 OLED_FONT_8X16
 * @return  字符宽度 (像素)
 * @note    仅支持可打印 ASCII 字符。
 *          6×8 字体: 每字符 6 像素宽, 8 像素高
 *          8×16 字体: 每字符 8 像素宽, 16 像素高
 */
uint8_t OLED_DrawChar(uint8_t x, uint8_t y, char ch, uint8_t font);

/**
 * @brief   在光标位置显示一个 ASCII 字符 (自动换行)
 * @param   ch  要显示的字符
 * @note    换行符 '\n' 和回车符 '\r' 被正确处理。
 *          字符超出屏幕宽度时自动换行。
 */
void OLED_PutChar(char ch);

/**
 * @brief   在指定位置显示字符串
 * @param   x, y    左上角坐标
 * @param   str     要显示的字符串 (以 '\0' 结尾)
 * @param   font    字体
 */
void OLED_ShowString(uint8_t x, uint8_t y, const char *str, uint8_t font);

/**
 * @brief   在光标位置打印字符串 (默认字体, 自动换行)
 * @param   str  要显示的字符串
 */
void OLED_Print(const char *str);

/**
 * @brief   在指定位置显示无符号整数
 * @param   x, y    左上角坐标
 * @param   num     要显示的数字
 * @param   len     最小位数 (不足补前导零)
 * @param   font    字体
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len,
                  uint8_t font);

/**
 * @brief   在指定位置显示浮点数
 * @param   x, y        左上角坐标
 * @param   num         要显示的浮点数
 * @param   int_len     整数部分最小位数
 * @param   dec_len     小数部分位数
 * @param   font        字体
 */
void OLED_ShowFloat(uint8_t x, uint8_t y, double num,
                    uint8_t int_len, uint8_t dec_len, uint8_t font);

/*========================= 高级功能 ==============================*/

/**
 * @brief   设置显示方向
 * @param   rotate  0: 正常, 1: 90° 顺时针, 2: 180°, 3: 270°
 * @note    通过配置 SSD1306 的段重映射和 COM 扫描方向实现。
 *          设置后需调用 OLED_Refresh() 更新。
 */
void OLED_SetRotate(uint8_t rotate);

/**
 * @brief   设置显示对比度
 * @param   contrast  对比度值 (0~255, 默认 0x7F)
 * @note    值越大, OLED 像素越亮。
 */
void OLED_SetContrast(uint8_t contrast);

/**
 * @brief   启用反色显示
 * @param   invert  1: 反色 (黑底白字 → 白底黑字), 0: 正常
 */
void OLED_Invert(uint8_t invert);

/**
 * @brief   开启显示
 */
void OLED_DisplayOn(void);

/**
 * @brief   关闭显示 (进入睡眠模式, 低功耗)
 * @note    关闭后 GDDRAM 数据保留, 重新开启可恢复显示。
 */
void OLED_DisplayOff(void);

/**
 * @brief   整屏滚动指定行数
 * @param   lines  滚动行数 (正数: 向上滚动, 负数: 向下滚动)
 * @note    通过移动帧缓冲区数据实现。
 */
void OLED_Scroll(uint8_t lines);

/**
 * @brief   硬件水平滚动
 * @param   direction  0: 向左, 1: 向右
 * @param   start_page 起始页 (0~7)
 * @param   end_page   结束页 (0~7)
 * @param   speed      滚动速度 (0~7, 0=最快, 7=最慢)
 * @note    SSD1306 硬件滚动功能, 无需 CPU 干预。
 *          使用后如需正常显示, 需调用 OLED_StopScroll()。
 */
void OLED_HWScroll(uint8_t direction, uint8_t start_page,
                   uint8_t end_page, uint8_t speed);

/**
 * @brief   停止硬件滚动
 */
void OLED_StopScroll(void);

/**
 * @brief   将缓冲区取反 (黑 ↔ 白)
 */
void OLED_InvertBuffer(void);

#ifdef __cplusplus
}
#endif

#endif


