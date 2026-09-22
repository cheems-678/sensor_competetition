/**
 * WH-L101-L-P-H10 LoRa UART透传驱动 (从板/传感器节点) - 实现
 *
 * USART2: PA2(TX) PA3(RX), 115200 8N1
 * 控制脚: PA4=RELOAD(常态高), PA1=WAKE(常态高), PA0=HOST_WAKE(输入,不用)
 * WH-L101 默认就是透传模式, 发什么对端就收什么。
 */

#include "lora.h"

/* ================= USART2 + 控制引脚初始化 ================= */
void LoRa_Init(void)
{
    GPIO_InitTypeDef  g;
    USART_InitTypeDef u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA2 TX - 复用推挽 */
    g.GPIO_Pin   = GPIO_Pin_2;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    /* PA3 RX - 浮空输入 */
    g.GPIO_Pin  = GPIO_Pin_3;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &g);

    /* PA4 = RELOAD: 输出, 常态高(外部有4.7k上拉, 拉低才起作用) */
    g.GPIO_Pin   = LORA_RELOAD_PIN | LORA_WAKE_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    /* PA0 = HOST_WAKE: 输入(模块输出给MCU的信号, 当前不用, 上拉避免浮空) */
    g.GPIO_Pin  = LORA_HWAK_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &g);

    /* 初始: RELOAD=高(不触发), WAKE=高(正常工作) */
    GPIO_SetBits(GPIOA, LORA_RELOAD_PIN | LORA_WAKE_PIN);

    /* USART2: 115200 8N1 */
    u.USART_BaudRate            = LORA_BAUD;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &u);
    USART_Cmd(USART2, ENABLE);

    /* 接收中断: 收主板下行命令 */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);
}

/* ================= 控制引脚 ================= */
void LoRa_Reload(void)
{
    GPIO_ResetBits(LORA_RELOAD_PORT, LORA_RELOAD_PIN);
    SysTick_Delay_Ms(1000);
    GPIO_SetBits(LORA_RELOAD_PORT, LORA_RELOAD_PIN);
    SysTick_Delay_Ms(100);
}

void LoRa_WakeUp(void) { GPIO_SetBits(LORA_WAKE_PORT, LORA_WAKE_PIN); }
void LoRa_Sleep(void)  { GPIO_ResetBits(LORA_WAKE_PORT, LORA_WAKE_PIN); }

/* ================= 发送 ================= */
static void loraputc(uint8_t c)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, c);
}

void LoRa_SendPacket(uint8_t type, uint8_t seq, uint8_t total,
                     uint8_t* data, uint8_t len)
{
    uint8_t i;
    uint8_t cs = 0;

    loraputc(LORA_HDR0);
    loraputc(LORA_HDR1);
    loraputc(type);  cs ^= type;
    loraputc(seq);   cs ^= seq;
    loraputc(total); cs ^= total;
    loraputc(len);   cs ^= len;
    for (i = 0; i < len; i++) {
        loraputc(data[i]);
        cs ^= data[i];
    }
    loraputc(cs);

    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    SysTick_Delay_Us(500);      /* 帧间留一点间隔给LoRa模块 */
}

void LoRa_SendRaw(uint8_t* data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        loraputc(data[i]);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/* ================= 从板上报 ================= */

/**
 * 仓内温湿度 + 气压 (PKT_NODE_ENV, 10字节)
 *   [0..1] t_in   int16  0.1℃     [2..3] rh_in  uint16 0.1%RH
 *   [4..5] press  uint16 0.1hPa   [6..7] t_bme  int16  0.1℃
 *   [8]    flags  bit0=温湿度电缆OK bit1=BME/BMP在线 bit2=气压有效
 *   [9]    th_type 0=DHT11 1=AM2302 0xFF=未知
 * 无效值统一填 0x8000(int16最小值) 或 0xFFFF, 主板按flags判断。
 */
void LoRa_SendNodeEnv(const TH_Data* th, const BME280_Data* bme, uint8_t bme_ok)
{
    uint8_t  buf[10];
    int16_t  t_in, t_bme;
    uint16_t rh_in, press;
    uint8_t  flags = 0;

    if (th && th->valid) {
        t_in  = (int16_t)(th->temp_c * 10.0f);
        rh_in = (uint16_t)(th->hum_pct * 10.0f);
        flags |= 0x01;
    } else {
        t_in  = (int16_t)0x8000;
        rh_in = 0xFFFF;
    }

    if (bme_ok && bme) {
        press = (uint16_t)(bme->press_hpa * 10.0f);
        t_bme = (int16_t)(bme->temp_c * 10.0f);
        flags |= 0x02;
        if (press > 1000 && press < 13000) flags |= 0x04;   /* 100~1300hPa 视为有效 */
    } else {
        press = 0xFFFF;
        t_bme = (int16_t)0x8000;
    }

    buf[0] = (uint8_t)((uint16_t)t_in >> 8);
    buf[1] = (uint8_t)((uint16_t)t_in & 0xFF);
    buf[2] = (uint8_t)(rh_in >> 8);
    buf[3] = (uint8_t)(rh_in & 0xFF);
    buf[4] = (uint8_t)(press >> 8);
    buf[5] = (uint8_t)(press & 0xFF);
    buf[6] = (uint8_t)((uint16_t)t_bme >> 8);
    buf[7] = (uint8_t)((uint16_t)t_bme & 0xFF);
    buf[8] = flags;
    buf[9] = (th && th->valid) ? th->type : 0xFF;

    LoRa_SendPacket(PKT_NODE_ENV, 0, 1, buf, 10);
}

/** 虫害声特征 (PKT_NODE_SOUND, 13字节) */
void LoRa_SendNodeSound(const MicFrame* f)
{
    uint8_t buf[13];

    buf[0]  = (uint8_t)(f->ch[0].rms >> 8);
    buf[1]  = (uint8_t)(f->ch[0].rms & 0xFF);
    buf[2]  = (uint8_t)(f->ch[0].peak >> 8);
    buf[3]  = (uint8_t)(f->ch[0].peak & 0xFF);
    buf[4]  = (uint8_t)(f->ch[0].freq_hz >> 8);
    buf[5]  = (uint8_t)(f->ch[0].freq_hz & 0xFF);
    buf[6]  = (uint8_t)(f->ch[1].rms >> 8);
    buf[7]  = (uint8_t)(f->ch[1].rms & 0xFF);
    buf[8]  = (uint8_t)(f->ch[1].peak >> 8);
    buf[9]  = (uint8_t)(f->ch[1].peak & 0xFF);
    buf[10] = (uint8_t)(f->ch[1].freq_hz >> 8);
    buf[11] = (uint8_t)(f->ch[1].freq_hz & 0xFF);
    buf[12] = f->level;

    LoRa_SendPacket(PKT_NODE_SOUND, 0, 1, buf, 13);
}

/** 声音包络 (PKT_NODE_AUDIO, 64字节) */
void LoRa_SendNodeAudio(const MicFrame* f)
{
    uint8_t buf[MIC_ENV_POINTS];
    uint8_t i;

    for (i = 0; i < MIC_ENV_POINTS; i++) buf[i] = f->env[i];
    LoRa_SendPacket(PKT_NODE_AUDIO, 0, 1, buf, MIC_ENV_POINTS);
}

/* ================= 接收 (USART2中断 -> 环形缓冲) ================= */
#define LORA_RX_SIZE 16
static volatile uint8_t rx_ring[LORA_RX_SIZE];
static volatile uint8_t rx_head = 0, rx_tail = 0;

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t b    = (uint8_t)USART_ReceiveData(USART2);
        uint8_t next = (uint8_t)((rx_head + 1) % LORA_RX_SIZE);
        if (next != rx_tail) {
            rx_ring[rx_head] = b;
            rx_head = next;
        }
    }
}

uint8_t LoRa_GetCmd(void)
{
    static uint8_t got_prefix = 0;

    while (rx_tail != rx_head) {
        uint8_t b = rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1) % LORA_RX_SIZE);

        if (got_prefix) {
            got_prefix = 0;
            return b;                 /* 命令字节 */
        }
        if (b == LORA_CMD_PREFIX) got_prefix = 1;
    }
    return 0;
}
