/**
 * @file    key_matrix.c
 * @brief   4x4 矩阵键盘驱动实现
 * @note    智能安防系统专用 (自研, 非驱动库原文件)
 *          行: PB0/PB1/PB8/PB9 (输出)
 *          列: PA6/PA8/PA11/PA12 (上拉输入)
 *
 *          消抖逻辑:
 *          - KeyMatrix_Tick() 每 1ms 调用一次
 *          - 连续 10ms 读到同一按键视为有效
 *          - 松手后自动复位, 支持长按重复吗? 不支持, 需重新按下
 */
#include "key_matrix.h"

#define DEBOUNCE_MS         10      /* 消抖时间 (10ms) */

/* 列引脚映射表 */
static const uint16_t col_pins[KEYM_COL_NUM] = {
    KEYM_COL_P0, KEYM_COL_P1, KEYM_COL_P2, KEYM_COL_P3
};

/* 行引脚映射表 */
static const uint16_t row_pins[KEYM_ROW_NUM] = {
    KEYM_ROW_P0, KEYM_ROW_P1, KEYM_ROW_P2, KEYM_ROW_P3
};

static uint8_t  last_raw;       /* 上次扫描的原始键值 (KEY_NONE=无) */
static uint8_t  stable_key;     /* 已消抖确认的按键 */
static uint16_t stable_tick;    /* 当前键值保持的节拍计数 */
static uint8_t  released;       /* 上一次有效按键是否已松手 */

/**
 * @brief   初始化矩阵键盘引脚
 */
void KeyMatrix_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    /* 行: 推挽输出, 初始高电平 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = KEYM_ROW_PINS;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEYM_ROW_PORT, &gpio);
    GPIO_SetBits(KEYM_ROW_PORT, KEYM_ROW_PINS);

    /* 列: 上拉输入 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = KEYM_COL_PINS;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(KEYM_COL_PORT, &gpio);

    last_raw   = KEY_NONE;
    stable_key = KEY_NONE;
    stable_tick = 0;
    released   = 1;
}

/**
 * @brief   扫描一次键盘, 返回原始键值 (不做消抖)
 * @return  KEY_NONE: 无按键; 0~15: 按键编号
 */
static uint8_t key_scan_raw(void)
{
    uint8_t r, c;

    for (r = 0; r < KEYM_ROW_NUM; r++) {
        /* 先将所有行置高, 再只将当前行拉低 */
        GPIO_SetBits(KEYM_ROW_PORT, KEYM_ROW_PINS);
        GPIO_ResetBits(KEYM_ROW_PORT, row_pins[r]);

        /* 读 4 个列 */
        for (c = 0; c < KEYM_COL_NUM; c++) {
            if (GPIO_ReadInputDataBit(KEYM_COL_PORT, col_pins[c]) == Bit_RESET) {
                /* 列被拉低, 说明该交点按键按下 */
                return (uint8_t)(r * KEYM_COL_NUM + c);
            }
        }
    }

    /* 没有按键按下 (恢复所有行为高电平) */
    GPIO_SetBits(KEYM_ROW_PORT, KEYM_ROW_PINS);
    return KEY_NONE;
}

/**
 * @brief   1ms 节拍, 消抖状态机
 */
void KeyMatrix_Tick(void)
{
    uint8_t cur = key_scan_raw();

    if (cur == last_raw) {
        /* 键值稳定 */
        if (cur != KEY_NONE) {
            if (stable_tick < 0xFFFF)
                stable_tick++;
            if (stable_tick >= DEBOUNCE_MS && released) {
                /* 消抖完成且上一按键已松开, 确认本次按下 */
                stable_key = cur;
                released   = 0;
            }
        }
    } else {
        /* 键值变化, 重新计时 */
        last_raw   = cur;
        stable_tick = 0;
    }

    /* 松手检测 */
    if (cur == KEY_NONE && !released) {
        released   = 1;
        stable_key = KEY_NONE;
    }
}

/**
 * @brief   获取消抖后的按键
 * @return  KEY_NONE: 无按键; 0~15: 按键编号
 * @note    按键被确认后, 直到松手并再次按下才会返回新值。
 */
uint8_t KeyMatrix_Scan(void)
{
    uint8_t k = stable_key;
    return k;
}
