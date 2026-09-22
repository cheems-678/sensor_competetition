/**
 * @file    as608.c
 * @brief   AS608 光学指纹传感器驱动实现 (USART2, PA2/PA3)
 * @note    AS608 是晟元 (Synochip) 的指纹识别芯片:
 *          - UART 通讯, 默认 57600 8N1
 *          - 内置指纹采集 + 特征提取 + 比对搜索 + FLASH 存储
 *          - 支持 120~300 枚指纹存储
 *
 *          【本工程适配】智能安防系统原理图:
 *          AS608 使用 USART2 (PA2 TX, PA3 RX, 57600), PA1 = TOUCH
 *          
 *          模块接线:
 *          VCC → 3.3V
 *          GND → GND
 *          TX  → PA3 (USART2_RX, 接模块 TX)
 *          RX  → PA2 (USART2_TX, 接模块 RX)
 *          TOUCH → PA1 (手指触碰检测, 低电平有效)
 *          
 *          本驱动使用轮询方式, 不占用中断。
 *          如有多个 USART 模块, 注意 USART2_IRQHandler 冲突。
 *          
 *          通信协议:
 *          命令包格式: [HEADER(2)] [ADDR(4)] [PID(1)] [LEN(2)] [DATA(N)] [CS(2)]
 *          应答包格式: [HEADER(2)] [ADDR(4)] [PID=0x07(1)] [LEN(2)] [DATA(N)] [CS(2)]
 *          
 *          HEADER = 0xEF 0x01
 *          ADDR   = 0xFF 0xFF 0xFF 0xFF (默认)
 *          PID    = 0x01 (命令) / 0x07 (应答) / 0x02 (数据包)
 *          LEN    = DATA 区域字节数 (含校验和, 大端序)
 *          DATA   = 指令码 + 参数 或 确认码 + 返回数据
 *          CS     = 校验和 (PID + LEN + DATA 的累加和, 大端序)
 *          
 *          指纹注册流程:
 *          1. GetImage            → 采集手指图像
 *          2. Img2Tz(buffer=1)    → 生成特征存入 CharBuffer1
 *          3. 抬手指 → 等待重按同一手指
 *          4. GetImage            → 采集第二张图像
 *          5. Img2Tz(buffer=2)    → 生成特征存入 CharBuffer2
 *          6. RegModel            → 合并两个特征为模板
 *          7. Store(id)           → 存入指纹库
 *          
 *          指纹识别流程:
 *          1. GetImage            → 采集手指图像
 *          2. Img2Tz(buffer=1)    → 生成特征存入 CharBuffer1
 *          3. Search              → 搜索整个指纹库
 */
#include "as608.h"

/* AS608 模块默认地址 (4 字节: FF FF FF FF) */
static const uint8_t as608_addr[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

/* USART 接收缓冲区 */
static uint8_t rx_buf[64];
static uint16_t rx_len;


/*==================================================================*
 *                      USART 初始化                                  *
 *==================================================================*/

/**
 * @brief   初始化 USART2 (PA2 TX, PA3 RX, 57600 8N1)
 * @note    配置参数:
 *          - 波特率: 57600
 *          - 字长: 8 位
 *          - 停止位: 1 位
 *          - 校验: 无
 *          - 无硬件流控
 *          - 使能发送和接收, 禁能中断 (轮询模式)
 */
static void usart_init(void)
{
    USART_InitTypeDef usart;
    GPIO_InitTypeDef gpio;

    /* 开启 USART2 和 GPIOA 时钟 (USART2 时钟来自 APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /*
     * PA2 (TX): 复用推挽输出
     * PA3 (RX): 浮空输入
     */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = AS608_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(AS608_TX_PORT, &gpio);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = AS608_RX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(AS608_RX_PORT, &gpio);

    /* 配置 TOUCH/WKUP 引脚 (PA1, 上拉输入, 低电平有效) */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = AS608_TOUCH_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(AS608_TOUCH_PORT, &gpio);

    /* USART2 配置: 57600 8N1 */
    USART_StructInit(&usart);
    usart.USART_BaudRate            = AS608_BAUDRATE;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(AS608_USART, &usart);

    /* 使能 USART2 (禁能中断, 使用轮询) */
    USART_ITConfig(AS608_USART, USART_IT_RXNE, DISABLE);
    USART_Cmd(AS608_USART, ENABLE);
}

/**
 * @brief   检测手指是否触碰传感器
 * @note    读 PA1 (TOUCH), 低电平表示有手指触碰
 * @return  1: 有手指  0: 无手指
 */
uint8_t AS608_IsTouched(void)
{
    if (GPIO_ReadInputDataBit(AS608_TOUCH_PORT, AS608_TOUCH_PIN) == Bit_RESET)
        return 1;
    return 0;
}

/*==================================================================*
 *                      UART 收发底层                                 *
 *==================================================================*/

/**
 * @brief   发送一个字节 (轮询)
 * @param   data  要发送的字节
 */
static void uart_send_byte(uint8_t data)
{
    while (USART_GetFlagStatus(AS608_USART, USART_FLAG_TXE) == RESET);
    USART_SendData(AS608_USART, data);
}

/**
 * @brief   发送多字节数据
 * @param   data  数据缓冲区
 * @param   len   数据长度
 */
static void uart_send_data(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        uart_send_byte(data[i]);
    }
}

/**
 * @brief   接收一个字节 (轮询, 带超时)
 * @param   data    输出参数, 存放接收到的字节
 * @param   timeout 超时计数 (轮询次数)
 * @return  0: 收到数据, 1: 超时
 */
static uint8_t uart_recv_byte(uint8_t *data, uint32_t timeout)
{
    while (USART_GetFlagStatus(AS608_USART, USART_FLAG_RXNE) == RESET) {
        if (--timeout == 0) {
            return 1;  /* 超时 */
        }
    }
    *data = (uint8_t)USART_ReceiveData(AS608_USART);
    return 0;
}

/**
 * @brief   清空接收缓冲区 (丢弃残留数据)
 */
static void uart_flush(void)
{
    uint8_t dummy;

    while (USART_GetFlagStatus(AS608_USART, USART_FLAG_RXNE) == SET) {
        dummy = (uint8_t)USART_ReceiveData(AS608_USART);
    }
    (void)dummy;
}

/*==================================================================*
 *                      校验和计算                                    *
 *==================================================================*/

/**
 * @brief   计算 AS608 数据包校验和
 * @param   data  数据起始指针 (从 PID 字节开始)
 * @param   len   数据长度 (PID + LEN + DATA)
 * @return  16 位校验和
 */
static uint16_t calc_checksum(const uint8_t *data, uint16_t len)
{
    uint16_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/*==================================================================*
 *                      数据包收发                                    *
 *==================================================================*/

/**
 * @brief   构建并发送命令包
 * @param   cmd       指令码
 * @param   params    参数字节数组
 * @param   param_len 参数长度
 * @note    命令包格式:
 *          [0xEF 0x01] [ADDR(4)] [0x01] [LEN(2)] [CMD] [PARAMS] [CS(2)]
 *          
 *          数据区域 = PID(1) + LEN(2) + CMD(1) + PARAMS(N)
 *          数据区域总长度 = 1 + 2 + 1 + param_len = 4 + param_len
 *          LEN 字段 = 数据区域长度 - 3(不包含PID+LEN自身) + 2(校验和)
 *                   = (4 + param_len) - 3 + 2 = 3 + param_len
 *          
 *          校验和计算范围: PID + LEN + CMD + PARAMS
 *          (即: 从 PID 开始到最后一个 PARAMS 字节)
 */
static void send_cmd_packet(uint8_t cmd, const uint8_t *params,
                            uint8_t param_len)
{
    uint8_t pkt[32];
    uint16_t i;
    uint16_t idx;
    uint16_t checksum;
    uint16_t data_len;
    uint16_t pkt_len;

    /* 数据区域总长度 (PID + LEN + CMD + PARAMS) */
    data_len = 1 + 2 + 1 + param_len;

    /* LEN 字段 = (CMD + PARAMS + 校验和) 的字节数 */
    /* = 1 + param_len + 2 = 3 + param_len */
    pkt_len = 3 + param_len;

    /* 构建数据包 */
    idx = 0;
    pkt[idx++] = 0xEF;           /* 包头 1 */
    pkt[idx++] = 0x01;           /* 包头 2 */
    pkt[idx++] = as608_addr[0];  /* 地址 */
    pkt[idx++] = as608_addr[1];
    pkt[idx++] = as608_addr[2];
    pkt[idx++] = as608_addr[3];
    pkt[idx++] = 0x01;           /* PID: 命令包 */
    pkt[idx++] = (uint8_t)(pkt_len >> 8);   /* 长度高字节 */
    pkt[idx++] = (uint8_t)(pkt_len & 0xFF); /* 长度低字节 */
    pkt[idx++] = cmd;            /* 指令码 */

    /* 添加参数 */
    for (i = 0; i < param_len; i++) {
        pkt[idx++] = params[i];
    }

    /* 计算校验和 (从 PID 到最后一个 PARAMS 字节) */
    checksum = calc_checksum(&pkt[6], data_len);

    pkt[idx++] = (uint8_t)(checksum >> 8);
    pkt[idx++] = (uint8_t)(checksum & 0xFF);

    /* 发送 */
    uart_send_data(pkt, idx);
}

/**
 * @brief   接收并解析应答包
 * @param   conf      输出参数, 确认码
 * @param   resp_data 输出参数, 附加数据缓冲区 (可为 0)
 * @param   resp_len  输出参数, 附加数据长度
 * @return  0: 成功收到有效应答, 1: 超时/校验错误
 * @note    应答包解析:
 *          收到包头后逐字节解析, 验证地址和校验和。
 *          在收到确认码后, 根据长度读取附加数据。
 *          
 *          应答包格式:
 *          [0xEF 0x01] [ADDR(4)] [0x07] [LEN(2)] [CONF] [EXTRA_DATA(N)] [CS(2)]
 *          
 *          EXTRA_DATA 长度 = LEN - 1(CONF) - 2(CS) = LEN - 3
 */
static uint8_t recv_resp_packet(uint8_t *conf, uint8_t *resp_data,
                                uint8_t *resp_len)
{
    uint16_t i;
    uint8_t  byte;
    uint16_t pkt_len;
    uint16_t checksum;
    uint16_t calc_cs;
    uint8_t  extra_len;

    /* 等待并验证包头 */
    /* 先收 0xEF */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    if (byte != 0xEF) return 1;

    /* 再收 0x01 */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    if (byte != 0x01) return 1;

    /* 读取地址 (4 字节) */
    rx_buf[0] = 0xEF;
    rx_buf[1] = 0x01;
    for (i = 0; i < 4; i++) {
        if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
        rx_buf[2 + i] = byte;
    }

    /* 读取 PID (应为 0x07) */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    if (byte != 0x07) return 1;
    rx_buf[6] = 0x07;

    /* 读取长度 (2 字节, 大端序) */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    rx_buf[7] = byte;
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    rx_buf[8] = byte;
    pkt_len = (uint16_t)(((uint16_t)rx_buf[7] << 8) | rx_buf[8]);

    /* 长度 = CONF(1) + EXTRA + CS(2), 至少 3 字节 */
    if (pkt_len < 3) return 1;
    if (pkt_len > sizeof(rx_buf) - 9) return 1;

    /* 读取确认码 (1 字节) */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    rx_buf[9] = byte;
    if (conf) *conf = byte;

    /* 读取附加数据 = pkt_len - 1(conf) - 2(cs) = pkt_len - 3 */
    extra_len = (uint8_t)(pkt_len - 3);
    for (i = 0; i < extra_len; i++) {
        if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
        rx_buf[10 + i] = byte;
    }

    /* 读取校验和 (2 字节) */
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    rx_buf[10 + extra_len] = byte;
    if (uart_recv_byte(&byte, AS608_RX_TIMEOUT)) return 1;
    rx_buf[11 + extra_len] = byte;

    checksum = (uint16_t)(((uint16_t)rx_buf[10 + extra_len] << 8) |
                rx_buf[11 + extra_len]);

    /* 验证校验和 (从 PID 开始到附加数据结束) */
    calc_cs = calc_checksum(&rx_buf[6],
                            1 + 2 + 1 + extra_len);

    if (calc_cs != checksum) return 1;

    /* 返回附加数据 */
    if (resp_data && extra_len > 0) {
        for (i = 0; i < extra_len; i++) {
            resp_data[i] = rx_buf[10 + i];
        }
    }
    if (resp_len) *resp_len = extra_len;

    rx_len = 10 + extra_len;
    return 0;
}

/*==================================================================*
 *                      内部命令函数                                  *
 *==================================================================*/

/**
 * @brief   发送指令并接收应答 (通用接口)
 * @param   cmd       指令码
 * @param   params    参数
 * @param   param_len 参数长度
 * @param   conf      确认码输出
 * @param   resp_data 附加数据输出
 * @param   resp_len  附加数据长度输出
 * @return  0: 通讯成功, 1: 通讯失败
 */
uint8_t AS608_SendCmd(uint8_t cmd, const uint8_t *params,
                      uint8_t param_len, uint8_t *conf,
                      uint8_t *resp_data, uint8_t *resp_len)
{
    uart_flush();
    send_cmd_packet(cmd, params, param_len);
    return recv_resp_packet(conf, resp_data, resp_len);
}

/**
 * @brief   发送指令, 只获取确认码
 * @param   cmd    指令码
 * @param   params 参数
 * @param   len    参数长度
 * @return  确认码 (0x00 = 成功)
 */
static uint8_t cmd_with_conf(uint8_t cmd, const uint8_t *params, uint8_t len)
{
    uint8_t conf;

    if (AS608_SendCmd(cmd, params, len, &conf, 0, 0) != 0) {
        return AS608_CONF_COM_ERR;
    }
    return conf;
}

/*==================================================================*
 *                      模块初始化                                    *
 *==================================================================*/

/**
 * @brief   初始化 AS608 指纹模块
 * @note    1. 初始化 USART2 (PA2 TX, PA3 RX, 57600)
 *          2. 发送验证密码命令 (密码默认 0x00000000)
 *          3. 验证成功后模块可正常工作
 * @return  0: 成功, 非 0: 失败
 */
uint8_t AS608_Init(void)
{
    uint8_t res;

    usart_init();

    /* 验证默认密码 */
    res = AS608_VerifyPassword(AS608_DEFAULT_PASSWORD);
    if (res != AS608_CONF_OK) {
        return res;
    }

    return AS608_CONF_OK;
}

/*==================================================================*
 *                      密码管理                                      *
 *==================================================================*/

uint8_t AS608_VerifyPassword(uint32_t password)
{
    uint8_t params[4];

    params[0] = (uint8_t)(password >> 24);
    params[1] = (uint8_t)(password >> 16);
    params[2] = (uint8_t)(password >> 8);
    params[3] = (uint8_t)(password);

    return cmd_with_conf(AS608_CMD_VALID_PWD, params, 4);
}

uint8_t AS608_SetPassword(uint32_t password)
{
    uint8_t params[4];

    params[0] = (uint8_t)(password >> 24);
    params[1] = (uint8_t)(password >> 16);
    params[2] = (uint8_t)(password >> 8);
    params[3] = (uint8_t)(password);

    return cmd_with_conf(AS608_CMD_SET_PWD, params, 4);
}

/*==================================================================*
 *                      系统参数                                      *
 *==================================================================*/

uint8_t AS608_GetSysPara(AS608_SysPara *para)
{
    uint8_t conf;
    uint8_t data[16];
    uint8_t len;

    if (AS608_SendCmd(AS608_CMD_GET_SYS_PARA, 0, 0, &conf, data, &len) != 0) {
        return AS608_CONF_COM_ERR;
    }
    if (conf != AS608_CONF_OK) {
        return conf;
    }
    if (len < 16) {
        return AS608_CONF_RECV_ERR;
    }

    if (para) {
        para->status      = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
        para->capacity    = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        para->security    = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
        para->addr        = ((uint32_t)data[6] << 24) |
                            ((uint32_t)data[7] << 16) |
                            ((uint32_t)data[8] << 8)  |
                            data[9];
        para->baud        = (uint16_t)(((uint16_t)data[10] << 8) | data[11]);
        para->packet_len  = (uint16_t)(((uint16_t)data[12] << 8) | data[13]);
    }

    return AS608_CONF_OK;
}

uint16_t AS608_GetStoredCount(void)
{
    AS608_SysPara para;

    if (AS608_GetSysPara(&para) != AS608_CONF_OK) {
        return 0xFFFF;
    }

    /*
     * Status 寄存器:
     * Bit [9:0] = 已存储指纹数
     */
    return (uint16_t)(para.status & 0x03FF);
}

/*==================================================================*
 *                      指纹图像采集与特征提取                         *
 *==================================================================*/

uint8_t AS608_GetImage(void)
{
    return cmd_with_conf(AS608_CMD_GET_IMAGE, 0, 0);
}

uint8_t AS608_Img2Tz(uint8_t buffer_id)
{
    uint8_t params[1];

    if (buffer_id != 1 && buffer_id != 2) {
        return AS608_CONF_INVALID_PARAM;
    }

    params[0] = buffer_id;
    return cmd_with_conf(AS608_CMD_IMG2TZ, params, 1);
}

/*==================================================================*
 *                      模板生成与存储                                *
 *==================================================================*/

uint8_t AS608_RegModel(void)
{
    return cmd_with_conf(AS608_CMD_REGMODEL, 0, 0);
}

uint8_t AS608_Store(uint16_t id)
{
    uint8_t params[2];

    params[0] = (uint8_t)(id >> 8);   /* ID 高字节 */
    params[1] = (uint8_t)(id);         /* ID 低字节 */
    return cmd_with_conf(AS608_CMD_STORE, params, 2);
}

/*==================================================================*
 *                      指纹搜索 (1:N)                               *
 *==================================================================*/

uint8_t AS608_Search(uint16_t *page_id, uint16_t *score)
{
    uint8_t conf;
    uint8_t data[4];
    uint8_t len;

    /*
     * Search 参数:
     * BufferID (1 字节) = 0x01 (使用 CharBuffer1)
     * StartPage (2 字节) = 0x0000 (从第 0 页开始搜索)
     * PageCount (2 字节) = 0x03FF (搜索全部, 最大 1023 页)
     */
    uint8_t params[5] = { 0x01, 0x00, 0x00, 0x03, 0xFF };

    if (AS608_SendCmd(AS608_CMD_SEARCH, params, 5, &conf, data, &len) != 0) {
        return AS608_CONF_COM_ERR;
    }

    if (conf == AS608_CONF_OK) {
        if (page_id && len >= 2) {
            *page_id = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
        }
        if (score && len >= 4) {
            *score = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        }
    }

    return conf;
}

/*==================================================================*
 *                      高级功能: 注册与识别                          *
 *==================================================================*/

/**
 * @brief   等待手指按压并采集图像
 * @note    在 timeout_ms 时间内重试, 直到检测到手指或超时。
 *          每次重试间隔 AS608_RETRY_DELAY ms。
 */
static uint8_t wait_and_capture(uint32_t timeout_ms)
{
    uint8_t res;
    uint32_t elapsed;
    volatile uint32_t delay;

    elapsed = 0;
    while (elapsed < timeout_ms) {
        res = AS608_GetImage();
        if (res == AS608_CONF_OK) {
            return AS608_CONF_OK;       /* 采集成功 */
        }
        if (res != AS608_CONF_NO_FINGER) {
            return res;                 /* 其他错误 */
        }

        /* 延时 AS608_RETRY_DELAY ms */
        for (delay = 0; delay < AS608_RETRY_DELAY * 7200; delay++);
        elapsed += AS608_RETRY_DELAY;
    }

    return AS608_CONF_NO_FINGER;        /* 超时, 无手指 */
}

uint8_t AS608_Enroll(uint16_t id)
{
    uint8_t res;

    /* 步骤 1: 采集第一张图像, 等待手指 */
    res = wait_and_capture(AS608_RETRY_COUNT * AS608_RETRY_DELAY);
    if (res != AS608_CONF_OK) return res;

    /* 步骤 2: 生成特征 → CharBuffer1 */
    res = AS608_Img2Tz(1);
    if (res != AS608_CONF_OK) return res;

    /* 步骤 3: 提示抬手指, 等待重新按压同一手指 */
    /* (应用层可以通过串口或其他方式提示用户) */

    /* 等待手指抬起后再等待再次按压 */
    {
        uint32_t wait;
        volatile uint32_t delay;

        /* 先等待手指抬起 */
        wait = 0;
        while (wait < 100) {
            res = AS608_GetImage();
            if (res == AS608_CONF_NO_FINGER) break;
            for (delay = 0; delay < 10 * 7200; delay++);
            wait++;
        }

        /* 等待手指再次按压 */
        res = wait_and_capture(AS608_RETRY_COUNT * AS608_RETRY_DELAY);
        if (res != AS608_CONF_OK) return res;
    }

    /* 步骤 4: 生成特征 → CharBuffer2 */
    res = AS608_Img2Tz(2);
    if (res != AS608_CONF_OK) return res;

    /* 步骤 5: 合并两个特征为模板 */
    res = AS608_RegModel();
    if (res != AS608_CONF_OK) return res;

    /* 步骤 6: 存入指纹库 */
    res = AS608_Store(id);

    return res;
}

uint8_t AS608_Identify(uint16_t *page_id, uint16_t *score)
{
    uint8_t res;

    /* 步骤 1: 采集图像 */
    res = wait_and_capture(AS608_RETRY_COUNT * AS608_RETRY_DELAY);
    if (res != AS608_CONF_OK) return res;

    /* 步骤 2: 生成特征 → CharBuffer1 */
    res = AS608_Img2Tz(1);
    if (res != AS608_CONF_OK) return res;

    /* 步骤 3: 搜索指纹库 */
    res = AS608_Search(page_id, score);

    return res;
}

/*==================================================================*
 *                      指纹管理                                      *
 *==================================================================*/

uint8_t AS608_Delete(uint16_t id)
{
    uint8_t params[4];

    /* StartPage(2) + Count(2) = 删除单个指纹 */
    params[0] = (uint8_t)(id >> 8);
    params[1] = (uint8_t)(id);
    params[2] = 0x00;
    params[3] = 0x01;

    return cmd_with_conf(AS608_CMD_DELETE, params, 4);
}

uint8_t AS608_DeleteMulti(uint16_t start, uint16_t count)
{
    uint8_t params[4];

    params[0] = (uint8_t)(start >> 8);
    params[1] = (uint8_t)(start);
    params[2] = (uint8_t)(count >> 8);
    params[3] = (uint8_t)(count);

    return cmd_with_conf(AS608_CMD_DELETE, params, 4);
}

uint8_t AS608_Empty(void)
{
    return cmd_with_conf(AS608_CMD_EMPTY, 0, 0);
}

uint8_t AS608_SetSecurityLevel(uint8_t level)
{
    uint8_t params[3];

    if (level < 1) level = 1;
    if (level > 5) level = 5;

    /*
     * SetSysPara 参数:
     * Byte 0: 参数编号 (0x05 = 安全等级)
     * Byte 1-2: 参数值 (大端序)
     */
    params[0] = 0x05;       /* 安全等级编号 */
    params[1] = 0x00;
    params[2] = level;

    return cmd_with_conf(AS608_CMD_SET_SYS_PARA, params, 3);
}
