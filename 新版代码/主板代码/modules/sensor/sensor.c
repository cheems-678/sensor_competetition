/**
 * 声学传感器驱动 - 实现 (Standard Peripheral Library)
 */

#include "sensor.h"
#include "../../dwt_us.h"   /* DWT 周期计数器访问宏(不依赖 CMSIS 是否导出 DWT) */

/* 确保 config.h 路径也被sensor.c包含 (sensor.h 已经通过 ../../config.h 包含进来了)
   但为了保险, 再确保所有 .c 都可以直接取到 config 里的类型定义 */
#if 0
#include "../../config.h"
#endif

/* ========== 40kHz正弦查找表 (32点/周期, 12位DAC, 中心2048, ±2047) ========== */
static const uint16_t sine32[32] = {
    2048,2447,2831,3185,3495,3761,3974,4130,4224,4255,4224,4130,3974,3761,3495,3185,
    2831,2447,2048,1649,1265, 911, 601, 335, 122,   0,   0,   0,   0,   0, 162, 375
};

/* ========== GPIO 初始化 ========== */
static void GPIO_Init_All(void) {
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC, ENABLE);

    /* PA0 模拟输入 ADC1_IN0 */
    g.GPIO_Pin  = GPIO_Pin_0;
    g.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &g);

    /* PA4 模拟输出 DAC_OUT1 */
    g.GPIO_Pin  = GPIO_Pin_4;
    g.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &g);

    /* PA6,PA7,PA8 推挽输出: CD74HC4067 多路开关 S0,S1,S2
       (PA11/PA12 由 servo.c 配置为舵机软PWM输出, 此处不管) */
    g.GPIO_Pin  = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed= GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    /* PB0 推挽输出: MUX S3 (恒低, 8通道模式), PB2 状态LED
       (PB1=风扇2的TIM3_CH4, 由 fan.c 配置, 此处不能碰!) */
    g.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_2;
    GPIO_Init(GPIOB, &g);

    /* PC13/14/15 按键输入(带上拉, 按下=GND) */
    g.GPIO_Pin  = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    g.GPIO_Mode = GPIO_Mode_IPU;   /* 内部上拉 */
    GPIO_Init(GPIOC, &g);

    /* 初始状态 */
    GPIO_SetBits(GPIOB,  GPIO_Pin_2);            /* LED灭 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_0);           /* MUX S3=0 */
}

/* ========== ADC1 初始化 (PA0, 软件触发, 12位) ========== */
static void ADC_Init_All(void) {
    ADC_InitTypeDef a;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);   /* 72/6=12MHz <14MHz */

    a.ADC_Mode               = ADC_Mode_Independent;
    a.ADC_ScanConvMode       = DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;
    a.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign          = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &a);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_1Cycles5);
    ADC_Cmd(ADC1, ENABLE);

    /* 校准 */
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
}

/* ========== DAC 初始化 (PA4 Channel1, 软件触发) ========== */
static void DAC_Init_All(void) {
    DAC_InitTypeDef d;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

    d.DAC_Trigger        = DAC_Trigger_None;
    d.DAC_WaveGeneration = DAC_WaveGeneration_None;
    d.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    d.DAC_OutputBuffer   = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &d);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_SetChannel1Data(DAC_Align_12b_R, 2048);   /* 中间电平 */
}

/* ========== SysTick 延时 (共用 stm32f10x_it.c 中定义的 g_tick_ms + SysTick_Handler) ========== */
extern volatile uint32_t g_tick_ms;   /* 来自 stm32f10x_it.c, 不要再定义! */

/* DWT周期计数器初始化 (定义见下方) */
static void sensor_dwt_init(void);

void SysTick_Init(void) {
    /* 1ms tick: 72MHz / 1000 = 72000 */
    SysTick_Config(SYS_CLK / 1000);
    sensor_dwt_init();          /* 单总线温湿度需要微秒级计数(DWT), 一并初始化 */
}
uint32_t SysTick_Ms(void) { return g_tick_ms; }
void SysTick_Delay_Ms(uint32_t ms) {
    uint32_t s = g_tick_ms;
    while ((g_tick_ms - s) < ms);
}
void SysTick_Delay_Us(uint32_t us) {
    /* 简单延时 @72MHz: 约12循环 = 1μs */
    for (volatile uint32_t i = 0; i < (uint32_t)us * 12; i++) __NOP();
}
/* SysTick_Handler 不要在本文件定义! 已经在 stm32f10x_it.c 中了 */

/* ========== DWT周期计数器 (不含任何定时器资源, 给单总线微秒级测量用) ========== */
static uint8_t sensor_dwt_ok = 0;

static void sensor_dwt_init(void) {
    uint32_t a, b;
    volatile uint32_t i;

    DWT_US_ENABLE();                 /* 使能DWT周期计数器(绝对地址, 不依赖CMSIS) */

    a = DWT_US_CYCLES();
    for (i = 0; i < 300; i++) { }
    b = DWT_US_CYCLES();
    sensor_dwt_ok = (b != a) ? 1 : 0;
}
uint8_t  SysTick_DwtOk(void)     { return sensor_dwt_ok; }
uint32_t SysTick_RawCycles(void) { return sensor_dwt_ok ? DWT_US_CYCLES() : 0; }
uint32_t SysTick_UsNow(void) {
    if (!sensor_dwt_ok) return 0;
    return DWT_US_CYCLES() / (SYS_CLK / 1000000UL);
}

/* ========== CD74HC4067 通道选择 (S0=PA6, S1=PA7, S2=PA8; S3=PB0恒低) ========== */
static void muxSel(uint8_t ch) {
    ch &= 7;   /* S3接地, 仅通道0~7 */
    GPIO_WriteBit(MUX_S0_PORT, MUX_S0_PIN, (ch & 1) ? Bit_SET : Bit_RESET);  /* S0 */
    GPIO_WriteBit(MUX_S1_PORT, MUX_S1_PIN, (ch & 2) ? Bit_SET : Bit_RESET);  /* S1 */
    GPIO_WriteBit(MUX_S2_PORT, MUX_S2_PIN, (ch & 4) ? Bit_SET : Bit_RESET);  /* S2 */
}

/* ========== 收发模式切换 (实际硬件无继电器, 同一MUX通道先发后收) ========== */
static void setModeTX(uint8_t tx) {
    (void)tx;   /* 无继电器, 保留接口兼容; 发射后立即采样接收 */
}

/* ========== DAC输出正弦脉冲 (40kHz × n周期) ========== */
static void dacBurst(uint8_t cycles, uint16_t amp) {
    /* 1周期 = 32点, 40kHz → 每点 ~0.78125μs = 781ns
       72MHz下约56个时钟, 用循环粗略延时 */
    uint16_t total = 32 * cycles;
    for (uint16_t i = 0; i < total; i++) {
        uint16_t v = sine32[i & 31];
        /* 缩放到指定幅度 (以2048为中心) */
        int32_t dv = (int32_t)v - 2048;
        dv = dv * (int32_t)amp / 4095;
        v  = (uint16_t)(2048 + dv);
        DAC_SetChannel1Data(DAC_Align_12b_R, v);
        /* 粗略 ~780ns 延时 */
        for (volatile int j = 0; j < 9; j++) __NOP();
    }
    DAC_SetChannel1Data(DAC_Align_12b_R, 2048);
}

/* ========== ADC 软件采样 N 点 @~1MHz ========== */
static void adcReadN(uint16_t* buf, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
        buf[i] = ADC_GetConversionValue(ADC1);
        /* 72MHz下: 采样+等待已经用掉几百ns, 补几循环到1μs */
        for (volatile int j = 0; j < 6; j++) __NOP();
    }
}

/* ========== 从采样数据中提取TOF(ms)和幅度(dB) ========== */
static void extract(uint16_t* d, uint16_t n, float* tof, float* amp) {
    uint16_t nNoise, tofSm, mv, nv, end, pk, i;
    uint32_t s;
    float var, sd, thr, dv;

    /* 1. DC偏置 = 前100个样本平均 */
    nNoise = (n > 100) ? 100 : n/4;
    s = 0;
    for (i = 0; i < nNoise; i++) s += d[i];
    s = (s + nNoise/2) / nNoise;

    /* 2. 噪声标准差 */
    var = 0;
    for (i = 0; i < nNoise; i++) {
        dv = (float)d[i] - (float)s;
        var += dv * dv;
    }
    var /= (float)nNoise;
    sd = sqrtf(var);
    thr = (float)s + 5.0f * sd;  /* 5σ门限 */

    /* 3. 第一个超过门限的样本索引 */
    tofSm = 0;
    for (i = nNoise; i < n; i++) {
        if ((float)d[i] > thr) { tofSm = i; break; }
    }

    /* 4. TOF(ms): 1μs/点 → /1000 */
    *tof = (float)tofSm / 1000.0f;

    /* 5. 峰峰值 → dB(相对满量程4095) */
    mv = 0; nv = 4095;
    end = (tofSm + 200 < n) ? tofSm + 200 : n;
    for (i = tofSm; i < end; i++) {
        if (d[i] > mv) mv = d[i];
        if (d[i] < nv) nv = d[i];
    }
    pk = mv - nv;
    if (pk > 0) *amp = 20.0f * log10f((float)pk / 4095.0f);
    else         *amp = -60.0f;

    if (tofSm == 0 || tofSm >= n - 10) { *tof = 0; *amp = -60; }
}

/* ========== 对外API ========== */
void Sensor_Init(void) {
    GPIO_Init_All();
    ADC_Init_All();
    DAC_Init_All();
    SysTick_Init();
    muxSel(0);
    setModeTX(0);   /* 默认接收模式 */
}

void Sensor_SelTx(uint8_t idx) { muxSel(idx); }
void Sensor_SelRx(uint8_t idx) { muxSel(idx); }
void Sensor_Transmit(void)     { dacBurst(TX_CYCLES, TX_AMP); }

void Sensor_Receive(uint16_t* buf) {
    adcReadN(buf, ADC_BUF_SIZE);
}

void Sensor_MeasureRay(uint8_t tx, uint8_t rx, RayMeas* out) {
    static uint16_t adcBuf[ADC_BUF_SIZE];   /* 全局静态缓冲区, 节省RAM */
    float tof, amp;

    out->valid = 0;
    out->tof_ms = 0;
    out->amp_db = -60;

    /* 1. 发射 */
    Sensor_SelTx(tx);
    setModeTX(1);
    Sensor_Transmit();

    /* 2. 快速切接收 */
    setModeTX(0);
    Sensor_SelRx(rx);

    /* 3. 采样 */
    adcReadN(adcBuf, ADC_BUF_SIZE);

    /* 4. 提取 */
    extract(adcBuf, ADC_BUF_SIZE, &tof, &amp);
    out->tof_ms = tof;
    out->amp_db = amp;
    out->valid  = (tof > 0.005f && amp > -50.0f) ? 1 : 0;
}

void Sensor_ScanAll(RayMeas* rs) {
    int i, j, r = 0;
    for (i = 0; i < NUM_TX; i++) {
        for (j = i+1; j < NUM_TX; j++) {
            Sensor_MeasureRay((uint8_t)i, (uint8_t)j, &rs[r]);
            r++;
        }
    }
}

void Sensor_ListenAE(uint16_t* buf, uint16_t count, uint32_t duration_ms) {
    uint32_t s;
    uint16_t w = 0;
    setModeTX(0);
    Sensor_SelRx(0);
    s = SysTick_Ms();
    while ((SysTick_Ms() - s) < duration_ms) {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
        buf[w % count] = ADC_GetConversionValue(ADC1);
        w++;
        /* ~20μs 延时 → 50kHz采样率 (适合虫害声1-20kHz) */
        SysTick_Delay_Us(18);
    }
}

float Sensor_Level_mm(void) {
    uint16_t buf[ADC_BUF_SIZE];
    float tof, amp, dist_mm;

    /* 顶部换能器索引 = 2 (T3: (0,100)) */
    Sensor_SelTx(2);
    setModeTX(1);
    Sensor_Transmit();
    setModeTX(0);
    Sensor_SelRx(2);

    adcReadN(buf, ADC_BUF_SIZE);
    extract(buf, ADC_BUF_SIZE, &tof, &amp);

    /* 距离 = V×t / 2 (往返) */
    dist_mm = V_GRAIN_MS * tof * 1000.0f * 0.5f;
    return dist_mm;
}

/* LED 控制对外宏 */
void LED_Set(uint8_t on) {
    GPIO_WriteBit(GPIOB, GPIO_Pin_2, on ? Bit_SET : Bit_RESET);
}

/* 按键读取 (返回1=按下, 带50ms去抖) */
static uint32_t keyT[3] = {0};
uint8_t Key_Read(uint8_t idx) {
    GPIO_TypeDef* port;
    uint16_t      pin;
    if (idx == 0)      { port = GPIOC; pin = GPIO_Pin_13; }  /* START */
    else if (idx == 1) { port = GPIOC; pin = GPIO_Pin_14; }  /* MODE  */
    else               { port = GPIOC; pin = GPIO_Pin_15; }  /* SAVE  */

    /* 按下=低电平 */
    if (GPIO_ReadInputDataBit(port, pin) == Bit_RESET) {
        if (SysTick_Ms() - keyT[idx] > 50) {
            keyT[idx] = SysTick_Ms();
            return 1;
        }
    }
    return 0;
}
