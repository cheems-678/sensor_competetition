/**
 * @file    ws2812b.h
 * @brief   WS2812B RGB灯带驱动 (PB13, 软件位操作)
 * @note    WS2812B时序:
 *          - 800kHz数据率 (1.25us/位)
 *          - T0H=0.4us, T0L=0.85us
 *          - T1H=0.8us,  T1L=0.45us
 *          - Reset: >50us低电平
 *          GRB顺序, 每颗LED 24bit
 */
#ifndef __WS2812B_H
#define __WS2812B_H

#include <stm32f10x.h>
#include <stdint.h>

/**
 * @brief 初始化WS2812B (PB13 推挽输出, 初始全灭)
 */
void WS2812B_Init(void);

/**
 * @brief 设置单个LED颜色
 * @param idx  LED索引 0~(WS2812_NUM-1)
 * @param r    红色 0~255
 * @param g    绿色 0~255
 * @param b    蓝色 0~255
 */
void WS2812B_SetColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置全部LED同色
 * @param r 红 0~255
 * @param g 绿 0~255
 * @param b 蓝 0~255
 */
void WS2812B_SetAll(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置亮度 (0~100%), 全部LED暖白色
 * @param pct 亮度百分比
 */
void WS2812B_SetBrightness(uint8_t pct);

/**
 * @brief 刷新显示 (将颜色数据发送到LED)
 * @note 必须调用此函数才能更新LED显示
 */
void WS2812B_Show(void);

/**
 * @brief 全部熄灭
 */
void WS2812B_Off(void);

#endif /* __WS2812B_H */
