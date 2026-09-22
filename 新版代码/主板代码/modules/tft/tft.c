/**
 * 淘晶驰T1 2.4寸串口屏驱动 - 实现
 *
 * USART1: PA9=TX PA10=RX, 115200 8N1
 * 协议: 0x5A 0xA5 [CMD] [LEN] [DATA] [XOR]
 */

#include "tft.h"

/* ========== 帧协议 ========== */
#define FRAME_HDR1  0x5A
#define FRAME_HDR2  0xA5

/* 命令码 */
#define CMD_PAGE    0x01
#define CMD_SETINT  0x02
#define CMD_SETFLOAT 0x03
#define CMD_SETSTR  0x04
#define CMD_TOUCH   0x05
#define CMD_FILLRECT 0x06
#define CMD_DRAWSTR 0x07
#define CMD_CLEAR   0x08
#define CMD_PIXEL   0x09

/* 触摸事件缓冲区 */
#define TOUCH_BUF_SIZE  16
static volatile uint8_t  touch_buf[TOUCH_BUF_SIZE];
static volatile uint8_t  touch_len = 0;
static TFT_TouchCB       touch_cb = 0;

/* 接收缓冲区 */
#define RX_BUF_SIZE 64
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint8_t  rx_wr = 0;

/* ========== USART3 初始化 ========== */
void TFT_Init(void) {
    GPIO_InitTypeDef  g;
    USART_InitTypeDef u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* PB10 TX - 复用推挽 */
    g.GPIO_Pin   = GPIO_Pin_10;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);

    /* PB11 RX - 浮空输入 */
    g.GPIO_Pin  = GPIO_Pin_11;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &g);

    /* USART3: 115200 8N1 */
    u.USART_BaudRate            = TFT_BAUD;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(TFT_USART, &u);
    USART_Cmd(TFT_USART, ENABLE);

    /* 使能接收中断 */
    USART_ITConfig(TFT_USART, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART3_IRQn);
    NVIC_SetPriority(USART3_IRQn, 2, 0);

    /* 清屏 */
    TFT_Clear(C_BLACK);
}

/* ========== 发送一个字节 ========== */
static void tftputc(uint8_t c) {
    while (USART_GetFlagStatus(TFT_USART, USART_FLAG_TXE) == RESET);
    USART_SendData(TFT_USART, c);
}

/* ========== 发送一个帧 ========== */
static void TFT_SendFrame(uint8_t cmd, uint8_t* data, uint8_t len) {
    uint8_t i;
    uint8_t xor;

    /* 帧头 */
    tftputc(FRAME_HDR1);
    tftputc(FRAME_HDR2);
    /* 命令 */
    tftputc(cmd);
    xor = cmd;
    /* 长度 */
    tftputc(len);
    xor ^= len;
    /* 数据 */
    for (i = 0; i < len; i++) {
        tftputc(data[i]);
        xor ^= data[i];
    }
    /* 校验 */
    tftputc(xor);

    while (USART_GetFlagStatus(TFT_USART, USART_FLAG_TC) == RESET);
}

/* ========== 页面/变量操作 ========== */
void TFT_SetPage(uint8_t page) {
    uint8_t d[1];
    d[0] = page;
    TFT_SendFrame(CMD_PAGE, d, 1);
    SysTick_Delay_Us(500);
}

void TFT_SetVarInt(uint8_t idx, uint16_t val) {
    uint8_t d[3];
    d[0] = idx;
    d[1] = (uint8_t)(val >> 8);
    d[2] = (uint8_t)(val & 0xFF);
    TFT_SendFrame(CMD_SETINT, d, 3);
}

void TFT_SetVarFloat(uint8_t idx, float val) {
    uint8_t d[5];
    uint8_t* p = (uint8_t*)&val;
    d[0] = idx;
    d[1] = p[0]; d[2] = p[1]; d[3] = p[2]; d[4] = p[3];
    TFT_SendFrame(CMD_SETFLOAT, d, 5);
}

void TFT_SetVarStr(uint8_t idx, const char* str) {
    uint8_t d[33];
    uint8_t len = (uint8_t)strlen(str);
    if (len > 31) len = 31;
    d[0] = idx;
    d[1] = len;
    memcpy(&d[2], str, len);
    TFT_SendFrame(CMD_SETSTR, d, (uint8_t)(len + 2));
}

void TFT_RegisterTouchCB(TFT_TouchCB cb) {
    touch_cb = cb;
}

/* ========== 基础绘图 ========== */
void TFT_Clear(uint16_t color) {
    uint8_t d[2];
    d[0] = (uint8_t)(color >> 8);
    d[1] = (uint8_t)(color & 0xFF);
    TFT_SendFrame(CMD_CLEAR, d, 2);
    SysTick_Delay_Us(200);
}

void TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    uint8_t d[10];
    d[0] = (uint8_t)(x >> 8); d[1] = (uint8_t)(x & 0xFF);
    d[2] = (uint8_t)(y >> 8); d[3] = (uint8_t)(y & 0xFF);
    d[4] = (uint8_t)((x + w - 1) >> 8); d[5] = (uint8_t)((x + w - 1) & 0xFF);
    d[6] = (uint8_t)((y + h - 1) >> 8); d[7] = (uint8_t)((y + h - 1) & 0xFF);
    d[8] = (uint8_t)(color >> 8); d[9] = (uint8_t)(color & 0xFF);
    TFT_SendFrame(CMD_FILLRECT, d, 10);
}

void TFT_Pixel(uint16_t x, uint16_t y, uint16_t color) {
    uint8_t d[6];
    d[0] = (uint8_t)(x >> 8); d[1] = (uint8_t)(x & 0xFF);
    d[2] = (uint8_t)(y >> 8); d[3] = (uint8_t)(y & 0xFF);
    d[4] = (uint8_t)(color >> 8); d[5] = (uint8_t)(color & 0xFF);
    TFT_SendFrame(CMD_PIXEL, d, 6);
}

void TFT_Text(uint16_t x, uint16_t y, const char* s, uint16_t color, uint8_t size) {
    uint8_t d[64];
    uint8_t len = (uint8_t)strlen(s);
    if (len > 55) len = 55;
    d[0] = (uint8_t)(x >> 8); d[1] = (uint8_t)(x & 0xFF);
    d[2] = (uint8_t)(y >> 8); d[3] = (uint8_t)(y & 0xFF);
    d[4] = (uint8_t)(color >> 8); d[5] = (uint8_t)(color & 0xFF);
    d[6] = size;
    d[7] = len;
    memcpy(&d[8], s, len);
    TFT_SendFrame(CMD_DRAWSTR, d, (uint8_t)(len + 8));
}

/* ========== 声速→颜色 蓝→绿→红 ========== */
static uint16_t v2c(float v, float vmin, float vmax) {
    float rng = vmax - vmin;
    if (rng < 0.0001f) return C_GREEN;
    float n = (v - vmin) / rng;
    if (n < 0) n = 0; if (n > 1) n = 1;
    float h = (1.0f - n) * 240.0f;
    float s = 1.0f, l = 0.5f;
    float c = (1.0f - fabsf(2*l - 1)) * s;
    float x = c * (1.0f - fabsf(fmodf(h/60.0f, 2) - 1));
    float m = l - c/2;
    float r, g, b;
    if      (h <  60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else              { r = 0; g = x; b = c; }
    uint8_t r5 = (uint8_t)((r + m) * 31);
    uint8_t g6 = (uint8_t)((g + m) * 63);
    uint8_t b5 = (uint8_t)((b + m) * 31);
    return (r5 << 11) | (g6 << 5) | b5;
}

/* ========== 异常类型颜色 ========== */
uint16_t TFT_AnomColor(uint8_t t) {
    switch (t) {
        case A_VOID: return C_RED;
        case A_WET:  return C_BLUE;
        case A_HOT:  return C_ORANGE;
        case A_CMP:  return C_YELLOW;
        case A_INS:  return C_MAGENTA;
        default:     return C_GREEN;
    }
}

static const char* AnomName(uint8_t t) {
    switch (t) {
        case A_NORM: return "NORMAL";
        case A_VOID: return "VOID";
        case A_WET:  return "WET";
        case A_HOT:  return "HOT";
        case A_CMP:  return "CMPD";
        case A_INS:  return "BUGS";
        default:     return "???";
    }
}

/* ========== 声速热力图 ========== */
void TFT_VelocityMap(uint16_t ox, uint16_t oy, uint8_t sz,
                     float* v, float vmin, float vmax) {
    int r, c;
    for (r = 0; r < GRID_N; r++) {
        for (c = 0; c < GRID_N; c++) {
            float cx = (float)c * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
            float cy = (float)r * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
            uint16_t x = ox + c * sz;
            uint16_t y = oy + r * sz;
            if (cx*cx + cy*cy > 100.0f*100.0f) {
                TFT_FillRect(x, y, sz, sz, C_DARKG);
            } else {
                TFT_FillRect(x, y, sz, sz, v2c(v[r*GRID_N + c], vmin, vmax));
            }
        }
    }
    /* 色标条 */
    uint16_t bx = ox + GRID_N*sz + 4;
    int i;
    for (i = 0; i < GRID_N*sz; i++) {
        float nv = vmin + (vmax - vmin) * (float)i / (float)(GRID_N*sz);
        TFT_FillRect(bx, oy + GRID_N*sz - 1 - i, 8, 1, v2c(nv, vmin, vmax));
    }
    TFT_Text(bx + 12, oy,             "Hi", C_WHITE, 1);
    TFT_Text(bx + 12, oy + GRID_N*sz - 10, "Lo", C_WHITE, 1);
}

/* ========== 异常分类图 + 状态栏 ========== */
void TFT_AnomalyMap(uint16_t ox, uint16_t oy, uint8_t sz,
                     uint8_t* a, uint8_t mainA,
                     float conf, uint32_t elapsed) {
    int r, c;
    for (r = 0; r < GRID_N; r++) {
        for (c = 0; c < GRID_N; c++) {
            float cx = (float)c * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
            float cy = (float)r * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
            uint16_t x = ox + c * sz;
            uint16_t y = oy + r * sz;
            if (cx*cx + cy*cy > 100.0f*100.0f) TFT_FillRect(x, y, sz, sz, C_DARKG);
            else TFT_FillRect(x, y, sz, sz, TFT_AnomColor(a[r*GRID_N + c]));
        }
    }
    uint16_t by = oy + GRID_N*sz + 2;
    char buf[40];
    TFT_Text(ox, by, "TYPE:", C_WHITE, 1);
    TFT_Text(ox + 36, by, AnomName(mainA), TFT_AnomColor(mainA), 1);

    snprintf(buf, sizeof(buf), "CONF:%d%%  ", (int)(conf*100));
    TFT_Text(ox, by + 14, buf, C_WHITE, 1);

    snprintf(buf, sizeof(buf), "TIME:%dms", (int)elapsed);
    TFT_Text(ox, by + 28, buf, C_CYAN, 1);
}

/* ========== 声发射波形 ========== */
void TFT_Waveform(uint16_t ox, uint16_t oy, uint16_t w, uint16_t h,
                   uint16_t* d, uint16_t n, uint16_t color) {
    TFT_FillRect(ox, oy, w, h, C_BLACK);
    /* 网格 */
    uint16_t x, y;
    for (x = 0; x < w; x += 20)
        for (y = 0; y < h; y += 20)
            TFT_Pixel(ox + x, oy + y, C_GRAY);

    uint32_t step = (n > w) ? n / w : 1;
    uint16_t px = 0;
    uint32_t i;
    for (i = 0; i < n && px < w; i += step) {
        uint16_t v = d[i];
        uint16_t py = oy + h - 1 - ((uint32_t)v * (uint32_t)(h-1) / 4095);
        if (py < oy) py = oy; if (py >= oy + h) py = oy + h - 1;
        TFT_Pixel(ox + px, py, color);
        px++;
    }
}

/* ========== 料位显示 ========== */
void TFT_DrawLevel(uint16_t level_mm, uint16_t siloH_mm) {
    TFT_Clear(C_BLACK);
    TFT_Text(20, 10, "GRAIN LEVEL", C_CYAN, 2);
    uint16_t sx = 70, sy = 60, sw = 100, sh = 200;
    /* 仓壁 */
    TFT_FillRect(sx, sy, sw, sh, C_DARKG);
    /* 粮食 */
    float r = (siloH_mm > 0) ? (float)level_mm / (float)siloH_mm : 0;
    if (r > 1) r = 1;
    uint16_t fh = (uint16_t)(sh * r);
    TFT_FillRect(sx, sy + sh - fh, sw, fh, C_YELLOW);

    char buf[40];
    snprintf(buf, sizeof(buf), "%dmm / %dmm", level_mm, siloH_mm);
    TFT_Text(sx - 20, sy + sh + 14, buf, C_WHITE, 1);
    snprintf(buf, sizeof(buf), "Full: %d%%", (int)(r*100));
    TFT_Text(sx + 10, sy + sh / 2, buf, C_BLACK, 1);
}

/* ========== 菜单 ========== */
void TFT_Menu(uint8_t cur) {
    TFT_Clear(C_BLACK);
    TFT_Text(24, 10, "GRAIN ACOUSTIC CT", C_CYAN, 2);
    TFT_Text(40, 48, "SYSTEM V1.0", C_WHITE, 1);

    const char* msgs[] = {"1.TOMOGRAPHY","2.LISTEN BUG","3.GRAIN LEVEL"};
    int i;
    for (i = 0; i < MODE_NUM; i++) {
        TFT_Text(30, 100 + i * 32, msgs[i],
                 (i == cur) ? C_YELLOW : C_WHITE, 2);
        if (i == cur) TFT_Text(15, 100 + i * 32, ">", C_YELLOW, 2);
    }
    TFT_Text(10, 290, "START:RUN MODE:SWITCH SAVE:SAVE", C_GREEN, 1);
}

/* ========== USART3 接收中断 ========== */
void USART3_IRQHandler(void) {
    if (USART_GetITStatus(TFT_USART, USART_IT_RXNE) != RESET) {
        uint8_t b = USART_ReceiveData(TFT_USART);
        if (rx_wr < RX_BUF_SIZE) {
            rx_buf[rx_wr++] = b;
        }
    }
}

/* ========== 触摸事件处理 ========== */
void TFT_ProcessTouch(void) {
    uint8_t i;
    uint8_t found = 0;
    uint8_t cmd, len, xor_calc, xor_recv;
    uint8_t data[8];

    /* 搜索完整帧: 0x5A 0xA5 [CMD] [LEN] [DATA] [XOR] */
    for (i = 0; i < rx_wr; i++) {
        if (rx_buf[i] == FRAME_HDR1 &&
            i + 1 < rx_wr && rx_buf[i+1] == FRAME_HDR2) {
            /* 找到帧头 */
            if (i + 3 >= rx_wr) break;
            cmd = rx_buf[i + 2];
            len = rx_buf[i + 3];
            if (i + 4 + len >= rx_wr) break;

            /* 校验 */
            xor_calc = 0;
            xor_calc ^= FRAME_HDR1;
            xor_calc ^= FRAME_HDR2;
            xor_calc ^= cmd;
            xor_calc ^= len;
            uint8_t j;
            for (j = 0; j < len; j++) {
                data[j] = rx_buf[i + 4 + j];
                xor_calc ^= data[j];
            }
            xor_recv = rx_buf[i + 4 + len];

            if (xor_calc == xor_recv && cmd == CMD_TOUCH) {
                /* 触摸事件: [page] [x_hi] [x_lo] [y_hi] [y_lo] */
                if (data[0] <= 5 && len >= 5) {
                    uint8_t page = data[0];
                    uint16_t x = ((uint16_t)data[1] << 8) | data[2];
                    uint16_t y = ((uint16_t)data[3] << 8) | data[4];
                    if (touch_cb) touch_cb(page, x, y);
                    found = 1;
                }
            }

            /* 清除已处理的帧 */
            rx_wr = 0;
            break;
        }
    }

    /* 如果没有找到完整帧, 清除无效数据 */
    if (!found && rx_wr > RX_BUF_SIZE / 2) {
        /* 搜索并保留可能的帧头 */
        uint8_t k;
        rx_wr = 0;
        for (k = 0; k < RX_BUF_SIZE; k++) {
            if (rx_buf[k] == FRAME_HDR1) {
                rx_buf[0] = rx_buf[k];
                rx_wr = 1;
                break;
            }
        }
    }
}
