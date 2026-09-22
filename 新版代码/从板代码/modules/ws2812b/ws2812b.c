/**
 * @file    ws2812b.c
 * @brief   WS2812B RGB灯带驱动实现 (PB13)
 *
 * 72MHz主频下软件位操作时序:
 *   T0H=0.4us≈29 cycles, T0L=0.85us≈61 cycles
 *   T1H=0.8us≈58 cycles,  T1L=0.45us≈32 cycles
 *   每位1.25us=90 cycles
 *   使用内联循环+__NOP()精确控制
 */

#include "ws2812b.h"
#include "../../config.h"

/* 颜色缓冲区 GRB格式 */
static uint8_t led_buf[WS2812_NUM * 3];

/* 延时循环数 (72MHz, 需根据实际示波器校准) */
#define DELAY_0H  8    /* T0H ~0.4us */
#define DELAY_0L  15   /* T0L ~0.85us */
#define DELAY_1H  14   /* T1H ~0.8us */
#define DELAY_1L  6    /* T1L ~0.45us */

static void ws2812_delay(uint8_t n)
{
    while (n--) __NOP();
}

static void ws2812_send_byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (byte & 0x80) {
            /* T1: 高0.8us + 低0.45us */
            WS2812_PORT->BSRR = WS2812_PIN;
            ws2812_delay(DELAY_1H);
            WS2812_PORT->BRR = WS2812_PIN;
            ws2812_delay(DELAY_1L);
        } else {
            /* T0: 高0.4us + 低0.85us */
            WS2812_PORT->BSRR = WS2812_PIN;
            ws2812_delay(DELAY_0H);
            WS2812_PORT->BRR = WS2812_PIN;
            ws2812_delay(DELAY_0L);
        }
        byte <<= 1;
    }
}

void WS2812B_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = WS2812_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WS2812_PORT, &gpio);

    WS2812B_Off();
}

void WS2812B_SetColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx >= WS2812_NUM) return;
    led_buf[idx * 3]     = g;  /* GRB顺序 */
    led_buf[idx * 3 + 1] = r;
    led_buf[idx * 3 + 2] = b;
}

void WS2812B_SetAll(uint8_t r, uint8_t g, uint8_t b)
{
    int i;
    for (i = 0; i < WS2812_NUM; i++)
        WS2812B_SetColor((uint8_t)i, r, g, b);
}

void WS2812B_SetBrightness(uint8_t pct)
{
    uint8_t v;
    int i;
    if (pct > 100) pct = 100;
    /* 暖白色: R=G=B, 亮度=百分比×2.55 */
    v = (uint8_t)((uint16_t)pct * 255 / 100);
    for (i = 0; i < WS2812_NUM; i++)
        WS2812B_SetColor((uint8_t)i, v, v, v);
}

void WS2812B_Show(void)
{
    uint8_t i;
    /* 关中断 保证时序不被打断 */
    __disable_irq();
    for (i = 0; i < WS2812_NUM * 3; i++) {
        ws2812_send_byte(led_buf[i]);
    }
    __enable_irq();
    /* Reset >50us */
    ws2812_delay(60);
}

void WS2812B_Off(void)
{
    int i;
    for (i = 0; i < WS2812_NUM * 3; i++)
        led_buf[i] = 0;
    WS2812B_Show();
}
