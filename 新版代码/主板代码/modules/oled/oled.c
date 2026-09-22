/**
 * @file    oled.c
 * @brief   0.96 寸 OLED 显示屏驱动实现 (SSD1306, 128×64, I2C)
 * @note    SSD1306 是一款单色 OLED 驱动芯片:
 *          - 128×64 像素, 1-bit 每像素
 *          - 内置 64×48 字节 GDDRAM (128×64/8 = 1024 字节)
 *          - I2C/SPI 接口
 *          - 支持硬件水平和垂直滚动
 *          
 *          本驱动使用 I2C1 (PB6 SCL, PB7 SDA, 400kHz),
 *          与 BH1750/PN532/MAX30100/APDS-9960 共用总线。
 *          
 *          帧缓冲区:
 *          使用 1024 字节的缓冲区 (128×64/8), 所有绘图操作
 *          先在缓冲区进行, 然后通过 OLED_Refresh() 批量刷到
 *          GDDRAM, 减少 I2C 通信次数。
 *          
 *          缓冲区映射 (页面模式):
 *          Page 0: (0,0) ~ (127,7)    行 0~7
 *          Page 1: (0,8) ~ (127,15)   行 8~15
 *          ...
 *          Page 7: (0,56) ~ (127,63)  行 56~63
 */
#include "oled.h"

#include <string.h>
#include <stdio.h>

/*==================================================================*
 *                      I2C 超时                                      *
 *==================================================================*/
#define I2C_TIMEOUT         50000

/*==================================================================*
 *                      帧缓冲区                                      *
 *==================================================================*/
static uint8_t frame_buf[OLED_BUFFER_SIZE];

/*==================================================================*
 *                      文本光标                                      *
 *==================================================================*/
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;
static uint8_t current_font = OLED_FONT_6X8;

/*==================================================================*
 *                      6×8 ASCII 字库 (0x20~0x7E)                   *
 *==================================================================*/
static const uint8_t font_6x8[][6] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00 },   /* 0x20 space */
    { 0x00,0x00,0x5F,0x00,0x00,0x00 },   /* 0x21 ! */
    { 0x00,0x07,0x00,0x07,0x00,0x00 },   /* 0x22 " */
    { 0x14,0xFF,0x14,0xFF,0x14,0x00 },   /* 0x23 # */
    { 0x24,0x2A,0xFF,0x2A,0x12,0x00 },   /* 0x24 $ */
    { 0x23,0x13,0x08,0x64,0x62,0x00 },   /* 0x25 % */
    { 0x36,0x49,0x55,0x22,0x50,0x00 },   /* 0x26 & */
    { 0x00,0x05,0x03,0x00,0x00,0x00 },   /* 0x27 ' */
    { 0x00,0x1C,0x22,0x41,0x00,0x00 },   /* 0x28 ( */
    { 0x00,0x41,0x22,0x1C,0x00,0x00 },   /* 0x29 ) */
    { 0x08,0x2A,0x1C,0x2A,0x08,0x00 },   /* 0x2A * */
    { 0x08,0x08,0x3E,0x08,0x08,0x00 },   /* 0x2B + */
    { 0x00,0x50,0x30,0x00,0x00,0x00 },   /* 0x2C , */
    { 0x08,0x08,0x08,0x08,0x08,0x00 },   /* 0x2D - */
    { 0x00,0x60,0x60,0x00,0x00,0x00 },   /* 0x2E . */
    { 0x20,0x10,0x08,0x04,0x02,0x00 },   /* 0x2F / */
    { 0x3E,0x51,0x49,0x45,0x3E,0x00 },   /* 0x30 0 */
    { 0x00,0x42,0x7F,0x40,0x00,0x00 },   /* 0x31 1 */
    { 0x42,0x61,0x51,0x49,0x46,0x00 },   /* 0x32 2 */
    { 0x21,0x41,0x45,0x4B,0x31,0x00 },   /* 0x33 3 */
    { 0x18,0x14,0x12,0x7F,0x10,0x00 },   /* 0x34 4 */
    { 0x27,0x45,0x45,0x45,0x39,0x00 },   /* 0x35 5 */
    { 0x3C,0x4A,0x49,0x49,0x30,0x00 },   /* 0x36 6 */
    { 0x01,0x71,0x09,0x05,0x03,0x00 },   /* 0x37 7 */
    { 0x36,0x49,0x49,0x49,0x36,0x00 },   /* 0x38 8 */
    { 0x06,0x49,0x49,0x29,0x1E,0x00 },   /* 0x39 9 */
    { 0x00,0x36,0x36,0x00,0x00,0x00 },   /* 0x3A : */
    { 0x00,0x56,0x36,0x00,0x00,0x00 },   /* 0x3B ; */
    { 0x00,0x08,0x14,0x22,0x41,0x00 },   /* 0x3C < */
    { 0x14,0x14,0x14,0x14,0x14,0x00 },   /* 0x3D = */
    { 0x41,0x22,0x14,0x08,0x00,0x00 },   /* 0x3E > */
    { 0x02,0x01,0x51,0x09,0x06,0x00 },   /* 0x3F ? */
    { 0x32,0x49,0x79,0x41,0x3E,0x00 },   /* 0x40 @ */
    { 0x7E,0x11,0x11,0x11,0x7E,0x00 },   /* 0x41 A */
    { 0x7F,0x49,0x49,0x49,0x36,0x00 },   /* 0x42 B */
    { 0x3E,0x41,0x41,0x41,0x22,0x00 },   /* 0x43 C */
    { 0x7F,0x41,0x41,0x22,0x1C,0x00 },   /* 0x44 D */
    { 0x7F,0x49,0x49,0x49,0x41,0x00 },   /* 0x45 E */
    { 0x7F,0x09,0x09,0x01,0x01,0x00 },   /* 0x46 F */
    { 0x3E,0x41,0x41,0x51,0x32,0x00 },   /* 0x47 G */
    { 0x7F,0x08,0x08,0x08,0x7F,0x00 },   /* 0x48 H */
    { 0x00,0x41,0x7F,0x41,0x00,0x00 },   /* 0x49 I */
    { 0x20,0x40,0x41,0x3F,0x01,0x00 },   /* 0x4A J */
    { 0x7F,0x08,0x14,0x22,0x41,0x00 },   /* 0x4B K */
    { 0x7F,0x40,0x40,0x40,0x40,0x00 },   /* 0x4C L */
    { 0x7F,0x02,0x04,0x02,0x7F,0x00 },   /* 0x4D M */
    { 0x7F,0x04,0x08,0x10,0x7F,0x00 },   /* 0x4E N */
    { 0x3E,0x41,0x41,0x41,0x3E,0x00 },   /* 0x4F O */
    { 0x7F,0x09,0x09,0x09,0x06,0x00 },   /* 0x50 P */
    { 0x3E,0x41,0x51,0x21,0x5E,0x00 },   /* 0x51 Q */
    { 0x7F,0x09,0x19,0x29,0x46,0x00 },   /* 0x52 R */
    { 0x46,0x49,0x49,0x49,0x31,0x00 },   /* 0x53 S */
    { 0x01,0x01,0x7F,0x01,0x01,0x00 },   /* 0x54 T */
    { 0x3F,0x40,0x40,0x40,0x3F,0x00 },   /* 0x55 U */
    { 0x07,0x18,0x60,0x18,0x07,0x00 },   /* 0x56 V */
    { 0x7F,0x20,0x18,0x20,0x7F,0x00 },   /* 0x57 W */
    { 0x63,0x14,0x08,0x14,0x63,0x00 },   /* 0x58 X */
    { 0x03,0x04,0x78,0x04,0x03,0x00 },   /* 0x59 Y */
    { 0x61,0x51,0x49,0x45,0x43,0x00 },   /* 0x5A Z */
    { 0x00,0x00,0x7F,0x41,0x41,0x00 },   /* 0x5B [ */
    { 0x02,0x04,0x08,0x10,0x20,0x00 },   /* 0x5C \ */
    { 0x41,0x41,0x7F,0x00,0x00,0x00 },   /* 0x5D ] */
    { 0x04,0x02,0x01,0x02,0x04,0x00 },   /* 0x5E ^ */
    { 0x40,0x40,0x40,0x40,0x40,0x00 },   /* 0x5F _ */
    { 0x00,0x01,0x02,0x04,0x00,0x00 },   /* 0x60 ` */
    { 0x20,0x54,0x54,0x54,0x78,0x00 },   /* 0x61 a */
    { 0x7F,0x48,0x44,0x44,0x38,0x00 },   /* 0x62 b */
    { 0x38,0x44,0x44,0x44,0x20,0x00 },   /* 0x63 c */
    { 0x38,0x44,0x44,0x48,0x7F,0x00 },   /* 0x64 d */
    { 0x38,0x54,0x54,0x54,0x18,0x00 },   /* 0x65 e */
    { 0x08,0x7E,0x09,0x01,0x02,0x00 },   /* 0x66 f */
    { 0x08,0x14,0x54,0x54,0x3C,0x00 },   /* 0x67 g */
    { 0x7F,0x08,0x04,0x04,0x78,0x00 },   /* 0x68 h */
    { 0x00,0x44,0x7D,0x40,0x00,0x00 },   /* 0x69 i */
    { 0x20,0x40,0x44,0x3D,0x00,0x00 },   /* 0x6A j */
    { 0x00,0x7F,0x10,0x28,0x44,0x00 },   /* 0x6B k */
    { 0x00,0x41,0x7F,0x40,0x00,0x00 },   /* 0x6C l */
    { 0x7C,0x04,0x18,0x04,0x78,0x00 },   /* 0x6D m */
    { 0x7C,0x08,0x04,0x04,0x78,0x00 },   /* 0x6E n */
    { 0x38,0x44,0x44,0x44,0x38,0x00 },   /* 0x6F o */
    { 0x7C,0x14,0x14,0x14,0x08,0x00 },   /* 0x70 p */
    { 0x08,0x14,0x14,0x18,0x7C,0x00 },   /* 0x71 q */
    { 0x7C,0x08,0x04,0x04,0x08,0x00 },   /* 0x72 r */
    { 0x48,0x54,0x54,0x54,0x20,0x00 },   /* 0x73 s */
    { 0x04,0x3F,0x44,0x40,0x20,0x00 },   /* 0x74 t */
    { 0x3C,0x40,0x40,0x20,0x7C,0x00 },   /* 0x75 u */
    { 0x1C,0x20,0x40,0x20,0x1C,0x00 },   /* 0x76 v */
    { 0x3C,0x40,0x30,0x40,0x3C,0x00 },   /* 0x77 w */
    { 0x44,0x28,0x10,0x28,0x44,0x00 },   /* 0x78 x */
    { 0x0C,0x50,0x50,0x50,0x3C,0x00 },   /* 0x79 y */
    { 0x44,0x64,0x54,0x4C,0x44,0x00 },   /* 0x7A z */
    { 0x00,0x08,0x36,0x41,0x00,0x00 },   /* 0x7B { */
    { 0x00,0x00,0x7F,0x00,0x00,0x00 },   /* 0x7C | */
    { 0x00,0x41,0x36,0x08,0x00,0x00 },   /* 0x7D } */
    { 0x02,0x01,0x02,0x04,0x02,0x00 },   /* 0x7E ~ */
};

/*==================================================================*
 *                      I2C 底层操作                                  *
 *==================================================================*/

static void i2c_init(void)
{
    I2C_InitTypeDef i2c;
    GPIO_InitTypeDef gpio;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_Init(GPIOB, &gpio);

    I2C_StructInit(&i2c);
    i2c.I2C_ClockSpeed          = OLED_I2C_SPEED;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_16_9;
    i2c.I2C_OwnAddress1         = 0x00;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(OLED_I2C, &i2c);

    I2C_Cmd(OLED_I2C, ENABLE);
}

static uint8_t i2c_write_raw(const uint8_t *data, uint16_t len)
{
    uint32_t tout;
    uint16_t i;

    I2C_GenerateSTART(OLED_I2C, ENABLE);
    tout = I2C_TIMEOUT;
    while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--tout == 0) {
            I2C_GenerateSTOP(OLED_I2C, ENABLE);
            return 1;
        }
    }

    I2C_Send7bitAddress(OLED_I2C, OLED_ADDR_WRITE, I2C_Direction_Transmitter);
    tout = I2C_TIMEOUT;
    while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (--tout == 0) {
            I2C_GenerateSTOP(OLED_I2C, ENABLE);
            return 1;
        }
    }

    for (i = 0; i < len; i++) {
        I2C_SendData(OLED_I2C, data[i]);
        tout = I2C_TIMEOUT;
        while (!I2C_CheckEvent(OLED_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            if (--tout == 0) {
                I2C_GenerateSTOP(OLED_I2C, ENABLE);
                return 1;
            }
        }
    }

    I2C_GenerateSTOP(OLED_I2C, ENABLE);
    return 0;
}

static void oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2];

    buf[0] = OLED_CTRL_CMD;
    buf[1] = cmd;
    i2c_write_raw(buf, 2);
}

static void oled_write_data(const uint8_t *data, uint16_t len)
{
    uint8_t *buf;
    uint16_t i;

    buf = (uint8_t *)data;
    (void)i;

    i2c_write_raw(data, len);
}

/*==================================================================*
 *                      模块初始化 (SSD1306 初始化序列)               *
 *==================================================================*/

uint8_t OLED_Init(void)
{
    uint32_t delay;

    i2c_init();

    oled_write_cmd(0xAE);        /* 关闭显示                    */
    oled_write_cmd(0xD5);        /* 设置时钟分频因子/振荡频率   */
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8);        /* 设置复用比 (MUX)            */
    oled_write_cmd(0x3F);        /* 64 MUX                      */
    oled_write_cmd(0xD3);        /* 设置显示偏移                */
    oled_write_cmd(0x00);
    oled_write_cmd(0x40);        /* 设置显示起始行 (行 0)       */
    oled_write_cmd(0x8D);        /* 电荷泵设置                  */
    oled_write_cmd(0x14);        /* 使能电荷泵                  */
    oled_write_cmd(0x20);        /* 设置内存地址模式            */
    oled_write_cmd(0x00);        /* 水平寻址模式                */
    oled_write_cmd(0xA1);        /* 段重映射 (列 127 = SEG0)    */
    oled_write_cmd(0xC8);        /* COM 扫描方向 (从 COM[N-1])  */
    oled_write_cmd(0xDA);        /* 设置 COM 引脚硬件配置       */
    oled_write_cmd(0x12);
    oled_write_cmd(0x81);        /* 设置对比度                  */
    oled_write_cmd(0xCF);
    oled_write_cmd(0xD9);        /* 设置预充电周期              */
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDB);        /* 设置 VCOMH 电压             */
    oled_write_cmd(0x40);
    oled_write_cmd(0xA4);        /* 全局显示 (遵循 RAM 内容)    */
    oled_write_cmd(0xA6);        /* 正常显示 (非反色)           */
    oled_write_cmd(0xAF);        /* 打开显示                    */

    OLED_Clear(OLED_BLACK);
    OLED_Refresh();

    cursor_x = 0;
    cursor_y = 0;
    current_font = OLED_FONT_6X8;

    return 0;
}

/*==================================================================*
 *                      帧缓冲区管理                                  *
 *==================================================================*/

void OLED_Clear(uint8_t color)
{
    uint16_t i;

    if (color == OLED_WHITE) {
        for (i = 0; i < OLED_BUFFER_SIZE; i++) {
            frame_buf[i] = 0xFF;
        }
    } else {
        for (i = 0; i < OLED_BUFFER_SIZE; i++) {
            frame_buf[i] = 0x00;
        }
    }
}

void OLED_Refresh(void)
{
    uint8_t i;
    uint8_t buf[OLED_BUFFER_SIZE + 1];

    for (i = 0; i < OLED_PAGES; i++) {
        uint16_t offset;
        uint8_t cmd_setcol[3];
        uint8_t j;

        cmd_setcol[0] = OLED_CTRL_CMD;
        cmd_setcol[1] = 0x21;        /* 设置列地址范围           */
        cmd_setcol[2] = 0;
        i2c_write_raw(cmd_setcol, 3);

        cmd_setcol[1] = 0x22;        /* 设置页地址范围           */
        cmd_setcol[2] = 0x7F;
        i2c_write_raw(cmd_setcol, 3);

        cmd_setcol[1] = 0xB0 | i;    /* 设置页起始地址           */
        cmd_setcol[2] = 0;
        i2c_write_raw(cmd_setcol, 2);

        buf[0] = OLED_CTRL_DATA;
        offset = i * OLED_WIDTH;
        for (j = 0; j < OLED_WIDTH; j++) {
            buf[1 + j] = frame_buf[offset + j];
        }
        i2c_write_raw(buf, OLED_WIDTH + 1);
    }
}

void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t color)
{
    uint16_t idx;

    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;

    idx = (uint16_t)x + ((uint16_t)(y / 8) * OLED_WIDTH);

    if (color == OLED_WHITE) {
        frame_buf[idx] |= (uint8_t)(1 << (y % 8));
    } else {
        frame_buf[idx] &= (uint8_t)(~(1 << (y % 8)));
    }
}

uint8_t OLED_GetPixel(uint8_t x, uint8_t y)
{
    uint16_t idx;

    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return OLED_BLACK;

    idx = (uint16_t)x + ((uint16_t)(y / 8) * OLED_WIDTH);

    if (frame_buf[idx] & (1 << (y % 8))) {
        return OLED_WHITE;
    }
    return OLED_BLACK;
}

void OLED_InvertBuffer(void)
{
    uint16_t i;

    for (i = 0; i < OLED_BUFFER_SIZE; i++) {
        frame_buf[i] = (uint8_t)(~frame_buf[i]);
    }
}

/*==================================================================*
 *                      图形绘制 (Bresenham 算法)                    *
 *==================================================================*/

void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2,
                   uint8_t color)
{
    int16_t dx, dy, sx, sy, err, e2;

    dx = (int16_t)((x1 < x2) ? (x2 - x1) : (x1 - x2));
    dy = (int16_t)((y1 < y2) ? (y2 - y1) : (y1 - y2));
    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;
    err = dx - dy;

    while (1) {
        OLED_DrawPixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = (int16_t)(err * 2);
        if (e2 > -dy) {
            err -= dy;
            x1 = (uint8_t)(x1 + sx);
        }
        if (e2 < dx) {
            err += dx;
            y1 = (uint8_t)(y1 + sy);
        }
    }
}

void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                   uint8_t color)
{
    uint8_t x2, y2;

    x2 = (uint8_t)(x + w - 1);
    y2 = (uint8_t)(y + h - 1);

    OLED_DrawLine(x, y, x2, y, color);
    OLED_DrawLine(x2, y, x2, y2, color);
    OLED_DrawLine(x2, y2, x, y2, color);
    OLED_DrawLine(x, y2, x, y, color);
}

void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                   uint8_t color)
{
    uint8_t i, j;

    for (i = 0; i < w; i++) {
        for (j = 0; j < h; j++) {
            OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + j), color);
        }
    }
}

void OLED_DrawRoundRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                        uint8_t r, uint8_t color)
{
    uint8_t x2, y2;

    x2 = (uint8_t)(x + w - 1);
    y2 = (uint8_t)(y + h - 1);

    OLED_DrawLine((uint8_t)(x + r), y, (uint8_t)(x2 - r), y, color);
    OLED_DrawLine(x2, (uint8_t)(y + r), x2, (uint8_t)(y2 - r), color);
    OLED_DrawLine((uint8_t)(x2 - r), y2, (uint8_t)(x + r), y2, color);
    OLED_DrawLine(x, (uint8_t)(y2 - r), x, (uint8_t)(y + r), color);

    {
        int16_t f, ddF_x, ddF_y, xc, yc;

        f = 1 - (int16_t)r;
        ddF_x = 1;
        ddF_y = (int16_t)(-2 * r);
        xc = 0;
        yc = (int16_t)r;

        while (xc < yc) {
            if (f >= 0) {
                yc--;
                ddF_y += 2;
                f += ddF_y;
            }
            xc++;
            ddF_x += 2;
            f += ddF_x;

            OLED_DrawPixel((uint8_t)(x + r - (uint8_t)xc), (uint8_t)(y + r - (uint8_t)yc), color);
            OLED_DrawPixel((uint8_t)(x2 - r + (uint8_t)xc), (uint8_t)(y + r - (uint8_t)yc), color);
            OLED_DrawPixel((uint8_t)(x + r - (uint8_t)xc), (uint8_t)(y2 - r + (uint8_t)yc), color);
            OLED_DrawPixel((uint8_t)(x2 - r + (uint8_t)xc), (uint8_t)(y2 - r + (uint8_t)yc), color);
            OLED_DrawPixel((uint8_t)(x + r - (uint8_t)yc), (uint8_t)(y + r - (uint8_t)xc), color);
            OLED_DrawPixel((uint8_t)(x2 - r + (uint8_t)yc), (uint8_t)(y + r - (uint8_t)xc), color);
            OLED_DrawPixel((uint8_t)(x + r - (uint8_t)yc), (uint8_t)(y2 - r + (uint8_t)xc), color);
            OLED_DrawPixel((uint8_t)(x2 - r + (uint8_t)yc), (uint8_t)(y2 - r + (uint8_t)xc), color);
        }
    }
}

void OLED_DrawTriangle(uint8_t x1, uint8_t y1,
                       uint8_t x2, uint8_t y2,
                       uint8_t x3, uint8_t y3,
                       uint8_t color)
{
    OLED_DrawLine(x1, y1, x2, y2, color);
    OLED_DrawLine(x2, y2, x3, y3, color);
    OLED_DrawLine(x3, y3, x1, y1, color);
}

static void swap_uint8(uint8_t *a, uint8_t *b)
{
    uint8_t t;

    t = *a;
    *a = *b;
    *b = t;
}

void OLED_FillTriangle(uint8_t x1, uint8_t y1,
                       uint8_t x2, uint8_t y2,
                       uint8_t x3, uint8_t y3,
                       uint8_t color)
{
    int16_t x, y, sa, sb;
    int16_t y_min, y_max;

    if (y1 > y2) { swap_uint8(&x1, &x2); swap_uint8(&y1, &y2); }
    if (y1 > y3) { swap_uint8(&x1, &x3); swap_uint8(&y1, &y3); }
    if (y2 > y3) { swap_uint8(&x2, &x3); swap_uint8(&y2, &y3); }

    y_min = (int16_t)y1;
    y_max = (int16_t)y3;

    for (y = y_min; y <= y_max; y++) {
        if (y <= (int16_t)y2) {
            sa = ((int16_t)y - (int16_t)y1) * ((int16_t)x2 - (int16_t)x1) /
                 ((int16_t)y2 - (int16_t)y1 + 1) + (int16_t)x1;
        } else {
            sa = ((int16_t)y - (int16_t)y2) * ((int16_t)x3 - (int16_t)x2) /
                 ((int16_t)y3 - (int16_t)y2 + 1) + (int16_t)x2;
        }
        sb = ((int16_t)y - (int16_t)y1) * ((int16_t)x3 - (int16_t)x1) /
             ((int16_t)y3 - (int16_t)y1 + 1) + (int16_t)x1;

        if (sa > sb) {
            int16_t t;
            t = sa; sa = sb; sb = t;
        }

        for (x = sa; x <= sb; x++) {
            OLED_DrawPixel((uint8_t)x, (uint8_t)y, color);
        }
    }
}

void OLED_DrawCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color)
{
    int16_t f, ddF_x, ddF_y, x, y;

    f = 1 - (int16_t)r;
    ddF_x = 1;
    ddF_y = (int16_t)(-2 * r);
    x = 0;
    y = (int16_t)r;

    OLED_DrawPixel(cx, (uint8_t)(cy + (uint8_t)r), color);
    OLED_DrawPixel(cx, (uint8_t)(cy - (uint8_t)r), color);
    OLED_DrawPixel((uint8_t)(cx + (uint8_t)r), cy, color);
    OLED_DrawPixel((uint8_t)(cx - (uint8_t)r), cy, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        OLED_DrawPixel((uint8_t)(cx + (uint8_t)x), (uint8_t)(cy + (uint8_t)y), color);
        OLED_DrawPixel((uint8_t)(cx - (uint8_t)x), (uint8_t)(cy + (uint8_t)y), color);
        OLED_DrawPixel((uint8_t)(cx + (uint8_t)x), (uint8_t)(cy - (uint8_t)y), color);
        OLED_DrawPixel((uint8_t)(cx - (uint8_t)x), (uint8_t)(cy - (uint8_t)y), color);
        OLED_DrawPixel((uint8_t)(cx + (uint8_t)y), (uint8_t)(cy + (uint8_t)x), color);
        OLED_DrawPixel((uint8_t)(cx - (uint8_t)y), (uint8_t)(cy + (uint8_t)x), color);
        OLED_DrawPixel((uint8_t)(cx + (uint8_t)y), (uint8_t)(cy - (uint8_t)x), color);
        OLED_DrawPixel((uint8_t)(cx - (uint8_t)y), (uint8_t)(cy - (uint8_t)x), color);
    }
}

void OLED_FillCircle(uint8_t cx, uint8_t cy, uint8_t r, uint8_t color)
{
    int16_t f, ddF_x, ddF_y, x, y;

    f = 1 - (int16_t)r;
    ddF_x = 1;
    ddF_y = (int16_t)(-2 * r);
    x = 0;
    y = (int16_t)r;

    {
        int16_t i;
        for (i = (int16_t)(cy - r); i <= (int16_t)(cy + r); i++) {
            OLED_DrawPixel(cx, (uint8_t)i, color);
        }
    }

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        {
            int16_t i;
            for (i = (int16_t)(cy - y); i <= (int16_t)(cy + y); i++) {
                OLED_DrawPixel((uint8_t)(cx + (uint8_t)x), (uint8_t)i, color);
                OLED_DrawPixel((uint8_t)(cx - (uint8_t)x), (uint8_t)i, color);
            }
            for (i = (int16_t)(cy - x); i <= (int16_t)(cy + x); i++) {
                OLED_DrawPixel((uint8_t)(cx + (uint8_t)y), (uint8_t)i, color);
                OLED_DrawPixel((uint8_t)(cx - (uint8_t)y), (uint8_t)i, color);
            }
        }
    }
}

/*==================================================================*
 *                      文本显示                                      *
 *==================================================================*/

void OLED_SetFont(uint8_t font)
{
    if (font == OLED_FONT_6X8 || font == OLED_FONT_8X16) {
        current_font = font;
    }
}

void OLED_SetCursor(uint8_t x, uint8_t y)
{
    cursor_x = x;
    cursor_y = y;
}

uint8_t OLED_GetCursorX(void)
{
    return cursor_x;
}

uint8_t OLED_GetCursorY(void)
{
    return cursor_y;
}

uint8_t OLED_DrawChar(uint8_t x, uint8_t y, char ch, uint8_t font)
{
    uint8_t i, j;
    uint8_t c;

    c = (uint8_t)ch;

    if (c < 0x20 || c > 0x7E) {
        c = 0x20;
    }

    if (font == OLED_FONT_6X8) {
        uint8_t idx;

        idx = (uint8_t)(c - 0x20);
        for (i = 0; i < 6; i++) {
            uint8_t line;

            line = font_6x8[idx][i];
            for (j = 0; j < 8; j++) {
                if (line & (1 << j)) {
                    OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + j), OLED_WHITE);
                } else {
                    OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + j), OLED_BLACK);
                }
            }
        }
        return 6;
    }

    return 0;
}

void OLED_PutChar(char ch)
{
    uint8_t char_w;

    if (ch == '\n') {
        cursor_y = (uint8_t)(cursor_y + ((current_font == OLED_FONT_6X8) ? 8 : 16));
        cursor_x = 0;
        return;
    }
    if (ch == '\r') {
        cursor_x = 0;
        return;
    }

    char_w = OLED_DrawChar(cursor_x, cursor_y, ch, current_font);

    cursor_x = (uint8_t)(cursor_x + char_w + 1);

    if (cursor_x >= OLED_WIDTH) {
        cursor_x = 0;
        cursor_y = (uint8_t)(cursor_y + ((current_font == OLED_FONT_6X8) ? 8 : 16));
    }
    if (cursor_y >= OLED_HEIGHT) {
        cursor_y = 0;
    }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *str, uint8_t font)
{
    while (*str) {
        uint8_t char_w;

        char_w = OLED_DrawChar(x, y, *str, font);
        x = (uint8_t)(x + char_w + 1);
        if (x + 6 >= OLED_WIDTH) {
            x = 0;
            y = (uint8_t)(y + ((font == OLED_FONT_6X8) ? 8 : 16));
        }
        if (y >= OLED_HEIGHT) {
            y = 0;
        }
        str++;
    }
}

void OLED_Print(const char *str)
{
    while (*str) {
        OLED_PutChar(*str);
        str++;
    }
}

void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len,
                  uint8_t font)
{
    uint8_t i;
    uint8_t show;
    uint8_t digit;
    uint8_t char_w;

    show = 0;
    for (i = 0; i < len; i++) {
        uint32_t divisor;

        divisor = 1;
        {
            uint8_t k;
            for (k = 0; k < (len - 1 - i); k++) {
                divisor *= 10;
            }
        }
        digit = (uint8_t)((num / divisor) % 10);

        if (digit != 0 || show || i == len - 1) {
            show = 1;
        }

        if (show) {
            char_w = OLED_DrawChar(x, y, (char)('0' + digit), font);
            x = (uint8_t)(x + char_w + 1);
        }
    }
}

void OLED_ShowFloat(uint8_t x, uint8_t y, double num,
                    uint8_t int_len, uint8_t dec_len, uint8_t font)
{
    uint32_t int_part;
    uint32_t dec_part;
    uint8_t char_w;
    uint8_t i;
    uint32_t multiplier;

    if (num < 0) {
        char_w = OLED_DrawChar(x, y, '-', font);
        x = (uint8_t)(x + char_w + 1);
        num = -num;
    }

    int_part = (uint32_t)num;

    multiplier = 1;
    for (i = 0; i < dec_len; i++) {
        multiplier *= 10;
    }
    dec_part = (uint32_t)((num - (double)int_part) * (double)multiplier + 0.5);

    OLED_ShowNum(x, y, int_part, int_len, font);

    {
        uint8_t len;
        uint8_t k;

        len = 0;
        {
            uint32_t tmp = dec_part;
            while (tmp > 0) { len++; tmp /= 10; }
        }
        if (len < dec_len) len = dec_len;

        for (k = 0; k < int_len; k++) {
            uint32_t div = 1;
            uint8_t m;
            for (m = 0; m < (int_len - 1 - k); m++) div *= 10;
            if (int_part / div > 0 || k == int_len - 1) {
                x = (uint8_t)(x + ((font == OLED_FONT_6X8) ? 7 : 9));
                break;
            }
        }
    }

    OLED_DrawChar(x, y, '.', font);
    x = (uint8_t)(x + ((font == OLED_FONT_6X8) ? 7 : 9));

    OLED_ShowNum(x, y, dec_part, dec_len, font);
}

/*==================================================================*
 *                      高级功能                                      *
 *==================================================================*/

void OLED_SetRotate(uint8_t rotate)
{
    switch (rotate % 4) {
        case 0:
            oled_write_cmd(0xA1);    /* 列 127 → SEG0              */
            oled_write_cmd(0xC8);    /* COM[N-1] → COM0            */
            break;
        case 1:                      /* 90° 顺时针                  */
            oled_write_cmd(0xA1);
            oled_write_cmd(0xC0);
            break;
        case 2:                      /* 180°                        */
            oled_write_cmd(0xA0);
            oled_write_cmd(0xC0);
            break;
        case 3:                      /* 270°                        */
            oled_write_cmd(0xA0);
            oled_write_cmd(0xC8);
            break;
    }
}

void OLED_SetContrast(uint8_t contrast)
{
    oled_write_cmd(0x81);
    oled_write_cmd(contrast);
}

void OLED_Invert(uint8_t invert)
{
    if (invert) {
        oled_write_cmd(0xA7);
    } else {
        oled_write_cmd(0xA6);
    }
}

void OLED_DisplayOn(void)
{
    oled_write_cmd(0xAF);
}

void OLED_DisplayOff(void)
{
    oled_write_cmd(0xAE);
}

void OLED_Scroll(uint8_t lines)
{
    uint8_t i, j;

    if (lines == 0) return;

    if (lines >= OLED_HEIGHT) {
        OLED_Clear(OLED_BLACK);
        return;
    }

    if (lines > 0) {
        for (i = 0; i < OLED_HEIGHT - lines; i++) {
            for (j = 0; j < OLED_WIDTH; j++) {
                uint8_t p;

                p = OLED_GetPixel(j, (uint8_t)(i + lines));
                if (p) {
                    OLED_DrawPixel(j, i, OLED_WHITE);
                } else {
                    OLED_DrawPixel(j, i, OLED_BLACK);
                }
            }
        }
        for (j = 0; j < OLED_WIDTH; j++) {
            uint8_t k;
            for (k = OLED_HEIGHT - lines; k < OLED_HEIGHT; k++) {
                OLED_DrawPixel(j, k, OLED_BLACK);
            }
        }
    } else {
        int16_t s = (int16_t)(-lines);
        uint8_t k;

        for (i = 0; i < (uint8_t)(OLED_HEIGHT - (uint8_t)s); i++) {
            uint8_t src;

            src = (uint8_t)(i + (uint8_t)s);
            for (j = 0; j < OLED_WIDTH; j++) {
                uint8_t p;

                p = OLED_GetPixel(j, src);
                if (p) {
                    OLED_DrawPixel(j, (uint8_t)(i + (uint8_t)s), OLED_WHITE);
                } else {
                    OLED_DrawPixel(j, (uint8_t)(i + (uint8_t)s), OLED_BLACK);
                }
            }
        }
        for (j = 0; j < OLED_WIDTH; j++) {
            for (k = 0; k < (uint8_t)s; k++) {
                OLED_DrawPixel(j, k, OLED_BLACK);
            }
        }
    }
}

void OLED_HWScroll(uint8_t direction, uint8_t start_page,
                   uint8_t end_page, uint8_t speed)
{
    if (speed > 7) speed = 7;

    oled_write_cmd(0x26 | (direction ? 0x01 : 0x00));
    oled_write_cmd(0x00);
    oled_write_cmd(start_page);
    oled_write_cmd(speed);
    oled_write_cmd(end_page);
    oled_write_cmd(0x00);
    oled_write_cmd(0xFF);
    oled_write_cmd(0x2F);    /* 开始滚动 */
}

void OLED_StopScroll(void)
{
    oled_write_cmd(0x2E);
}


