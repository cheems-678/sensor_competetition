/**
 * 淘晶驰T1 2.4寸串口屏驱动 - SPL版本
 *
 * 接线:
 *   STM32 PB10 (USART3_TX) → 串口屏 RX
 *   STM32 PB11 (USART3_RX) ← 串口屏 TX
 *   3V3 → VCC, GND → GND
 *
 * 通信协议 (帧格式):
 *   帧头: 0x5A 0xA5
 *   命令: 1字节
 *   长度: 1字节 (数据长度)
 *   数据: N字节
 *   校验: 1字节 (XOR, 从帧头到数据最后一字节)
 *
 * 命令:
 *   0x01 切页面
 *   0x02 设int变量
 *   0x03 设float变量
 *   0x04 设字符串变量
 *   0x05 触摸事件 (屏→MCU)
 *   0x06 画填充矩形
 *   0x07 画文字
 *   0x08 清屏
 *   0x09 画像素
 *
 * 页面规划 (在淘晶驰编辑器中设计):
 *   Page0: 主菜单
 *   Page1: 扫描中
 *   Page2: 声速热力图
 *   Page3: 异常分类图
 *   Page4: 虫害波形
 *   Page5: 料位显示
 */

#ifndef __TFT_H
#define __TFT_H

#include "../../config.h"

/* RGB565 颜色 */
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_GRAY    0x7BEF
#define C_ORANGE  0xFD20
#define C_DARKG   0x1CA3
#define C_PURPLE  0x780F

/* 页面编号 (与淘晶驰编辑器中对应) */
#define PAGE_MENU    0
#define PAGE_SCAN    1
#define PAGE_VMAP    2
#define PAGE_AMAP    3
#define PAGE_WAVE    4
#define PAGE_LEVEL   5

/* 变量编号 (与淘晶驰编辑器中对应) */
#define VAR_MAIN_ANOM   0
#define VAR_CONF_INT    1
#define VAR_ELAPSED_MS  2
#define VAR_LEVEL_MM    3
#define VAR_ENERGY      4
#define VAR_FREQ_HZ     5
#define VAR_INS_DETECT  6
#define VAR_MODE        7

/* 触摸事件回调 */
typedef void (*TFT_TouchCB)(uint8_t page, uint16_t x, uint16_t y);

void     TFT_Init(void);

/* 页面/变量操作 */
void     TFT_SetPage(uint8_t page);
void     TFT_SetVarInt(uint8_t idx, uint16_t val);
void     TFT_SetVarFloat(uint8_t idx, float val);
void     TFT_SetVarStr(uint8_t idx, const char* str);
void     TFT_RegisterTouchCB(TFT_TouchCB cb);

/* 直接绘图 (串口屏支持, 也可通过编辑器预设计) */
void     TFT_Clear(uint16_t color);
void     TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     TFT_Pixel(uint16_t x, uint16_t y, uint16_t color);
void     TFT_Text(uint16_t x, uint16_t y, const char* s, uint16_t color, uint8_t size);

/* 高级绘制 (直接在屏上画, 适合实时更新) */
void     TFT_VelocityMap(uint16_t ox, uint16_t oy, uint8_t pxSize,
                          float* vmap, float vmin, float vmax);
void     TFT_AnomalyMap(uint16_t ox, uint16_t oy, uint8_t pxSize,
                         uint8_t* amap, uint8_t mainAnom,
                         float conf, uint32_t elapsed_ms);
void     TFT_Waveform(uint16_t ox, uint16_t oy, uint16_t w, uint16_t h,
                       uint16_t* data, uint16_t n, uint16_t color);
void     TFT_DrawLevel(uint16_t level_mm, uint16_t siloH_mm);
void     TFT_Menu(uint8_t currentMode);

uint16_t TFT_AnomColor(uint8_t t);

/* 触摸事件处理 (在主循环中调用) */
void     TFT_ProcessTouch(void);

#endif /* __TFT_H */
