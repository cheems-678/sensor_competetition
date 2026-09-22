/**
 * @file    esp8266.c
 * @brief   ESP8266 WiFi 驱动实现 (UART AT 指令)
 * @note    本工程适配: USART3 (PB10 TX, PB11 RX), 115200
 *          AT 指令格式: "AT+<CMD>\r\n"
 *          响应格式: "<data>\r\nOK\r\n" 或 "ERROR\r\n"
 *          +IPD 数据: "+IPD,<len>:<data>" (TCP 接收)
 *
 *          典型流程:
 *          1. ESP_Init()             初始化硬件
 *          2. ESP_AT()               检测模块在线
 *          3. ESP_SetMode(STATION)   设置 WiFi 模式
 *          4. ESP_JoinAP(ssid, pwd)  连接路由器
 *          5. ESP_Connect(TCP, ...)  建立 TCP 连接
 *          6. ESP_Send(data, len)    发送数据
 *          7. ESP_RecvData(buf)      接收数据
 */
#include "esp8266.h"
#include <string.h>
#include <stdio.h>

/*========================== 内部常量 =========================*/
#define RX_BUF_SIZE         512         /* 接收环形缓冲区大小 */
#define TX_BUF_SIZE         512         /* AT 命令缓冲区大小 */
#define AT_SHORT_TIMEOUT    3000        /* 短超时 (普通 AT 指令) */
#define AT_LONG_TIMEOUT     10000       /* 长超时 (连 WiFi / TCP) */

/*========================== 内部变量 =========================*/
static uint8_t           rx_buf[RX_BUF_SIZE];       /* 中断接收环形缓冲区 */
static volatile uint16_t rx_head;                    /* 生产者 (中断) 索引 */
static volatile uint16_t rx_tail;                    /* 消费者 (主循环) 索引 */
static char              tx_buf[TX_BUF_SIZE];        /* AT 命令构建缓冲区 */
static void              (*recv_cb)(const uint8_t *, uint16_t);  /* 接收回调 */

/*========================== 内部函数声明 =====================*/
static void     usart_init(void);
static void     gpio_init(void);
static void     uart_send_str(const char *s);
static void     uart_send_buf(const uint8_t *d, uint16_t len);
static uint16_t uart_avail(void);
static uint8_t  uart_read(void);
static void     uart_flush(void);
static void     delay_ms(uint32_t ms);

/* 发送 AT 指令并等待响应 (核心函数) */
static ESPStatus at_cmd(const char *cmd, char *resp, uint16_t max_len,
                        uint32_t timeout);

/* 检查响应字符串中是否包含指定关键字 */
static uint8_t  resp_contains(const char *resp, const char *keyword);

/*========================== 模块初始化 =======================*/

/**
 * @brief  初始化 ESP8266 控制引脚和 USART3
 * @note   USART3: PB10(TX) PB11(RX), 默认 115200 8N1
 *         PA4(RST) 和 PA5(CH_PD/EN) 拉高唤醒模块
 *         上电后需等待至少 1s 让模块启动
 * @return ESP_OK
 */
ESPStatus ESP_Init(void)
{
    rx_head = 0;
    rx_tail = 0;
    recv_cb = 0;
    gpio_init();
    usart_init();

    /* 拉高 EN 和 RST 引脚, 使模块退出复位状态 */
    GPIO_WriteBit(ESP_CH_PD_PORT, ESP_CH_PD_PIN, Bit_SET);
    GPIO_WriteBit(ESP_RST_PORT, ESP_RST_PIN, Bit_SET);
    delay_ms(1000);                      /* 等待模块上电启动完成 */
    return ESP_OK;
}

/*========================== 基础 AT 指令 =====================*/

/**
 * @brief  测试 AT 通信 (AT\r\n)
 * @return ESP_OK 模块正常应答 "OK"
 *         ESP_ERR_NO_RESPONSE 模块无应答
 */
ESPStatus ESP_AT(void)
{
    char resp[32];
    ESPStatus st = at_cmd("AT\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == ESP_OK && resp_contains(resp, "OK")) ? ESP_OK : ESP_ERR_NO_RESPONSE;
}

/**
 * @brief  软复位模块 (AT+RST)
 * @return ESPStatus
 */
ESPStatus ESP_Reset(void)
{
    char resp[64];
    ESPStatus st = at_cmd("AT+RST\r\n", resp, sizeof(resp), AT_LONG_TIMEOUT);
    delay_ms(1000);                      /* 等重启完成 */
    return st;
}

/**
 * @brief  读取固件版本 (AT+GMR)
 * @param  buf     存放版本字符串
 * @param  max_len 缓冲区大小
 * @return ESPStatus
 */
ESPStatus ESP_GetVersion(char *buf, uint8_t max_len)
{
    return at_cmd("AT+GMR\r\n", buf, max_len, AT_SHORT_TIMEOUT);
}

/*========================== WiFi 配置 ========================*/

/**
 * @brief  设置 WiFi 工作模式
 * @param  mode  ESP_MODE_STATION(1) / AP(2) / BOTH(3)
 * @return ESP_OK 成功
 */
ESPStatus ESP_SetMode(uint8_t mode)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d\r\n", mode);
    char resp[16];
    return at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
}

/**
 * @brief  连接 WiFi 路由器 (AT+CWJAP)
 * @param  ssid   WiFi 名称
 * @param  pwd    WiFi 密码 (可为空)
 * @return ESP_OK          连接成功
 *         ESP_ERR_WIFI    连接失败 (超时/密码错)
 */
ESPStatus ESP_JoinAP(const char *ssid, const char *pwd)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    char resp[64];
    ESPStatus st = at_cmd(cmd, resp, sizeof(resp), AT_LONG_TIMEOUT);
    if (st != ESP_OK) return ESP_ERR_WIFI;
    if (!resp_contains(resp, "OK") || resp_contains(resp, "FAIL"))
        return ESP_ERR_WIFI;
    return ESP_OK;
}

/**
 * @brief  断开当前 WiFi 连接 (AT+CWQAP)
 * @return ESP_OK
 */
ESPStatus ESP_QuitAP(void)
{
    char resp[16];
    return at_cmd("AT+CWQAP\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
}

/**
 * @brief  获取本机 IP 地址 (AT+CIFSR)
 * @param  ip      存放 IP 字符串 (如 "192.168.1.100")
 * @param  max_len 缓冲区大小
 * @return ESP_OK 成功
 */
ESPStatus ESP_GetIP(char *ip, uint8_t max_len)
{
    char resp[64];
    ESPStatus st = at_cmd("AT+CIFSR\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != ESP_OK) return st;
    if (!resp_contains(resp, "OK")) return ESP_ERR_NO_RESPONSE;

    /* 响应示例: +CIFSR:STAIP,"192.168.1.100" */
    char *p = strstr(resp, "STAIP,\"");
    if (!p) return ESP_ERR_NO_RESPONSE;
    p += 7;                              /* 跳过 STAIP," */
    uint8_t i = 0;
    while (*p && *p != '\"' && i < max_len - 1)
        ip[i++] = *p++;
    ip[i] = 0;                           /* 字符串结尾 */
    return ESP_OK;
}

/*========================== TCP/UDP 连接 =====================*/

/**
 * @brief  建立 TCP/UDP/SSL 连接 (AT+CIPSTART)
 * @param  type   ESP_TCP / ESP_UDP / ESP_SSL
 * @param  host   目标 IP 或域名
 * @param  port   目标端口
 * @return ESP_OK 连接成功
 *         ESP_ERR_CONNECT 连接失败
 */
ESPStatus ESP_Connect(uint8_t type, const char *host, uint16_t port)
{
    char cmd[128];
    const char *proto = (type == ESP_TCP) ? "TCP"
                       : (type == ESP_SSL) ? "SSL" : "UDP";
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%u\r\n",
             proto, host, port);
    char resp[64];
    ESPStatus st = at_cmd(cmd, resp, sizeof(resp), AT_LONG_TIMEOUT);
    if (st != ESP_OK) return ESP_ERR_CONNECT;
    /* 成功响应包含 CONNECT 或 ALREADY CONNECTED */
    if (!resp_contains(resp, "CONNECT") && !resp_contains(resp, "ALREADY"))
        return ESP_ERR_CONNECT;
    return ESP_OK;
}

/**
 * @brief  发送数据 (AT+CIPSEND)
 * @param  data  数据缓冲区
 * @param  len   字节数
 * @return ESP_OK       发送成功
 *         ESP_ERR_SEND 发送失败
 * @note   先发 "AT+CIPSEND=<len>" 等 ">" 提示符,
 *         再发实际数据, 最后等 "SEND OK"
 */
ESPStatus ESP_Send(const uint8_t *data, uint16_t len)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", len);
    char resp[32];
    ESPStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != ESP_OK) return ESP_ERR_SEND;
    if (!resp_contains(resp, ">"))       /* 模块返回 ">" 表示准备好接收数据 */
        return ESP_ERR_SEND;

    uart_send_buf(data, len);            /* 发送实际负载 */

    delay_ms(200);
    uart_flush();
    /* 等待 "SEND OK" 或 "ERROR" */
    uint32_t tout = AT_SHORT_TIMEOUT / 10;
    uint16_t rlen = 0;
    while (tout--) {
        while (uart_avail() && rlen < 32) {
            resp[rlen++] = (char)uart_read();
        }
        if (resp_contains(resp, "SEND OK"))
            return ESP_OK;
        if (resp_contains(resp, "ERROR"))
            return ESP_ERR_SEND;
        delay_ms(10);
    }
    return (resp_contains(resp, "SEND OK")) ? ESP_OK : ESP_ERR_SEND;
}

/**
 * @brief  关闭当前 TCP/UDP 连接 (AT+CIPCLOSE)
 * @return ESP_OK
 */
ESPStatus ESP_Close(void)
{
    char resp[16];
    return at_cmd("AT+CIPCLOSE\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
}

/*========================== HTTP GET =========================*/

/**
 * @brief  发起 HTTP GET 请求
 * @param  url     完整 URL (如 "http://example.com/data")
 * @param  resp    存放响应内容
 * @param  max_len 缓冲区大小
 * @return ESPStatus
 * @note   需要 AT 固件支持 +HTTPCLIENT 指令
 *         部分旧固件需用 AT+CIPSTART + AT+CIPSEND 手动实现
 */
ESPStatus ESP_HTTP_GET(const char *url, char *resp, uint16_t max_len)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+HTTPCLIENT=0,1,%s\r\n", url);
    return at_cmd(cmd, resp, max_len, AT_LONG_TIMEOUT);
}

/*========================== 数据接收 =========================*/

/**
 * @brief  读取接收缓冲区中的数据, 自动解析 +IPD 格式
 * @param  buf     存放数据
 * @param  max_len 最大读取字节数
 * @return >0  实际数据长度
 *         0   缓冲区空
 * @note   ESP8266 在 TCP 接收时以 +IPD,<len>:<data> 格式上报,
 *         本函数自动剥离 +IPD 头返回纯数据
 */
int16_t ESP_RecvData(uint8_t *buf, uint16_t max_len)
{
    if (!uart_avail())
        return 0;

    uint16_t idx = 0;
    while (uart_avail() && idx < max_len)
        buf[idx++] = uart_read();

    /* 若数据以 "+IPD," 开头, 需剥离长度和冒号 */
    if (idx >= 5 && memcmp(buf, "+IPD,", 5) == 0) {
        uint16_t dlen = 0;
        uint8_t i = 5;
        while (i < idx && buf[i] >= '0' && buf[i] <= '9') {
            dlen = dlen * 10 + (buf[i] - '0');
            i++;
        }
        if (i < idx && buf[i] == ':') {
            i++;
            uint16_t remain = idx - i;
            if (remain > dlen) remain = dlen;
            memmove(buf, buf + i, remain);
            return (int16_t)remain;
        }
    }
    return (int16_t)idx;
}

/**
 * @brief  注册接收回调函数
 * @param  cb  回调指针 (在 USART3 中断中调用)
 */
void ESP_SetRecvCallback(void (*cb)(const uint8_t *, uint16_t))
{
    recv_cb = cb;
}

/*========================== 底层 USART3 =====================*/

/**
 * @brief  初始化 USART3 (PB10 TX, PB11 RX)
 * @note   波特率由 ESP_USART_BAUD 定义, 默认 115200
 *         使能 RXNE 中断接收
 *         USART3 时钟来自 APB1
 */
static void usart_init(void)
{
    USART_InitTypeDef usart;
    GPIO_InitTypeDef  gpio;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = ESP_TX_PIN;        /* PB10: USART3_TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP_TX_PORT, &gpio);

    gpio.GPIO_Pin  = ESP_RX_PIN;         /* PB11: USART3_RX */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ESP_RX_PORT, &gpio);

    NVIC_InitTypeDef nvic;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic.NVIC_IRQChannel = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_StructInit(&usart);
    usart.USART_BaudRate = ESP_USART_BAUD;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(ESP_USART, &usart);
    USART_ITConfig(ESP_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(ESP_USART, ENABLE);
}

/**
 * @brief  初始化 RST 和 CH_PD GPIO (推挽输出)
 */
static void gpio_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = ESP_RST_PIN | ESP_CH_PD_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
}

/*========================== 串口收发 =========================*/

/**
 * @brief  轮询发送字符串
 * @param  s  以 '\0' 结尾的字符串
 */
static void uart_send_str(const char *s)
{
    while (*s) {
        USART_SendData(ESP_USART, (uint8_t)*s);
        while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TC) == RESET);
        s++;
    }
}

/**
 * @brief  轮询发送数据缓冲区
 * @param  d   数据指针
 * @param  len 字节数
 */
static void uart_send_buf(const uint8_t *d, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USART_SendData(ESP_USART, d[i]);
        while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TC) == RESET);
    }
}

/**
 * @brief  检查环形缓冲区中是否有数据
 * @return 1 有数据, 0 空
 */
static uint16_t uart_avail(void)
{
    return (rx_head != rx_tail) ? 1 : 0;
}

/**
 * @brief  从环形缓冲区读取一个字节 (阻塞)
 * @return 读取的字节
 */
static uint8_t uart_read(void)
{
    while (rx_tail == rx_head);          /* 等待数据 */
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_BUF_SIZE);
    return c;
}

/**
 * @brief  清空环形缓冲区
 */
static void uart_flush(void)
{
    rx_head = 0;
    rx_tail = 0;
}

/*========================== AT 指令收发 =====================*/

/**
 * @brief  发送 AT 指令并等待响应
 * @param  cmd      完整的 AT 指令 (含 \r\n)
 * @param  resp     存放响应的缓冲区
 * @param  max_len  缓冲区最大长度
 * @param  timeout  超时时间 (ms)
 * @return ESP_OK         收到响应
 *         ESP_ERR_TIMEOUT 超时无响应
 * @note   响应检测条件:
 *         - 出现 "OK" → 结束
 *         - 出现 "ERROR" → 结束
 *         - 超时 → 返回已有数据或超时
 */
static ESPStatus at_cmd(const char *cmd, char *resp, uint16_t max_len,
                        uint32_t timeout)
{
    uart_flush();                        /* 清空之前残留数据 */
    uart_send_str(cmd);                  /* 发送 AT 指令 */

    uint32_t tout = timeout / 10;        /* 每 10ms 轮询一次 */
    uint16_t idx = 0;

    while (tout--) {
        while (uart_avail() && idx < max_len - 1) {
            char c = (char)uart_read();
            resp[idx++] = c;
            tout = timeout / 10;         /* 收到数据则刷新超时计数器 */
        }
        /* 检查是否收到 "OK" 或 "ERROR" */
        if (idx >= 2) {
            if (resp[idx - 2] == 'O' && resp[idx - 1] == 'K')
                break;
            if (idx >= 5 && memcmp(resp + idx - 5, "ERROR", 5) == 0)
                break;
        }
        delay_ms(10);
    }
    resp[idx] = 0;                       /* 字符串结尾 */
    return (idx > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

/**
 * @brief  检查响应字符串中是否包含关键字
 * @param  resp    响应字符串
 * @param  keyword 要查找的关键字
 * @return 1 包含, 0 不包含
 */
static uint8_t resp_contains(const char *resp, const char *keyword)
{
    return (strstr(resp, keyword) != 0);
}

/*========================== USART3 中断处理 ===================*/

/**
 * @brief  USART3 接收中断
 * @note   将 ESP8266 发来的数据逐字节存入环形缓冲区
 *         不会阻塞主循环
 */
void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(ESP_USART, USART_IT_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(ESP_USART);
        uint16_t next = (uint16_t)((rx_head + 1) % RX_BUF_SIZE);
        if (next != rx_tail) {           /* 缓冲区未满 */
            rx_buf[rx_head] = d;
            rx_head = next;
        }
        USART_ClearITPendingBit(ESP_USART, USART_IT_RXNE);
    }
}

/*========================== 简易延时 =========================*/

/**
 * @brief  忙等待延时 (约 1ms @72MHz)
 * @param  ms  毫秒数
 * @note   近似延时, 用于短时间等待, 不适合精确定时
 */
static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 12000; i++)
        __NOP();
}
