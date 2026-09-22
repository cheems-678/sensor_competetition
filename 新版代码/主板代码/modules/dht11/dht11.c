/**
 * @file    dht11.c
 * @brief   单总线温湿度传感器驱动实现 (DHT11 / AM2302-DHT22, 自动识别)
 *
 * 单总线时序 (DATA 需外部4.7k上拉到3V3):
 *   1) 主机起始: 拉低 DATA 一段时间 (DHT11 ≥18ms, AM2302 ≥1ms) 后释放
 *   2) 器件应答: 拉低 80us -> 拉高 80us
 *   3) 40位数据: 每位 = 50us低电平 + 高电平
 *        高电平 26~28us = 0 ;  高电平 70us = 1
 *   4) 末位结束: 器件拉低 50us 后释放总线(上拉恢复高)
 *
 * 判位方式:
 *   - DWT 周期计数器可用时(72MHz, 见 dwt_us.h): 高电平 >45us 即判 1, 精确;
 *   - DWT 不可用时: 改用"自适应阈值"(取40位高电平宽度的最小值与最大值的中点),
 *     对CPU主频/循环速度不敏感, 只是极端帧(全0或全1)可能误判。
 *
 * 时间基与器件类型均由 config.h 提供, 本文件与主板/从板通用。
 */

#include "dht11.h"
#include "../../config.h"

/* ================= 引脚宏 ================= */
#define TH_LOW()     GPIO_ResetBits(TH_DATA_PORT, TH_DATA_PIN)
#define TH_HIGH()    GPIO_SetBits(TH_DATA_PORT, TH_DATA_PIN)
#define TH_READ()    ((GPIO_ReadInputDataBit(TH_DATA_PORT, TH_DATA_PIN) != Bit_RESET) ? 1 : 0)

#define TH_TIMEOUT   0xFFFFFFFFUL

/* ================= 内部状态 ================= */
static uint8_t  th_type_runtime = 0xFF;   /* 已识别到的类型: 0=DHT11 1=AM2302 0xFF=未知 */
static uint8_t  tb_dwt;                   /* 1=用DWT周期计数计时 */
static uint32_t tb_fake;                  /* DWT不可用时的单调计数 */
static uint32_t tb_per_us;                /* 每微秒的刻度数 */

/* ================= 时间基 ================= */
static void tb_init(void)
{
    tb_dwt     = SysTick_DwtOk();
    tb_per_us  = tb_dwt ? (SYS_CLK / 1000000UL) : 8UL;   /* 无DWT时按"1us≈8次轮询"粗估 */
}

static uint32_t tb_now(void)
{
    if (tb_dwt) return DWT_US_CYCLES();
    return ++tb_fake;                 /* 每调用一次+1, 等效"轮询次数"计时 */
}

#define TB_US(us)   ((uint32_t)(us) * tb_per_us)

/* ================= 引脚方向切换 ================= */
static void th_pin_out(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = TH_DATA_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TH_DATA_PORT, &g);
}

static void th_pin_in(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = TH_DATA_PIN;
    g.GPIO_Mode  = GPIO_Mode_IPU;         /* 输入上拉, 配合外部4.7k */
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TH_DATA_PORT, &g);
}

/**
 * @brief 等待引脚出现指定电平
 * @param want_high 1=等高电平 0=等低电平
 * @param limit_ticks 超时(以时间基刻度计)
 * @return 等待耗时(刻度), 超时返回 TH_TIMEOUT
 */
static uint32_t th_wait(uint8_t want_high, uint32_t limit_ticks)
{
    uint32_t t0 = tb_now();

    while (TH_READ() != want_high) {
        if ((uint32_t)(tb_now() - t0) > limit_ticks) return TH_TIMEOUT;
    }
    return (uint32_t)(tb_now() - t0);
}

/**
 * @brief 读一帧原始40位
 * @param start_low_ms 主机起始拉低时间
 * @param high_ticks   输出: 40位各自的高电平宽度(时间基刻度)
 * @return 0=成功 1=失败
 */
static uint8_t th_read_frame(uint16_t start_low_ms, uint16_t *high_ticks)
{
    uint8_t  i;
    uint32_t w;

    /* 1. 主机起始信号 */
    th_pin_out();
    TH_LOW();
    SysTick_Delay_Ms(start_low_ms);
    TH_HIGH();                    /* 释放总线 */
    th_pin_in();

    /* 2. 器件应答: 80us低 -> 80us高 */
    if (th_wait(0, TB_US(300)) == TH_TIMEOUT) return 1;
    if (th_wait(1, TB_US(300)) == TH_TIMEOUT) return 1;
    if (th_wait(0, TB_US(300)) == TH_TIMEOUT) return 1;   /* 80us高结束 */

    /* 3. 40位数据 */
    for (i = 0; i < 40; i++) {
        if (th_wait(1, TB_US(150)) == TH_TIMEOUT) return 1;  /* 等50us低结束 */
        w = th_wait(0, TB_US(200));                          /* 测高电平宽度 */
        if (w == TH_TIMEOUT) return 1;
        high_ticks[i] = (w > 0xFFFEUL) ? 0xFFFEU : (uint16_t)w;
    }
    return 0;
}

/**
 * @brief 把40位原始宽度转成5字节数据
 * @note  判位: DWT可用 -> 绝对门限45us; 否则 -> 自适应门限(最大/最小中点)
 */
static void th_bits_to_bytes(const uint16_t *high_ticks, uint8_t *out)
{
    uint8_t  i, j;
    uint16_t thr;

    if (tb_dwt) {
        thr = (uint16_t)TB_US(45);
    } else {
        uint16_t mn = 0xFFFF, mx = 0;
        for (i = 0; i < 40; i++) {
            if (high_ticks[i] < mn) mn = high_ticks[i];
            if (high_ticks[i] > mx) mx = high_ticks[i];
        }
        if (mx == mn) mn = 0;                       /* 退化保护 */
        thr = (uint16_t)(mn + (uint16_t)((mx - mn) / 2));
    }

    for (j = 0; j < 5; j++) {
        uint8_t b = 0;
        for (i = 0; i < 8; i++) {
            b <<= 1;
            if (high_ticks[j * 8 + i] > thr) b |= 1;
        }
        out[j] = b;
    }
}

/* ================= 对外接口 ================= */

void DHT_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    th_pin_in();          /* 空闲: 输入上拉(总线被外部上拉保持高) */
    tb_init();

    /* 器件上电稳定 (规格书要求≥1s) */
    SysTick_Delay_Ms(1100);
}

uint8_t DHT_GetType(void) { return th_type_runtime; }

const char* DHT_TypeName(uint8_t type)
{
    if (type == TH_TYPE_DHT11)  return "DHT11";
    if (type == TH_TYPE_AM2302) return "AM2302";
    return "UNKNOWN";
}

uint8_t DHT_Read(DHT_Data *d)
{
    uint16_t high_ticks[40];
    uint8_t  b[5];
    uint8_t  attempt, got = 0, type = 0xFF, tmp_type;

    if (d == 0) return 1;
    d->valid = 0;

    /* 起始拉低时间: AM2302 用 2ms(DHT11 亦可兼容), DHT11 用 20ms。
       未识别出类型时先按 AM2302 时序试一次, 失败再按 DHT11 时序试。 */
    for (attempt = 0; attempt < 2 && !got; attempt++) {
        uint16_t start_ms;

        if (th_type_runtime == TH_TYPE_DHT11)       start_ms = 20;
        else if (th_type_runtime == TH_TYPE_AM2302) start_ms = 2;
        else                                        start_ms = (attempt == 0) ? 2 : 20;

        if (th_read_frame(start_ms, high_ticks) != 0) continue;

        th_bits_to_bytes(high_ticks, b);

        /* 校验和 */
        if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) continue;

        /* 识别器件: DHT11 的小数字节恒为0 */
        if (th_type_runtime != 0xFF)              tmp_type = th_type_runtime;
        else if (b[1] == 0 && b[3] == 0)          tmp_type = TH_TYPE_DHT11;
        else                                      tmp_type = TH_TYPE_AM2302;

#if (TH_SENSOR_TYPE == TH_TYPE_DHT11)
        tmp_type = TH_TYPE_DHT11;
#elif (TH_SENSOR_TYPE == TH_TYPE_AM2302)
        tmp_type = TH_TYPE_AM2302;
#endif

        if (tmp_type == TH_TYPE_DHT11) {
            /* 整数分辨率: 湿度=20~90%, 温度=0~50℃ */
            if (b[0] < 1 || b[0] > 100) continue;
            if (b[2] > 60) continue;
            d->hum_pct  = (float)b[0];
            d->temp_c   = (float)b[2];
        } else {
            /* 0.1 分辨率, 温度为有符号16位 */
            float hum = (float)(((uint16_t)b[0] << 8) | b[1]) / 10.0f;
            float tem = (float)(((uint16_t)(b[2] & 0x7F) << 8) | b[3]) / 10.0f;
            if (b[2] & 0x80) tem = -tem;
            if (hum < 0.0f || hum > 100.0f) continue;
            if (tem < -45.0f || tem > 90.0f) continue;
            d->hum_pct  = hum;
            d->temp_c   = tem;
        }

        type = tmp_type;
        got  = 1;
    }

    if (!got) return 1;

    th_type_runtime = type;
    d->type  = type;
    d->valid = 1;
    return 0;
}
