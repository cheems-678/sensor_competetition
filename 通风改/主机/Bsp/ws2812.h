#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

typedef struct
{
    uint32_t send_count;
    uint32_t failure_count;
    uint32_t last_elapsed_ms;
    uint32_t last_dma_isr;
    uint32_t last_timer_restore_ok;
    uint32_t last_red;
    uint32_t last_green;
    uint32_t last_blue;
} Ws2812Diagnostics;

extern volatile Ws2812Diagnostics Ws2812Diag;

/* 30 cascaded WS2812 pixels, data output on PB13/TIM1_CH1N. */
void Ws2812_Init(void);
uint8_t Ws2812_SetSolid(uint8_t red, uint8_t green, uint8_t blue);

#endif /* WS2812_H */
