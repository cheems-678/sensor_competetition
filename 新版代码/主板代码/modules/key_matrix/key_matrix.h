/**
 * @file    key_matrix.h
 * @brief   4x4 矩阵键盘驱动
 * @note    智能安防系统专用 (自研, 非驱动库原文件):
 *          行 (Rows, 输出): PB0  PB1  PB8  PB9
 *          列 (Cols, 输入): PA6  PA8  PA11 PA12
 *
 *          扫描原理:
 *          1. 所有行输出高电平
 *          2. 依次将某一行拉低, 读取 4 个列输入
 *          3. 若某列读到低电平, 则该 (行,列) 交点处的按键被按下
 *
 *          按键编号 (KeyMatrix_Scan 返回值 0~15):
 *            R0:  0:1  1:2  2:3  3:A
 *            R1:  4:4  5:5  6:6  7:B
 *            R2:  8:7  9:8  10:9 11:C
 *            R3:  12:* 13:0 14:# 15:D
 *
 *          使用说明:
 *          - main 中开启 1ms SysTick, 在 SysTick 中断或主循环里调用
 *            KeyMatrix_Tick() 进行消抖计时
 *          - 主循环轮询 KeyMatrix_Scan(), 返回 KEY_NONE 表示无按键
 *          - 典型代码:
 *              KeyMatrix_Init();
 *              while(1) {
 *                  key = KeyMatrix_Scan();
 *                  if (key != KEY_NONE) { ... }
 *                  delay(10);
 *              }
 */
#ifndef __KEY_MATRIX_H
#define __KEY_MATRIX_H

#include <stm32f10x.h>
#include <stdint.h>

/*========================== 引脚配置 =========================*/
/* 行 (输出, 扫描时逐行拉低) */
#define KEYM_ROW_PORT       GPIOB
#define KEYM_ROW_PINS       (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_8 | GPIO_Pin_9)
#define KEYM_ROW_P0         GPIO_Pin_0      /* PB0 行 0 */
#define KEYM_ROW_P1         GPIO_Pin_1      /* PB1 行 1 */
#define KEYM_ROW_P2         GPIO_Pin_8      /* PB8 行 2 */
#define KEYM_ROW_P3         GPIO_Pin_9      /* PB9 行 3 */

/* 列 (输入, 上拉, 读到低电平表示按下) */
#define KEYM_COL_PORT       GPIOA
#define KEYM_COL_PINS       (GPIO_Pin_6 | GPIO_Pin_8 | GPIO_Pin_11 | GPIO_Pin_12)
#define KEYM_COL_P0         GPIO_Pin_6      /* PA6  列 0 */
#define KEYM_COL_P1         GPIO_Pin_8      /* PA8  列 1 */
#define KEYM_COL_P2         GPIO_Pin_11     /* PA11 列 2 */
#define KEYM_COL_P3         GPIO_Pin_12     /* PA12 列 3 */

/* 无按键返回值 */
#define KEY_NONE            0xFF

/* 行列数量 */
#define KEYM_ROW_NUM        4
#define KEYM_COL_NUM        4

/**
 * @brief   初始化矩阵键盘引脚
 * @note    行: 推挽输出, 初始高电平
 *          列: 上拉输入
 */
void KeyMatrix_Init(void);

/**
 * @brief   扫描键盘并消抖
 * @return  KEY_NONE: 无有效按键
 *          0~15:    被按下的按键编号
 * @note    需配合 KeyMatrix_Tick() (1ms 周期调用) 完成消抖,
 *          否则每次调用仅返回未消抖结果。
 */
uint8_t KeyMatrix_Scan(void);

/**
 * @brief   1ms 定时器节拍, 用于按键消抖
 * @note    在 SysTick 中断或主循环中每 1ms 调用一次
 */
void KeyMatrix_Tick(void);

#endif /* __KEY_MATRIX_H */
