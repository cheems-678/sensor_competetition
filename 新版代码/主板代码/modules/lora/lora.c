/**
 * WH-L101-L-P-H10 LoRa UART透传驱动 - 实现
 *
 * USART2: PA2(TX) PA3(RX), 115200 8N1
 * WH-L101默认透传模式, 发什么串口数据, 对端就收什么
 */

#include "lora.h"

/* ========== USART2 初始化 (WH-L101 透传, 仅TX/RX, 无控制引脚) ========== */
void LoRa_Init(void) {
    GPIO_InitTypeDef  g;
    USART_InitTypeDef u;
    USART_TypeDef*    U = USART2;

    /* 开时钟 */
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

    /* USART2: 115200 8N1 */
    u.USART_BaudRate            = LORA_BAUD;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(U, &u);
    USART_Cmd(U, ENABLE);

    /* 使能接收中断 (下行控制命令) */
    USART_ITConfig(U, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);
}

/* ========== 发送一个字节 (阻塞) ========== */
static void loraputc(uint8_t c) {
    /* 先等TXE(发送数据寄存器空), 再写入, 避免覆盖未发出的字节 */
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, c);
}

/* ========== 发送一个数据帧 ========== */
void LoRa_SendPacket(uint8_t type, uint8_t seq, uint8_t total,
                     uint8_t* data, uint8_t len) {
    uint8_t i;
    uint8_t cs = 0;  /* XOR校验 */

    /* 帧头 */
    loraputc(0xAA);
    loraputc(0x55);
    /* 类型+序号+总片+长度 */
    loraputc(type);  cs ^= type;
    loraputc(seq);   cs ^= seq;
    loraputc(total); cs ^= total;
    loraputc(len);   cs ^= len;
    /* 数据 */
    for (i = 0; i < len; i++) {
        loraputc(data[i]);
        cs ^= data[i];
    }
    /* 校验 */
    loraputc(cs);

    /* 等待发送完成 */
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);

    /* 帧间间隔, 让LoRa模块处理完 */
    SysTick_Delay_Us(500);
}

/* ========== 发送声速图 (256像素, 量化到uint8, 分2帧) ========== */
void LoRa_SendVMap(float* vmap, float vmin, float vmax) {
    uint8_t  buf[128];
    uint8_t  i;
    float    rng = vmax - vmin;
    if (rng < 0.001f) rng = 0.1f;

    /* 第1帧: 像素0~127 */
    for (i = 0; i < 128; i++) {
        float v = vmap[i];
        float n = (v - vmin) / rng;
        if (n < 0) n = 0; if (n > 1) n = 1;
        buf[i] = (uint8_t)(n * 255.0f);
    }
    LoRa_SendPacket(PKT_VMAP, 0, 2, buf, 128);
    SysTick_Delay_Us(1000);  /* 等LoRa空中传输完 */

    /* 第2帧: 像素128~255 */
    for (i = 0; i < 128; i++) {
        float v = vmap[i + 128];
        float n = (v - vmin) / rng;
        if (n < 0) n = 0; if (n > 1) n = 1;
        buf[i] = (uint8_t)(n * 255.0f);
    }
    LoRa_SendPacket(PKT_VMAP, 1, 2, buf, 128);
    SysTick_Delay_Us(1000);
}

/* ========== 发送异常图 (256字节, 分2帧) + 摘要 ========== */
void LoRa_SendAMap(uint8_t* amap, uint8_t mainAnom,
                   float conf, uint32_t elapsed_ms) {
    uint8_t buf[128];
    uint8_t i;

    /* 第1帧: 像素0~127 */
    for (i = 0; i < 128; i++) buf[i] = amap[i];
    LoRa_SendPacket(PKT_AMAP, 0, 2, buf, 128);
    SysTick_Delay_Us(1000);

    /* 第2帧: 像素128~255 */
    for (i = 0; i < 128; i++) buf[i] = amap[i + 128];
    LoRa_SendPacket(PKT_AMAP, 1, 2, buf, 128);
    SysTick_Delay_Us(1000);

    /* 摘要帧 */
    LoRa_SendSummary(mainAnom, conf, elapsed_ms);
}

/* ========== 发送摘要 ========== */
void LoRa_SendSummary(uint8_t mainAnom, float conf, uint32_t elapsed_ms) {
    uint8_t buf[8];
    uint16_t conf_int = (uint16_t)(conf * 1000.0f);  /* 0~1000 */
    buf[0] = mainAnom;
    buf[1] = (uint8_t)(conf_int >> 8);
    buf[2] = (uint8_t)(conf_int & 0xFF);
    buf[3] = (uint8_t)(elapsed_ms >> 24);
    buf[4] = (uint8_t)(elapsed_ms >> 16);
    buf[5] = (uint8_t)(elapsed_ms >> 8);
    buf[6] = (uint8_t)(elapsed_ms & 0xFF);
    LoRa_SendPacket(PKT_SUMMARY, 0, 1, buf, 7);
}

/* ========== 发送料位 ========== */
void LoRa_SendLevel(float level_mm, float siloH_mm) {
    uint8_t buf[4];
    uint16_t lvl = (uint16_t)level_mm;
    uint16_t silo = (uint16_t)siloH_mm;
    buf[0] = (uint8_t)(lvl >> 8);
    buf[1] = (uint8_t)(lvl & 0xFF);
    buf[2] = (uint8_t)(silo >> 8);
    buf[3] = (uint8_t)(silo & 0xFF);
    LoRa_SendPacket(PKT_LEVEL, 0, 1, buf, 4);
}

/* ========== 发送声发射波形 (512点×2字节=1024字节, 分8帧, 每帧128字节) ========== */
void LoRa_SendWave(uint16_t* samples, uint16_t len) {
    uint8_t  buf[128];
    uint8_t  i, j;
    uint8_t  total = 8;   /* 1024 / 128 */

    if (len > 512) len = 512;

    for (i = 0; i < total; i++) {
        for (j = 0; j < 64; j++) {        /* 每帧64个点 = 128字节 */
            uint16_t idx = (uint16_t)i * 64u + j;
            uint16_t v   = (idx < len) ? samples[idx] : 0;
            buf[j * 2]     = (uint8_t)(v >> 8);      /* 大端高字节 */
            buf[j * 2 + 1] = (uint8_t)(v & 0xFF);    /* 大端低字节 */
        }
        LoRa_SendPacket(PKT_WAVE, i, total, buf, 128);
        SysTick_Delay_Us(1000);
    }
}

/* ========== 发送原始字节 ========== */
void LoRa_SendRaw(uint8_t* data, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        loraputc(data[i]);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/* ========== 发送环境数据 (光照lux + 4路风扇转速 + 光照灯带状态) ========== */
void LoRa_SendEnv(uint16_t lux, const uint8_t* fan_pct, uint8_t led_on) {
    uint8_t buf[8];
    uint8_t i;
    buf[0] = (uint8_t)(lux >> 8);
    buf[1] = (uint8_t)(lux & 0xFF);
    for (i = 0; i < 4; i++) buf[2 + i] = fan_pct[i];
    buf[6] = led_on;
    buf[7] = 0;
    LoRa_SendPacket(PKT_ENV, 0, 1, buf, 8);
}

/* ========== 接收: USART2中断 -> 环形缓冲 -> 解析 ==========
 * 缓冲要能装下一整帧(从板声音包络帧 6+64+1=71 字节),
 * 所以开到 192 字节, 即使主机在处理别的任务时也能缓住一整帧。
 */
#define LORA_RX_SIZE 192

static volatile uint8_t rx_ring[LORA_RX_SIZE];
static volatile uint8_t rx_head = 0, rx_tail = 0;

/* 帧解析状态机 */
static uint8_t  fr_state = 0;      /* 0=找0xAA 1=找0x55 2=类型 3=序号 4=总片 5=长度 6=数据 7=校验 */
static uint8_t  fr_type = 0, fr_seq = 0, fr_total = 0, fr_len = 0, fr_idx = 0, fr_cs = 0;
static uint8_t  fr_buf[LORA_PKT_MAX];

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t b    = (uint8_t)USART_ReceiveData(USART2);
        uint8_t next = (uint8_t)((rx_head + 1) % LORA_RX_SIZE);
        if (next != rx_tail) {       /* 未满才存 */
            rx_ring[rx_head] = b;
            rx_head = next;
        }
        /* 溢出直接丢弃(下一次上报会补齐) */
    }
}

/**
 * @brief  从接收缓冲取一条控制命令 (0xC5前缀 + 命令字节)
 * @return 命令字节, 无命令返回0
 */
uint8_t LoRa_GetCmd(void)
{
    static uint8_t got_prefix = 0;

    while (rx_tail != rx_head) {
        uint8_t b = rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1) % LORA_RX_SIZE);
        if (got_prefix) {
            got_prefix = 0;
            return b;                /* 命令字节 */
        }
        if (b == LORA_CMD_PREFIX)
            got_prefix = 1;          /* 下个字节是命令 */
    }
    return 0;
}

/**
 * @brief  解析一帧从板发来的数据帧 (0xAA 0x55 ... XOR)
 * @param  type   输出: 包类型
 * @param  data   输出缓冲, 至少 LORA_PKT_MAX 字节
 * @param  len    输出: 数据长度
 * @return 1=解析到一帧完整且校验正确的数据, 0=暂无完整帧
 * @note   未通过校验的帧会被静默丢弃(现场干扰较多时这是必要的)
 */
uint8_t LoRa_PollFrame(uint8_t* type, uint8_t* data, uint8_t* len)
{
    while (rx_tail != rx_head) {
        uint8_t b = rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1) % LORA_RX_SIZE);

        switch (fr_state) {
            case 0:                                   /* 找帧头高字节 */
                if (b == 0xAA) fr_state = 1;
                break;
            case 1:                                   /* 找帧头低字节 */
                fr_state = (b == 0x55) ? 2 : ((b == 0xAA) ? 1 : 0);
                break;
            case 2:
                fr_type = b; fr_cs = b; fr_state = 3;
                break;
            case 3:
                fr_seq = b; fr_cs ^= b; fr_state = 4;
                break;
            case 4:
                fr_total = b; fr_cs ^= b; fr_state = 5;
                break;
            case 5:
                fr_len = b; fr_cs ^= b; fr_idx = 0;
                fr_state = (fr_len <= LORA_PKT_MAX) ? 6 : 0;   /* 长度非法丢弃 */
                break;
            case 6:
                if (fr_idx < fr_len) {
                    fr_buf[fr_idx++] = b;
                    fr_cs ^= b;
                }
                if (fr_idx >= fr_len) fr_state = 7;
                break;
            case 7:
                fr_state = 0;
                if (b == fr_cs) {                     /* 校验通过 */
                    uint8_t i;
                    for (i = 0; i < fr_len; i++) data[i] = fr_buf[i];
                    *type = fr_type;
                    *len  = fr_len;
                    return 1;
                }
                break;
            default:
                fr_state = 0;
                break;
        }
    }
    return 0;
}

/**
 * @brief 给从板下发一条命令 (0xC5 + 命令字节), 见 config.h LORA_CMD_NODE_*
 */
void LoRa_SendCtrl(uint8_t cmd)
{
    uint8_t buf[2];
    buf[0] = LORA_CMD_PREFIX;
    buf[1] = cmd;
    LoRa_SendRaw(buf, 2);
}
