/**
 * @file    hc05.c
 * @brief   HC-05 蓝牙模块驱动实现 (UART AT 指令 + 数据透传)
 * @note    AT 指令格式: "AT+<CMD>?\r\n" (查询) / "AT+<CMD>=<param>\r\n" (设置)
 *          响应格式: "<data>\r\nOK\r\n" 或 "ERROR\r\n"
 *          
 *          状态机:
 *          ┌─────────┐  拉高 KEY + 复位   ┌───────────┐
 *          │ 数据模式  │ ────────────────→│ AT 模式    │
 *          │ (透传)   │ ←─────────────── │ (配置)     │
 *          └─────────┘  拉低 KEY         └───────────┘
 *          
 *          典型使用流程 (从机):
 *          1. HC05_Init()             初始化硬件
 *          2. HC05_EnterATMode()      进入 AT 配置模式
 *          3. HC05_AT()               检测模块是否在线
 *          4. HC05_SetName("myDev")   设置设备名称
 *          5. HC05_SetPassword("0000")设置配对密码
 *          6. HC05_SetRole(SLAVE)     设为从机
 *          7. HC05_ExitATMode()       退出 AT 进入数据模式
 *          8. 手机蓝牙搜索并连接
 *          9. HC05_SendData()         发送数据
 *          10. HC05_RecvData()        接收数据
 *          
 *          典型使用流程 (主机):
 *          1. HC05_Init()
 *          2. HC05_EnterATMode()
 *          3. HC05_SetRole(MASTER)    设为主机
 *          4. HC05_SetCMode(FIXED)    设为固定地址连接
 *          5. HC05_Bind(&target_addr) 绑定目标设备
 *          6. HC05_ExitATMode()
 *          7. HC05_IsConnected()      检测连接状态
 */
#include "hc05.h"
#include <string.h>
#include <stdio.h>

/*========================== 内部常量 =========================*/
#define RX_BUF_SIZE         256         /* 接收环形缓冲区大小 (字节) */
#define TX_BUF_SIZE         128         /* AT 命令缓冲区大小 (字节)  */
#define AT_SHORT_TIMEOUT    2000        /* 短超时: 普通 AT 指令 (ms) */
#define AT_LONG_TIMEOUT     5000        /* 长超时: 复位/查询 (ms)    */
#define INQ_TIMEOUT         10000       /* 设备查询超时 (ms)         */

/*========================== 内部变量 =========================*/
static uint8_t           rx_buf[RX_BUF_SIZE];        /* 中断接收环形缓冲区       */
static volatile uint16_t rx_head;                     /* 生产者索引 (中断写入位置) */
static volatile uint16_t rx_tail;                     /* 消费者索引 (主循环读取位置) */
static char              tx_buf[TX_BUF_SIZE];         /* AT 命令格式化缓冲区      */
static void              (*recv_cb)(const uint8_t *, uint16_t);  /* 接收回调指针 */

/*========================== 内部函数声明 =====================*/
static void     usart_init(void);
static void     gpio_init(void);
static void     uart_send_str(const char *s);
static void     uart_send_buf(const uint8_t *d, uint16_t len);
static uint16_t uart_avail(void);
static uint8_t  uart_read(void);
static void     uart_flush(void);
static void     delay_ms(uint32_t ms);

/**
 * @brief   发送 AT 指令并等待响应 (核心内部函数)
 * @param   cmd      完整的 AT 指令字符串 (须包含 \r\n)
 * @param   resp     存放模块响应的缓冲区
 * @param   max_len  缓冲区最大长度
 * @param   timeout  等待超时时间 (毫秒)
 * @return  BT_OK          收到响应 (包含 OK 或 ERROR 均算有响应)
 *          BT_ERR_TIMEOUT 超时且未收到任何数据
 * @note    响应检测逻辑:
 *          - 每次收到数据后重置超时计数器 (防粘包断帧)
 *          - 收到 "OK" 或 "ERROR" 立即返回 (无论后续是否还有数据)
 *          - 超时后返回已接收的数据 (可能不完整)
 */
static BTStatus at_cmd(const char *cmd, char *resp, uint16_t max_len,
                       uint32_t timeout);

/**
 * @brief   检查字符串中是否包含指定关键字
 * @param   str      待检查的字符串
 * @param   keyword  要查找的关键字
 * @return  1  包含关键字
 *          0  不包含
 */
static uint8_t str_contains(const char *str, const char *keyword);

/**
 * @brief   从字符串中解析蓝牙 MAC 地址
 * @param   str   源字符串 (格式: "12:34:56:78:9a:bc")
 * @param   addr  存放解析结果的地址结构体
 * @return  1  解析成功
 *          0  格式错误
 * @note    地址字符串中各字节以冒号分隔, 十六进制格式
 */
static uint8_t parse_addr(const char *str, BTAddr *addr);

/**
 * @brief   将蓝牙地址格式化为冒号分隔的字符串
 * @param   addr  地址结构体
 * @param   str   存放字符串的缓冲区 (至少 18 字节)
 */
static void addr_to_str(const BTAddr *addr, char *str);

/*========================== 模块初始化 =======================*/

/**
 * @brief  初始化 HC-05 模块
 * @note   执行步骤:
 *         1. 清空接收环形缓冲区
 *         2. 清空回调指针
 *         3. 初始化控制 GPIO (KEY=低, 进入数据模式)
 *         4. 初始化 STATE 引脚 (输入模式)
 *         5. 初始化 USART1 (PA9 TX, PA10 RX, 9600 8N1)
 *         6. 使能 RXNE 中断
 * @return BT_OK  初始化完成
 */
BTStatus HC05_Init(void)
{
    rx_head = 0;
    rx_tail = 0;
    recv_cb = 0;

    gpio_init();        /* 先初始化 GPIO (含 KEY 拉低) */
    usart_init();       /* 再初始化 USART */

    return BT_OK;
}

/**
 * @brief  进入 AT 指令配置模式
 * @note   操作方法:
 *         将 KEY 拉高, 然后给模块一个复位脉冲 (拉低 RTS 脚或
 *         利用模块的 PIO 复位, 这里通过重初始化 KEY 和延时实现)
 *         
 *         更可靠的方式: 在 HC05_Init 前硬件拉高 KEY 再上电
 *         这里采用软件方法: 拉高 KEY → 延时 → 发送 AT 测试
 *         
 *         部分模块在 KEY 拉高后需要重新上电才能进入 AT 模式,
 *         如果软件方法无效, 请硬件上将 KEY 接 3.3V 后重新上电
 * @return BT_OK           成功进入 AT 模式
 *         BT_ERR_TIMEOUT  模块无响应
 */
BTStatus HC05_EnterATMode(void)
{
    /* 拉高 KEY 引脚, 通知模块进入 AT 模式 */
    GPIO_WriteBit(HC05_KEY_PORT, HC05_KEY_PIN, Bit_SET);
    delay_ms(100);                       /* 等待模块识别 KEY 电平 */

    /* 部分模块需要重新上电才能进入 AT 模式, 此处尝试软复位 */
    uart_send_str("AT+RESET\r\n");
    delay_ms(2000);                      /* 等待重启完成 */

    /* 发送 AT 测试指令, 确认模块已在 AT 模式 */
    return HC05_AT();
}

/**
 * @brief  退出 AT 模式, 回到数据透传模式
 * @note   将 KEY 拉低, 模块自动进入数据透传模式
 *         如果需要, 可发送 AT+RESET 让模块以数据模式重启
 * @return BT_OK
 */
BTStatus HC05_ExitATMode(void)
{
    GPIO_WriteBit(HC05_KEY_PORT, HC05_KEY_PIN, Bit_RESET);
    delay_ms(100);
    return BT_OK;
}

/*========================== 基础 AT 指令 =====================*/

/**
 * @brief  测试 AT 通信
 * @note   发送 "AT\r\n", 期望返回 "OK"
 * @return BT_OK              模块正常应答
 *         BT_ERR_NO_RESPONSE 无应答或应答错误
 */
BTStatus HC05_AT(void)
{
    char resp[16];
    BTStatus st = at_cmd("AT\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != BT_OK) return BT_ERR_NO_RESPONSE;
    return str_contains(resp, "OK") ? BT_OK : BT_ERR_NO_RESPONSE;
}

/**
 * @brief  软复位模块 (AT+RESET)
 * @note   复位后模块重新启动:
 *         - KEY=高: 进入 AT 模式
 *         - KEY=低: 进入数据模式
 * @return BT_OK  复位成功
 */
BTStatus HC05_Reset(void)
{
    char resp[32];
    BTStatus st = at_cmd("AT+RESET\r\n", resp, sizeof(resp), AT_LONG_TIMEOUT);
    delay_ms(2000);                      /* 等待模块完全重启 */
    return st;
}

/**
 * @brief  恢复出厂设置 (AT+ORGL)
 * @note   恢复后默认参数:
 *         - 名称: HC-05
 *         - 密码: 1234
 *         - 波特率: 9600, 1停止位, 无校验
 *         - 角色: 从机 (0)
 *         - 连接模式: 任意 (1)
 * @return BT_OK  恢复成功
 */
BTStatus HC05_RestoreDefault(void)
{
    char resp[32];
    BTStatus st = at_cmd("AT+ORGL\r\n", resp, sizeof(resp), AT_LONG_TIMEOUT);
    delay_ms(2000);                      /* 恢复后模块自动复位 */
    return st;
}

/*========================== 模块配置 =========================*/

/**
 * @brief  设置蓝牙广播名称 (AT+NAME=<name>)
 * @param  name  蓝牙名称字符串 (最长 31 字节)
 * @return BT_OK          设置成功
 *         BT_ERR_PARAM   名称过长或含非法字符
 * @note   名称不能包含特殊控制字符, 建议使用字母数字
 *         修改后需复位才能生效 (部分固件版本即时生效)
 */
BTStatus HC05_SetName(const char *name)
{
    if (!name) return BT_ERR_PARAM;

    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "AT+NAME=%s\r\n", name);
    if (n >= (int)sizeof(cmd)) return BT_ERR_PARAM;  /* 名称过长 */

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  设置配对密码 (AT+PSWD=<pwd>)
 * @param  pwd  4 位数字密码, 如 "0000"
 * @return BT_OK          设置成功
 *         BT_ERR_PARAM   密码为空
 * @note   默认密码: 1234
 *         建议修改为自定义密码防止他人连接
 */
BTStatus HC05_SetPassword(const char *pwd)
{
    if (!pwd) return BT_ERR_PARAM;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+PSWD=%s\r\n", pwd);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  设置串口参数 (AT+UART=<baud>,<stopbits>,<parity>)
 * @param  baud      波特率 (常见值: 4800/9600/19200/38400/57600/115200)
 * @param  stopbits  停止位: 0=1bit, 1=2bits
 * @param  parity    校验位: 0=None, 1=Odd, 2=Even
 * @return BT_OK  设置成功
 * @note   此指令修改的是 HC-05 模块自身的串口参数,
 *         调用后必须调用 HC05_SetBaudRate() 同步 MCU 端波特率
 */
BTStatus HC05_SetUART(uint32_t baud, uint8_t stopbits, uint8_t parity)
{
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+UART=%lu,%u,%u\r\n",
             (unsigned long)baud, (unsigned int)stopbits, (unsigned int)parity);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  同步更新 MCU 端串口波特率
 * @param  baud  新的波特率值
 * @note   此函数不发送任何 AT 指令, 只重新配置 USART 外设寄存器
 *         必须在调用 HC05_SetUART() 之后调用, 使两端保持一致
 */
void HC05_SetBaudRate(uint32_t baud)
{
    USART_InitTypeDef usart;

    /* 读取当前 USART 配置, 只修改波特率 */
    USART_StructInit(&usart);
    usart.USART_BaudRate = baud;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Cmd(HC05_USART, DISABLE);      /* 配置前先关闭 USART */
    USART_Init(HC05_USART, &usart);
    USART_Cmd(HC05_USART, ENABLE);       /* 重新使能            */
}

/**
 * @brief  设置模块角色 (AT+ROLE=<role>)
 * @param  role  BT_ROLE_SLAVE(0)=从机 / BT_ROLE_MASTER(1)=主机
 * @return BT_OK          设置成功
 *         BT_ERR_PARAM   参数错误
 * @note   从机: 等待手机或其他主机连接 (默认)
 *          主机: 主动连接指定的从设备 (需配合 BIND 和 CMODE)
 */
BTStatus HC05_SetRole(uint8_t role)
{
    if (role > 1) return BT_ERR_PARAM;

    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+ROLE=%u\r\n", (unsigned int)role);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  设置连接模式 (AT+CMODE=<mode>)
 * @param  mode  BT_CMODE_FIXED(0)=固定地址 / BT_CMODE_ANY(1)=任意
 * @return BT_OK          设置成功
 *         BT_ERR_PARAM   参数错误
 * @note   CMODE=0 (固定地址):
 *            只允许 BIND 绑定的设备连接, 安全性高
 *          CMODE=1 (任意地址):
 *            允许任何已配对的设备连接, 方便调试
 *          从机推荐使用 CMODE=1, 主机必须使用 CMODE=0
 */
BTStatus HC05_SetCMode(uint8_t mode)
{
    if (mode > 1) return BT_ERR_PARAM;

    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CMODE=%u\r\n", (unsigned int)mode);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  绑定目标蓝牙设备地址 (AT+BIND=<addr>)
 * @param  addr  目标设备的 MAC 地址 (6 字节)
 * @return BT_OK          绑定成功
 *         BT_ERR_PARAM   参数为空
 * @note   地址格式: 在模块内部为 12:34:56:78:9A:BC
 *         仅在 CMODE=0 时有效
 *         BIND 地址必须与对端设备的 ADDR 一致
 */
BTStatus HC05_Bind(BTAddr *addr)
{
    if (!addr) return BT_ERR_PARAM;

    char addr_str[18];
    addr_to_str(addr, addr_str);

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+BIND=%s\r\n", addr_str);

    char resp[24];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/*========================== 查询接口 =========================*/

/**
 * @brief  获取本机蓝牙地址 (AT+ADDR?)
 * @param  addr  存放地址的结构体指针
 * @return BT_OK          获取成功
 *         BT_ERR_PARAM   参数为空
 *         BT_ERR_TIMEOUT 模块无响应
 * @note   响应示例: "+ADDR:12:34:56:78:9a:bc"
 *         地址是模块的唯一标识, 可用于主机模式下的 BIND
 */
BTStatus HC05_GetAddr(BTAddr *addr)
{
    if (!addr) return BT_ERR_PARAM;

    char resp[32];
    BTStatus st = at_cmd("AT+ADDR?\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != BT_OK) return st;

    /* 解析 "+ADDR:12:34:56:78:9a:bc" */
    char *p = strstr(resp, "+ADDR:");
    if (!p) return BT_ERR_NO_RESPONSE;

    p += 6;                              /* 跳过 "+ADDR:" */
    return parse_addr(p, addr) ? BT_OK : BT_ERR_NO_RESPONSE;
}

/**
 * @brief  获取当前模块状态 (AT+STATE?)
 * @param  state   存放状态字符串 (如 "READY", "CONNECTED")
 * @param  max_len 缓冲区最大长度
 * @return BT_OK  获取成功
 * @note   状态含义:
 *         - INITIALIZING: 模块正在初始化, 不可操作
 *         - READY: AT 模式下准备就绪, 可接收指令
 *         - PAIRABLE: 可被配对 (从机, 未连接)
 *         - PAIRED: 已配对 (已交换配对信息)
 *         - INQUIRING: 正在查询周围设备 (主机)
 *         - CONNECTING: 正在建立连接
 *         - CONNECTED: 已建立蓝牙连接, 可进行数据透传
 */
BTStatus HC05_GetState(char *state, uint8_t max_len)
{
    if (!state || max_len == 0) return BT_ERR_PARAM;

    char resp[48];
    BTStatus st = at_cmd("AT+STATE?\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != BT_OK) return st;

    /* 解析 "+STATE: <state>" */
    char *p = strstr(resp, "+STATE:");
    if (!p) return BT_ERR_NO_RESPONSE;

    p += 7;                              /* 跳过 "+STATE:" */
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;  /* 跳过空白 */

    uint8_t i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < max_len - 1)
        state[i++] = *p++;
    state[i] = '\0';

    return (i > 0) ? BT_OK : BT_ERR_NO_RESPONSE;
}

/**
 * @brief  获取固件版本 (AT+VERSION?)
 * @param  ver  存放版本信息的结构体
 * @return BT_OK          获取成功
 *         BT_ERR_PARAM   参数为空
 * @note   响应示例: "+VERSION:2.0-20100601"
 *         版本号含义: 主版本.次版本-编译日期
 */
BTStatus HC05_GetVersion(BTVersion *ver)
{
    if (!ver) return BT_ERR_PARAM;

    char resp[48];
    BTStatus st = at_cmd("AT+VERSION?\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != BT_OK) return st;

    /* 解析 "+VERSION:<major>.<minor>-<variant>" */
    char *p = strstr(resp, "+VERSION:");
    if (!p) return BT_ERR_NO_RESPONSE;

    p += 8;                              /* 跳过 "+VERSION:" */
    ver->major = 0;
    ver->minor = 0;
    ver->variant = 0;

    /* 解析主版本号 */
    while (*p >= '0' && *p <= '9') {
        ver->major = ver->major * 10 + (*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            ver->minor = ver->minor * 10 + (*p - '0');
            p++;
        }
    }

    /* 剩余字符作为变体号 (例如日期) */
    if (*p == '-') {
        p++;
        while (*p >= '0' && *p <= '9') {
            ver->variant = ver->variant * 10 + (*p - '0');
            p++;
        }
    }

    return BT_OK;
}

/**
 * @brief  获取已连接设备的蓝牙地址
 * @param  addr  存放远端设备地址的结构体
 * @return BT_OK           获取成功
 *         BT_ERR_STATE    当前未连接
 * @note   仅在已连接状态下有效
 *         某些固件通过 AT+RNAME? 可获取远端设备名称
 */
BTStatus HC05_GetRemoteAddr(BTAddr *addr)
{
    if (!addr) return BT_ERR_PARAM;

    /* 先检查连接状态 */
    if (!HC05_IsConnected()) return BT_ERR_STATE;

    /* 部分固件使用 AT+REMOTEADDR? 或 AT+BIND? 查询已连接的设备地址 */
    char resp[48];
    BTStatus st = at_cmd("AT+BIND?\r\n", resp, sizeof(resp), AT_SHORT_TIMEOUT);
    if (st != BT_OK) return st;

    /* 解析 "+BIND:12:34:56:78:9a:bc" */
    char *p = strstr(resp, "+BIND:");
    if (!p) return BT_ERR_NO_RESPONSE;

    p += 6;                              /* 跳过 "+BIND:" */
    return parse_addr(p, addr) ? BT_OK : BT_ERR_NO_RESPONSE;
}

/*========================== 数据收发 =========================*/

/**
 * @brief  通过串口发送数据
 * @param  data  数据缓冲区
 * @param  len   数据长度 (字节)
 * @return BT_OK       发送成功
 *         BT_ERR_SEND 数据指针为空或长度为 0
 * @note   在数据透传模式下, 数据会直接通过蓝牙发送到对端设备
 *         在 AT 模式下, 数据会作为原始字节发送到串口 (慎用)
 *         此函数为阻塞发送, 会等待每个字节发送完成
 */
BTStatus HC05_SendData(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return BT_ERR_SEND;

    uart_send_buf(data, len);
    return BT_OK;
}

/**
 * @brief  从接收缓冲区读取数据
 * @param  buf     存放数据的缓冲区
 * @param  max_len 缓冲区最大长度
 * @return 实际读取的字节数 (0 = 无数据)
 * @note   非阻塞读取, 从环形缓冲区取出数据
 *         如果注册了回调函数, 数据也会通过回调通知
 */
uint16_t HC05_RecvData(uint8_t *buf, uint16_t max_len)
{
    if (!buf || max_len == 0) return 0;

    uint16_t idx = 0;
    while (uart_avail() && idx < max_len)
        buf[idx++] = uart_read();

    return idx;
}

/**
 * @brief  检测蓝牙是否已连接
 * @return 1  已连接 (STATE 引脚为高电平)
 *         0  未连接
 * @note   通过读取 STATE 引脚电平判断
 *         若未定义 STATE 引脚宏, 始终返回 0
 *         STATE 引脚在连接成功后输出高电平
 */
uint8_t HC05_IsConnected(void)
{
#ifdef HC05_STATE_PIN
    return (GPIO_ReadInputDataBit(HC05_STATE_PORT, HC05_STATE_PIN) == Bit_SET)
           ? 1 : 0;
#else
    return 0;
#endif
}

/**
 * @brief  注册接收回调函数
 * @param  cb  回调函数指针, 在串口接收中断中调用
 *             当模块有数据到达时, 系统通过此回调通知上层
 *             回调参数: (数据指针, 数据长度)
 * @note   回调函数在中断上下文中执行, 应尽量简短
 *         建议在回调中仅设置标志位或使用队列传递数据
 *         传 NULL 取消注册
 */
void HC05_SetRecvCallback(void (*cb)(const uint8_t *, uint16_t))
{
    recv_cb = cb;
}

/*========================== 高级功能 =========================*/

/**
 * @brief  设置配对模式 (AT+TYPE=<mode>) — 部分固件支持
 * @param  mode  0=固定密码, 1=绑定
 * @return BT_OK  设置成功
 * @note   非所有 HC-05 固件都支持此指令
 *         若不支持, 模块会返回 ERROR, 这不影响正常使用
 */
BTStatus HC05_SetPairMode(uint8_t mode)
{
    if (mode > 1) return BT_ERR_PARAM;

    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+TYPE=%u\r\n", (unsigned int)mode);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  设置查询码 (AT+IAC=<iac>) — 主机模式
 * @param  iac  查询访问码 (通用查询: 0x9E8B33)
 * @return BT_OK  设置成功
 * @note   仅在主机模式下有意义
 *         通用查询码 0x9E8B33 可发现绝大多数蓝牙设备
 *         专用查询码用于发现特定类型的设备
 */
BTStatus HC05_SetInquiryCode(uint32_t iac)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+IAC=%lX\r\n", (unsigned long)iac);

    char resp[16];
    BTStatus st = at_cmd(cmd, resp, sizeof(resp), AT_SHORT_TIMEOUT);
    return (st == BT_OK && str_contains(resp, "OK")) ? BT_OK : st;
}

/**
 * @brief  开始查询周围蓝牙设备 (AT+INQ) — 主机模式
 * @param  devices     存放查询结果的地址数组
 * @param  max_num     最多可存放的设备数量
 * @param  timeout_ms  查询超时时间 (毫秒), 建议 5000~10000
 * @return >=0  发现的设备数量
 *         BT_ERR_TIMEOUT  超时未返回结果
 *         BT_ERR_STATE    模块状态异常 (可能不在主机模式)
 * @note   仅在主机模式下有效
 *         查询期间模块会进入 INQUIRING 状态, 不响应其他 AT 指令
 *         查询结果格式: "+INQ:12:34:56:78:9a:bc,<type>,<rssi>"
 *         每个发现的设备返回一行 +INQ 数据
 */
int16_t HC05_Inquiry(BTAddr *devices, uint8_t max_num, uint32_t timeout_ms)
{
    if (!devices || max_num == 0) return BT_ERR_PARAM;

    uart_flush();                        /* 清空缓冲区 */

    /* 发送查询指令 */
    uart_send_str("AT+INQ\r\n");

    /* 等待模块返回查询结果 */
    uint32_t timeout = (timeout_ms > 0) ? timeout_ms : INQ_TIMEOUT;
    uint32_t elapsed = 0;
    uint16_t idx = 0;
    char resp[256];
    uint16_t rlen = 0;

    while (elapsed < timeout) {
        while (uart_avail() && rlen < sizeof(resp) - 1)
            resp[rlen++] = (char)uart_read();

        /* 检查是否收到 OK (查询完成) 或 ERROR */
        if (str_contains(resp, "OK\r\n") || str_contains(resp, "ERROR\r\n"))
            break;

        delay_ms(10);
        elapsed += 10;
    }
    resp[rlen] = '\0';

    /* 解析 +INQ 行 */
    uint8_t found = 0;
    char *line = resp;
    while (line && found < max_num) {
        char *p = strstr(line, "+INQ:");
        if (!p) break;

        p += 5;                          /* 跳过 "+INQ:" */

        /* 解析地址: "12:34:56:78:9a:bc,type,rssi" */
        char addr_str[18];
        uint8_t ai = 0;
        while (*p && *p != ',' && ai < 17)
            addr_str[ai++] = *p++;
        addr_str[ai] = '\0';

        if (parse_addr(addr_str, &devices[found]))
            found++;

        /* 移动到下一行 */
        line = strchr(p, '\n');
        if (line) line++;
    }

    return (found > 0) ? (int16_t)found : BT_ERR_TIMEOUT;
}

/**
 * @brief  取消正在进行的设备查询 (AT+INQC)
 * @return BT_OK  取消成功
 */
BTStatus HC05_CancelInquiry(void)
{
    uart_flush();
    uart_send_str("AT+INQC\r\n");
    return BT_OK;
}

/*========================== 底层串口初始化 ===================*/

/**
 * @brief  初始化 USART1 (PA9 TX, PA10 RX)
 * @note   配置为 9600 8N1, 使能 RXNE 中断
 *         USART1 时钟来自 APB2
 */
static void usart_init(void)
{
    USART_InitTypeDef usart;
    GPIO_InitTypeDef  gpio;

    /* 使能 USART1 和 GPIOA 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置 PA9 = USART1_TX (推挽复用输出) */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = HC05_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(HC05_TX_PORT, &gpio);

    /* 配置 PA10 = USART1_RX (浮空输入) */
    gpio.GPIO_Pin  = HC05_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(HC05_RX_PORT, &gpio);

    /* 配置 USART1 中断 (NVIC) */
    NVIC_InitTypeDef nvic;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    /* 配置 USART1 参数 */
    USART_StructInit(&usart);
    usart.USART_BaudRate = HC05_USART_BAUD;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(HC05_USART, &usart);

    /* 使能接收中断和 USART */
    USART_ITConfig(HC05_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(HC05_USART, ENABLE);
}

/**
 * @brief  初始化 HC-05 控制引脚
 * @note   KEY 引脚: 推挽输出, 初始化为低电平 (数据模式)
 *         STATE 引脚: 浮空输入 (读取蓝牙连接状态)
 *         同时使能 GPIOB 时钟
 */
static void gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 KEY 和 STATE 所在 GPIO 端口时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* 配置 KEY = 推挽输出, 初始低电平 (数据模式) */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = HC05_KEY_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(HC05_KEY_PORT, &gpio);
    GPIO_WriteBit(HC05_KEY_PORT, HC05_KEY_PIN, Bit_RESET);  /* 默认数据模式 */

#ifdef HC05_STATE_PIN
    /* 配置 STATE = 浮空输入 */
    gpio.GPIO_Pin  = HC05_STATE_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(HC05_STATE_PORT, &gpio);
#endif
}

/*========================== 串口收发 =========================*/

/**
 * @brief  轮询方式发送字符串
 * @param  s  以 '\0' 结尾的字符串
 * @note   阻塞发送, 每字节等待 TC 标志置位
 */
static void uart_send_str(const char *s)
{
    while (*s) {
        USART_SendData(HC05_USART, (uint8_t)*s);
        while (USART_GetFlagStatus(HC05_USART, USART_FLAG_TC) == RESET);
        s++;
    }
}

/**
 * @brief  轮询方式发送数据缓冲区
 * @param  d   数据指针
 * @param  len 字节数
 * @note   阻塞发送, 适合发送二进制数据
 */
static void uart_send_buf(const uint8_t *d, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USART_SendData(HC05_USART, d[i]);
        while (USART_GetFlagStatus(HC05_USART, USART_FLAG_TC) == RESET);
    }
}

/**
 * @brief  查询环形缓冲区中是否有未读数据
 * @return 1  缓冲区有数据
 *         0  缓冲区空
 * @note   通过比较头尾指针判断, 非阻塞
 */
static uint16_t uart_avail(void)
{
    return (rx_head != rx_tail) ? 1 : 0;
}

/**
 * @brief  从环形缓冲区读取一个字节
 * @return 读取的字节数据
 * @note   若缓冲区为空则阻塞等待
 *         通常在调用 uart_avail() 确认有数据后再调用
 */
static uint8_t uart_read(void)
{
    while (rx_tail == rx_head);          /* 等待数据 (若为空则阻塞) */
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1) % RX_BUF_SIZE);
    return c;
}

/**
 * @brief  清空接收环形缓冲区
 * @note   将头尾指针归零, 丢弃所有未读数据
 *         通常在发送 AT 指令前调用, 确保不受残留数据干扰
 */
static void uart_flush(void)
{
    rx_head = 0;
    rx_tail = 0;
}

/*========================== AT 指令收发核心 ===================*/

/**
 * @brief  发送 AT 指令并等待响应
 * @param  cmd      完整的 AT 指令 (须含 \r\n 结尾)
 * @param  resp     存放响应字符串的缓冲区
 * @param  max_len  缓冲区最大长度
 * @param  timeout  超时时间 (毫秒)
 * @return BT_OK           收到模块响应 (含 OK 或 ERROR)
 *         BT_ERR_TIMEOUT  超时且未收到任何数据
 * @note   实现细节:
 *         1. 先清空接收缓冲区, 防止旧数据干扰
 *         2. 发送 AT 指令
 *         3. 以 10ms 为周期轮询接收数据
 *         4. 每次收到数据后重置超时计数器 (防粘包)
 *         5. 检测到 "OK" 或 "ERROR" 立即返回
 *         6. 超时返回已有数据
 */
static BTStatus at_cmd(const char *cmd, char *resp, uint16_t max_len,
                       uint32_t timeout)
{
    uart_flush();                        /* 清空接收缓冲区, 排除干扰 */
    uart_send_str(cmd);                  /* 发送 AT 指令               */

    uint32_t tick = timeout / 10;        /* 每 10ms 轮询一次          */
    uint16_t idx = 0;

    while (tick--) {
        /* 从环形缓冲区读取可用数据 */
        while (uart_avail() && idx < max_len - 1) {
            char c = (char)uart_read();
            resp[idx++] = c;
            tick = timeout / 10;         /* 收到数据则延长超时, 等待完整响应 */
        }

        /* 检测终止条件: "OK\r\n" 或 "ERROR\r\n" */
        if (idx >= 2) {
            if (resp[idx - 2] == 'O' && resp[idx - 1] == 'K')
                break;
            /* 注意: ERROR 长度为 5, 检测最后 5 个字节 */
            if (idx >= 5 && memcmp(resp + idx - 5, "ERROR", 5) == 0)
                break;
        }

        delay_ms(10);                    /* 等待 10ms 后再次轮询 */
    }

    resp[idx] = '\0';                    /* 字符串结束符 */
    return (idx > 0) ? BT_OK : BT_ERR_TIMEOUT;
}

/*========================== 工具函数 =========================*/

/**
 * @brief  检查字符串中是否包含关键字
 * @param  str      被搜索的字符串
 * @param  keyword  要查找的关键字
 * @return 1  找到关键字
 *         0  未找到
 */
static uint8_t str_contains(const char *str, const char *keyword)
{
    return (strstr(str, keyword) != 0);
}

/**
 * @brief  从冒号分隔的十六进制地址字符串解析 MAC 地址
 * @param  str   地址字符串, 如 "12:34:56:78:9a:bc"
 * @param  addr  存放解析结果的地址结构体
 * @return 1  解析成功
 *         0  格式错误
 * @note   地址字符串中各字节以冒号分隔, 使用十六进制表示
 *         解析结果 addr[0] = 0x12, addr[1] = 0x34, ...
 */
static uint8_t parse_addr(const char *str, BTAddr *addr)
{
    if (!str || !addr) return 0;

    const char *p = str;
    for (int i = 0; i < 6; i++) {
        unsigned int byte = 0;
        /* 跳过可能的分隔符 (首次不需要跳) */
        if (i > 0) {
            if (*p != ':') return 0;
            p++;
        }
        /* 解析两位十六进制数 */
        for (int j = 0; j < 2; j++) {
            byte <<= 4;
            if (*p >= '0' && *p <= '9')
                byte |= (*p - '0');
            else if (*p >= 'a' && *p <= 'f')
                byte |= (*p - 'a' + 10);
            else if (*p >= 'A' && *p <= 'F')
                byte |= (*p - 'A' + 10);
            else
                return 0;
            p++;
        }
        addr->addr[i] = (uint8_t)byte;
    }
    return 1;
}

/**
 * @brief  将蓝牙地址格式化为冒号分隔的大写十六进制字符串
 * @param  addr  地址结构体
 * @param  str   目标字符串缓冲区 (至少 18 字节)
 * @note   输出格式: "12:34:56:78:9A:BC"
 *         用于构造 AT+BIND 等指令参数
 */
static void addr_to_str(const BTAddr *addr, char *str)
{
    if (!addr || !str) return;

    char *p = str;
    for (int i = 0; i < 6; i++) {
        if (i > 0) *p++ = ':';
        uint8_t nibble_h = (addr->addr[i] >> 4) & 0x0F;
        uint8_t nibble_l = addr->addr[i] & 0x0F;
        *p++ = (nibble_h < 10) ? ('0' + nibble_h) : ('A' + nibble_h - 10);
        *p++ = (nibble_l < 10) ? ('0' + nibble_l) : ('A' + nibble_l - 10);
    }
    *p = '\0';
}

/*========================== USART1 中断处理 ===================*/

/**
 * @brief  USART1 接收中断服务函数
 * @note   当 HC-05 有数据发送到 MCU 时触发
 *         将数据存入环形缓冲区, 并调用已注册的回调函数
 *         中断优先级: 抢占 2, 子优先级 0
 *         
 *         注意: 此中断函数名由启动文件 (startup_stm32f10x_md.s)
 *         中的中断向量表定义, 不可更改
 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(HC05_USART, USART_IT_RXNE) != RESET) {
        /* 读取接收到的字节 */
        uint8_t d = (uint8_t)USART_ReceiveData(HC05_USART);

        /* 计算下一个头指针位置 (环形缓冲区) */
        uint16_t next = (uint16_t)((rx_head + 1) % RX_BUF_SIZE);

        /* 缓冲区未满则存入, 否则丢弃 (溢出保护) */
        if (next != rx_tail) {
            rx_buf[rx_head] = d;
            rx_head = next;
        }

        /* 如果注册了回调, 则调用回调通知上层 (单字节回调) */
        if (recv_cb)
            recv_cb(&d, 1);

        /* 清除中断标志 */
        USART_ClearITPendingBit(HC05_USART, USART_IT_RXNE);
    }
}

/*========================== 简易延时 =========================*/

/**
 * @brief  忙等待延时 (近似)
 * @param  ms  延时毫秒数
 * @note   基于空循环的近似延时, 适用于 @72MHz 系统时钟
 *         不适用于精确定时场景
 *         精度约 ±1ms @72MHz (O0 优化)
 */
static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms * 12000; i++)
        __NOP();
}
