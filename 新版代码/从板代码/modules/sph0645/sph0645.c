/**
 * @file    sph0645.c
 * @brief   SPH0645LM4H 双路数字麦克风驱动实现 (I2S2 + DMA, STM32F103 SPL)
 *
 * 关键点:
 *  1. STM32F103 的 I2S 时钟源 = SYSCLK(72MHz), 由 SPL 的 I2S_Init() 内部按
 *     RCC_GetClocksFreq() 自动算分频, 不需要配置 PLL3 (PLL3 是互联型F105/107的)。
 *  2. SPH0645LM4H 内部自带时钟, 不需要 MCLK, 只要 BCLK + WS 即可出数据,
 *     所以 I2S_MCLKOutput 关闭 (PC6 也就不用接)。
 *  3. I2S 一上电就持续输出 BCLK/WS(麦克风靠它工作), 数据靠 DMA 按需搬运;
 *     每次启动 DMA 前清一次 OVR, 避免上一次溢出影响。
 *  4. 16bit 帧时每声道 16 个 BCLK, 一帧 32 个 BCLK = 512kHz, 在 SPH0645
 *     允许的 BCLK 范围内。
 *
 * 数据格式说明: SPH0645 实际输出 24bit, 在 16bit 槽里取到的是高16位,
 *   并且它比标准 I2S 晚一个 BCLK 出数, 所以绝对值会比真实声压小/有偏。
 *   本项目只做能量/频率/事件判定, 用相对值即可, 无需精确标定;
 *   若要精确声压级, 改用 I2S_DataFormat_24b/32b 并做相应移位标定。
 */

#include "sph0645.h"

/* DMA 缓冲: 左右交织, 总点数 = 每声道点数 × 2 */
static int16_t snd_buf[MIC_BUF_SAMPLES];

/* ---------------- I2S2 初始化 ---------------- */
void Mic_Init(void)
{
    GPIO_InitTypeDef gpio;
    I2S_InitTypeDef  i2s;
    DMA_InitTypeDef  dma;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* PB13 = I2S2_CK(BCLK) ; PB12 = I2S2_WS(LRCLK)   —— 主机输出 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = GPIO_Pin_12 | GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* PB15 = I2S2_SD(麦克风数据) —— 主机接收, 输入 */
    gpio.GPIO_Pin  = GPIO_Pin_15;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

    /* DMA1_Channel4 = SPI2/I2S2 接收通道 */
    DMA_DeInit(DMA1_Channel4);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)(&(SPI2->DR));
    dma.DMA_MemoryBaseAddr     = (uint32_t)snd_buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;      /* 外设 -> 内存 */
    dma.DMA_BufferSize         = MIC_BUF_SAMPLES;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Normal;            /* 每次采集重启 */
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel4, &dma);
    DMA_Cmd(DMA1_Channel4, DISABLE);

    /* I2S2: 主模式接收, Philips标准, 16bit, 16kHz, 不用MCLK */
    I2S_StructInit(&i2s);
    i2s.I2S_Mode       = I2S_Mode_MasterRx;
    i2s.I2S_Standard   = I2S_Standard_Phillips;
    i2s.I2S_DataFormat = I2S_DataFormat_16b;
    i2s.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
    i2s.I2S_AudioFreq  = I2S_AudioFreq_16k;
    i2s.I2S_CPOL       = I2S_CPOL_Low;
    I2S_Init(SPI2, &i2s);

    /* 使能 I2S 的DMA接收请求, 然后启动 I2S (时钟持续输出) */
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);
    I2S_Cmd(SPI2, ENABLE);
}

/* ---------------- 清溢出标志 (读DR+SR即可清OVR) ---------------- */
static void i2s_clear_ovr(void)
{
    volatile uint16_t t;
    t = SPI2->DR;
    t = SPI2->SR;
    (void)t;
}

const int16_t* Mic_GetRawBuffer(void) { return snd_buf; }
uint16_t Mic_GetFrameSamples(void)    { return MIC_FRAME_SAMPLES; }

/* ---------------- 特征提取 ---------------- */
static void mic_analyze(MicFrame *f)
{
    const uint16_t n = MIC_FRAME_SAMPLES;
    uint16_t i, k;
    uint16_t bucket = (uint16_t)(n / MIC_ENV_POINTS);
    uint16_t bmax[MIC_ENV_POINTS];
    uint16_t envmax = 0;
    uint16_t ev_thr[2];

    int32_t  dc[2] = {0, 0};
    float    fsq[2] = {0.0f, 0.0f};
    uint16_t pk[2] = {0, 0};
    uint16_t zc[2] = {0, 0};
    uint16_t ev[2] = {0, 0};
    int16_t  prev[2] = {0, 0};
    uint8_t  above[2] = {0, 0};
    uint16_t hy[2];

    if (bucket == 0) bucket = 1;

    /* 1. 各通道直流分量 (SPH0645 输出带固定偏置, 必须去掉) */
    for (k = 0; k < 2; k++) {
        int32_t s = 0;
        for (i = 0; i < n; i++) s += (int32_t)snd_buf[i * 2 + k];
        dc[k] = s / (int32_t)n;
    }

    /* 2. 去直流后的 均方/峰值 统计 */
    for (i = 0; i < n; i++) {
        for (k = 0; k < 2; k++) {
            int32_t v  = (int32_t)snd_buf[i * 2 + k] - dc[k];
            int32_t av = (v < 0) ? -v : v;

            fsq[k] += (float)v * (float)v;
            if (av > 32767) av = 32767;
            if ((uint16_t)av > pk[k]) pk[k] = (uint16_t)av;
        }
    }

    /* 3. 用峰值定各自的门限(过零滞回门限 / 事件门限) */
    for (k = 0; k < 2; k++) {
        hy[k]    = (uint16_t)(pk[k] / 8);
        if (hy[k] < 20) hy[k] = 20;
        ev_thr[k] = (uint16_t)(pk[k] / 3);
        if (ev_thr[k] < MIC_RMS_QUIET) ev_thr[k] = MIC_RMS_QUIET;
    }

    /* 4. 带滞回统计过零次数(估算主频)与事件串数(虫鸣/啃食脉冲) */
    for (k = 0; k < 2; k++) { zc[k] = 0; ev[k] = 0; above[k] = 0; prev[k] = 0; }

    for (i = 0; i < n; i++) {
        for (k = 0; k < 2; k++) {
            int32_t v = (int32_t)snd_buf[i * 2 + k] - dc[k];

            if (v > (int32_t)hy[k]) {
                if (prev[k] == 0) zc[k]++;      /* 上升穿过 +门限, 计一次 */
                prev[k] = 1;
            } else if (v < -(int32_t)hy[k]) {
                prev[k] = 0;                    /* 落到负门限以下, 允许下次计数 */
            }
            /* 事件: 幅度超过事件门限的一"串"计一次 */
            if ((v > (int32_t)ev_thr[k]) || (v < -(int32_t)ev_thr[k])) {
                if (!above[k]) { above[k] = 1; ev[k]++; }
            } else if ((v < (int32_t)ev_thr[k]) && (v > -(int32_t)ev_thr[k])) {
                above[k] = 0;
            }
        }
    }

    /* 5. 包络: 每 bucket 个采样取 (左+右)/2 的绝对值最大值 */
    for (i = 0; i < MIC_ENV_POINTS; i++) bmax[i] = 0;
    for (i = 0; i < n; i++) {
        uint16_t bi = (uint16_t)(i / bucket);
        int32_t  m  = ((int32_t)snd_buf[i * 2] - dc[0] + (int32_t)snd_buf[i * 2 + 1] - dc[1]) / 2;
        if (m < 0) m = -m;
        if (bi < MIC_ENV_POINTS) {
            if ((uint16_t)m > bmax[bi]) bmax[bi] = (uint16_t)m;
        }
    }
    for (i = 0; i < MIC_ENV_POINTS; i++) if (bmax[i] > envmax) envmax = bmax[i];
    for (i = 0; i < MIC_ENV_POINTS; i++) {
        f->env[i] = (envmax > 0) ? (uint8_t)(((uint32_t)bmax[i] * 255u) / envmax) : 0;
    }

    /* 6. 汇总 */
    for (k = 0; k < 2; k++) {
        float rms = sqrtf(fsq[k] / (float)n);
        uint16_t freq = (uint16_t)(((uint32_t)zc[k] * MIC_SAMPLE_RATE) / (2u * n));

        f->ch[k].rms     = (rms > 65535.0f) ? 65535u : (uint16_t)rms;
        f->ch[k].peak    = pk[k];
        f->ch[k].freq_hz = freq;
        f->ch[k].events  = ev[k];
    }

    /* 7. 虫害判定: 以能量大的那个通道为准 */
    {
        uint8_t  loud = (f->ch[1].rms > f->ch[0].rms) ? 1 : 0;
        uint16_t r    = f->ch[loud].rms;
        uint16_t fr   = f->ch[loud].freq_hz;

        if (r >= MIC_RMS_PEST && fr >= MIC_FREQ_MIN && fr <= MIC_FREQ_MAX) {
            f->level = 2;                                  /* 虫害 */
        } else if (r >= MIC_RMS_SUSPECT) {
            f->level = 1;                                  /* 可疑 */
        } else {
            f->level = 0;                                  /* 正常 */
        }
    }
}

/* ---------------- 采集一帧 ---------------- */
uint8_t Mic_Capture(MicFrame *f)
{
    uint32_t t0;
    uint32_t timeout_ms;

    if (f == 0) return 1;

    /* 重启 DMA: 关->清标志->装长度->开 */
    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_HT4 | DMA1_FLAG_TE4 | DMA1_FLAG_GL4);
    i2s_clear_ovr();
    DMA_SetCurrDataCounter(DMA1_Channel4, MIC_BUF_SAMPLES);
    DMA_Cmd(DMA1_Channel4, ENABLE);

    /* 等搬完: 理论耗时 = n/16000 秒, 留 50ms 余量 */
    timeout_ms = ((uint32_t)MIC_FRAME_SAMPLES * 1000UL) / MIC_SAMPLE_RATE + 50UL;
    t0 = SysTick_Ms();
    while (DMA_GetFlagStatus(DMA1_FLAG_TC4) == RESET) {
        if ((uint32_t)(SysTick_Ms() - t0) > timeout_ms) {
            DMA_Cmd(DMA1_Channel4, DISABLE);
            return 1;
        }
    }

    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_TC4 | DMA1_FLAG_HT4 | DMA1_FLAG_TE4 | DMA1_FLAG_GL4);

    mic_analyze(f);
    return 0;
}
