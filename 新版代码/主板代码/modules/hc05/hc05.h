/**
 * @file    hc05.h
 * @brief   HC-05 蓝牙模块驱动 (UART AT 指令 + 数据透传)
 * @note    芯片: HC-05 (BC417 蓝牙 2.0 + EDR)
 *          接口: USART1 (PA9 TX, PA10 RX)
 *          默认波特率: 9600 (数据模式) / 38400 (部分 AT 模式)
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          HC-05 使用 USART1 (PA9=TX, PA10=RX)
 *          KEY = PB13 (PB0/PB1/PB2 被雨滴/土壤湿度传感器占用, 故改到 PB13)
 *          STATE 引脚未使用 (宏被注释, HC05_IsConnected() 恒返回 0)
 *
 *          工作模式:
 *          - 从机模式 (ROLE=0): 等待手机或其他主设备连接
 *          - 主机模式 (ROLE=1): 主动连接指定从设备
 *          
 *          进入 AT 指令模式 (二选一):
 *          方法 1: 上电前将 KEY 引脚拉高, 再上电/复位
 *          方法 2: 模块未配对/未连接时, KEY 引脚拉高后发 AT
 *          
 *          典型连接流程 (从机):
 *          1. HC05_Init()             初始化串口和 GPIO
 *          2. HC05_AT()               检测模块响应
 *          3. HC05_SetName("MyBT")    设置蓝牙名称
 *          4. HC05_SetPassword("1234")设置配对码
 *          5. HC05_SetRole(0)         设为从机模式
 *          6. HC05_EnterATMode()      可选的 AT 配置模式
 *          7. 手机搜索并连接          进入数据透传
 *          8. HC05_SendData() / HC05_RecvData() 数据传输
 */
#ifndef __HC05_H
#define __HC05_H

#include <stm32f10x.h>
#include <stdint.h>

/*========================== 引脚配置 =========================*/
/**
 * @def HC05_USART
 * @brief HC-05 使用的 USART 外设
 */
#define HC05_USART          USART1

/**
 * @def HC05_USART_BAUD
 * @brief 与 HC-05 通信的波特率 (默认 9600)
 * @note  HC-05 出厂默认 9600, 可通过 AT+UART 修改
 *        注意: AT 模式下部分模块需 38400
 */
#define HC05_USART_BAUD     9600

/* USART TX (MCU → HC-05 RXD) */
#define HC05_TX_PORT        GPIOA
#define HC05_TX_PIN         GPIO_Pin_9          /* PA9 = USART1_TX */

/* USART RX (MCU ← HC-05 TXD) */
#define HC05_RX_PORT        GPIOA
#define HC05_RX_PIN         GPIO_Pin_10         /* PA10 = USART1_RX */

/**
 * @def HC05_KEY_PORT / HC05_KEY_PIN
 * @brief AT 指令模式使能引脚 (KEY/EN)
 * @note  KEY=高电平时, 模块进入 AT 指令模式
 *        KEY=低电平时, 模块处于数据透传模式
 *        若不需要 AT 配置, 可将此引脚悬空或接地
 */
#define HC05_KEY_PORT       GPIOB
#define HC05_KEY_PIN        GPIO_Pin_13         /* PB13 = KEY/EN */

/**
 * @def HC05_STATE_PORT / HC05_STATE_PIN
 * @brief 蓝牙连接状态指示引脚 (STATE)
 * @note  STATE=高电平: 已配对/已连接
 *        STATE=低电平: 未连接/未配对
 *        本工程未使用此引脚, 注释掉宏即可让
 *        HC05_IsConnected() 恒返回 0。
 */
/* #define HC05_STATE_PORT     GPIOB
   #define HC05_STATE_PIN      GPIO_Pin_1 */

/*========================== 返回状态 =========================*/
/**
 * @enum  BTStatus
 * @brief HC-05 驱动函数返回状态码
 */
typedef enum {
    BT_OK               = 0,    /* 操作成功               */
    BT_ERR_TIMEOUT      = 1,    /* 等待模块响应超时       */
    BT_ERR_NO_RESPONSE  = 2,    /* 模块无任何应答         */
    BT_ERR_AT_MODE      = 3,    /* 不在 AT 指令模式       */
    BT_ERR_CONNECT      = 4,    /* 连接失败               */
    BT_ERR_DISCONNECT   = 5,    /* 断开连接失败           */
    BT_ERR_SEND         = 6,    /* 数据发送失败           */
    BT_ERR_PARAM        = 7,    /* 参数错误               */
    BT_ERR_STATE        = 8,    /* 状态异常               */
} BTStatus;

/*========================== 模式/角色定义 ====================*/
#define BT_ROLE_SLAVE       0   /* 从机模式 (等待连接)     */
#define BT_ROLE_MASTER      1   /* 主机模式 (主动连接)     */

#define BT_CMODE_FIXED      0   /* 固定地址连接模式        */
#define BT_CMODE_ANY        1   /* 任意地址连接模式        */

/*========================== 数据结构 =========================*/
/**
 * @struct BTAddr
 * @brief  蓝牙 MAC 地址 (6 字节)
 */
typedef struct {
    uint8_t addr[6];            /* MAC 地址 (高位在前)     */
} BTAddr;

/**
 * @struct BTVersion
 * @brief  HC-05 固件版本信息
 */
typedef struct {
    uint8_t  major;             /* 主版本号                */
    uint8_t  minor;             /* 次版本号                */
    uint8_t  variant;           /* 变体/修订号             */
} BTVersion;

#ifdef __cplusplus
extern "C" {
#endif

/*========================== API 函数 =========================*/

/**
 * @brief   初始化 HC-05 模块
 * @note    完成以下操作:
 *          - 初始化 USART3 (PB10 TX, PB11 RX, 9600 8N1)
 *          - 初始化 KEY 引脚 (推挽输出, 默认低电平进入数据模式)
 *          - 初始化 STATE 引脚 (浮空输入, 读取连接状态)
 *          - 使能 USART 接收中断 (RXNE)
 *          - 清空接收缓冲区
 * @return  BT_OK  初始化成功
 */
BTStatus HC05_Init(void);

/**
 * @brief   进入 AT 指令模式
 * @note    将 KEY 引脚拉高, 然后拉低 RST 再释放 (模拟上电),
 *          等待 500ms 让模块以 AT 模式启动
 *          进入 AT 模式后波特率可能变为 38400 (部分模块),
 *          若通信失败可尝试先切换波特率
 * @return  BT_OK           成功进入 AT 模式
 *          BT_ERR_TIMEOUT  模块无响应
 */
BTStatus HC05_EnterATMode(void);

/**
 * @brief   退出 AT 模式, 进入数据透传模式
 * @note    将 KEY 引脚拉低, 模块自动回到数据模式
 *          若在 AT 模式下修改了波特率, 需同步更新串口
 * @return  BT_OK  退出成功
 */
BTStatus HC05_ExitATMode(void);

/**
 * @brief   测试 AT 通信 (发送 "AT\r\n")
 * @note    用于检测模块是否在线、是否处于 AT 模式
 *          正常响应: "OK"
 * @return  BT_OK              模块正常应答
 *          BT_ERR_NO_RESPONSE  模块无应答 (可能不在 AT 模式)
 */
BTStatus HC05_AT(void);

/**
 * @brief   软复位模块 (AT+RESET)
 * @note    复位后模块重新启动, 若 KEY=高电平则进入 AT 模式
 *          等待 2s 让模块重启完成
 * @return  BT_OK  复位成功
 */
BTStatus HC05_Reset(void);

/**
 * @brief   恢复出厂设置 (AT+ORGL)
 * @note    恢复默认: 名称=HC-05, 密码=1234, 波特率=9600,
 *          角色=从机, CMODE=任意地址
 *          恢复后模块会自动复位
 * @return  BT_OK  恢复成功
 */
BTStatus HC05_RestoreDefault(void);

/**
 * @brief   设置蓝牙名称 (AT+NAME=<name>)
 * @param   name  蓝牙广播名称 (最长 31 字节, 不含特殊字符)
 * @return  BT_OK  设置成功
 * @note    名称修改后需复位或重新上电才能生效 (部分固件即时生效)
 */
BTStatus HC05_SetName(const char *name);

/**
 * @brief   设置配对密码 (AT+PSWD=<pwd>)
 * @param   pwd  4 位数字密码字符串, 如 "1234"
 * @return  BT_OK  设置成功
 */
BTStatus HC05_SetPassword(const char *pwd);

/**
 * @brief   设置串口参数 (AT+UART=<baud>,<stopbits>,<parity>)
 * @param   baud       波特率 (支持 4800/9600/19200/38400/57600/115200 等)
 * @param   stopbits   停止位 (0=1bit, 1=2bits)
 * @param   parity     校验位 (0=None, 1=Odd, 2=Even)
 * @return  BT_OK  设置成功
 * @note    此设置会改变模块的通信波特率, 调用后必须同步
 *          调用 HC05_SetBaudRate() 更新 MCU 端串口波特率
 */
BTStatus HC05_SetUART(uint32_t baud, uint8_t stopbits, uint8_t parity);

/**
 * @brief   更新 MCU 串口波特率 (不与模块通信)
 * @param   baud  新的波特率值 (需与模块 AT+UART 设置一致)
 * @note    在调用 HC05_SetUART() 修改模块波特率后,
 *          调用此函数使 MCU 串口与模块保持一致
 */
void HC05_SetBaudRate(uint32_t baud);

/**
 * @brief   设置角色 (AT+ROLE=<role>)
 * @param   role  BT_ROLE_SLAVE(0) = 从机 / BT_ROLE_MASTER(1) = 主机
 * @return  BT_OK  设置成功
 */
BTStatus HC05_SetRole(uint8_t role);

/**
 * @brief   设置连接模式 (AT+CMODE=<mode>)
 * @param   mode  BT_CMODE_FIXED(0) = 固定地址 / BT_CMODE_ANY(1) = 任意
 * @return  BT_OK  设置成功
 * @note    任意模式: 允许任何设备连接 (从机推荐)
 *          固定模式: 只允许 BIND 绑定的设备连接 (安全性更高)
 */
BTStatus HC05_SetCMode(uint8_t mode);

/**
 * @brief   绑定指定蓝牙地址 (AT+BIND=<addr>)
 * @param   addr  目标设备 MAC 地址 (6 字节)
 * @return  BT_OK  绑定成功
 * @note    仅在 CMODE=0 (固定地址) 时有效
 *          地址格式: 如 12:34:56:78:9A:BC
 */
BTStatus HC05_Bind(BTAddr *addr);

/**
 * @brief   获取本机蓝牙地址 (AT+ADDR?)
 * @param   addr  存放 MAC 地址的结构体指针
 * @return  BT_OK          获取成功
 *          BT_ERR_PARAM   参数错误
 * @note    响应示例: "+ADDR:12:34:56:78:9a:bc"
 */
BTStatus HC05_GetAddr(BTAddr *addr);

/**
 * @brief   获取模块状态 (AT+STATE?)
 * @param   state  存放状态字符串的缓冲区
 * @param   max_len 缓冲区最大长度
 * @return  BT_OK  获取成功
 * @note    常见状态:
 *          - INITIALIZING: 初始化中
 *          - READY: 就绪 (AT 模式)
 *          - PAIRABLE: 可配对
 *          - PAIRED: 已配对
 *          - INQUIRING: 查询中 (主机)
 *          - CONNECTING: 连接中
 *          - CONNECTED: 已连接
 */
BTStatus HC05_GetState(char *state, uint8_t max_len);

/**
 * @brief   获取固件版本 (AT+VERSION?)
 * @param   ver  存放版本信息的结构体指针
 * @return  BT_OK  获取成功
 */
BTStatus HC05_GetVersion(BTVersion *ver);

/**
 * @brief   查询已连接设备的地址 (AT+RNAME? 等)
 * @param   addr  存放远端设备地址的结构体指针
 * @return  BT_OK          获取成功
 *          BT_ERR_STATE   未连接任何设备
 * @note    仅在已连接状态下有效
 */
BTStatus HC05_GetRemoteAddr(BTAddr *addr);

/**
 * @brief   发送数据 (数据透传模式)
 * @param   data  数据缓冲区
 * @param   len   数据长度
 * @return  BT_OK       发送成功
 *          BT_ERR_SEND 发送失败
 * @note    仅在数据透传模式下使用 (非 AT 模式)
 *          在 AT 模式下此函数会直接发送原始数据到串口
 */
BTStatus HC05_SendData(const uint8_t *data, uint16_t len);

/**
 * @brief   接收数据 (数据透传模式)
 * @param   buf     存放接收到的数据
 * @param   max_len 缓冲区最大长度
 * @return  >0  实际接收到的字节数
 *          0   缓冲区无数据
 * @note    从环形缓冲区读取数据, 非阻塞
 */
uint16_t HC05_RecvData(uint8_t *buf, uint16_t max_len);

/**
 * @brief   检查蓝牙连接状态
 * @return  0  未连接
 *          1  已连接
 * @note    通过读取 STATE 引脚电平判断
 *          若未定义 STATE 引脚, 则始终返回 0
 */
uint8_t HC05_IsConnected(void);

/**
 * @brief   注册接收回调函数
 * @param   cb  回调函数指针, 在 USART 中断中调用
 *              参数: (data 指针, 数据长度)
 * @note    注册后, 每收到一帧数据会立即通知上层
 *          若不需要回调, 传 NULL 取消注册
 */
void HC05_SetRecvCallback(void (*cb)(const uint8_t *data, uint16_t len));

/**
 * @brief   设置模块配对码模式 (AT+TYPE=<mode>) — 部分固件
 * @param   mode  配对模式 (0=固定密码, 1=绑定)
 * @return  BT_OK  设置成功
 * @note    非所有固件支持此指令, 若返回 ERROR 属正常
 */
BTStatus HC05_SetPairMode(uint8_t mode);

/**
 * @brief   设置查询码 (AT+IAC=<iac>) — 主机模式
 * @param   iac  查询码 (如 9e8b33 为通用查询)
 * @return  BT_OK  设置成功
 * @note    仅在主机模式下有效, 用于发现周围蓝牙设备
 */
BTStatus HC05_SetInquiryCode(uint32_t iac);

/**
 * @brief   开始查询周围设备 (AT+INQ) — 主机模式
 * @param   devices     存放查询结果的缓冲区
 * @param   max_num     最大设备数量
 * @param   timeout_ms  查询超时 (ms)
 * @return  >=0  发现的设备数量
 *          BT_ERR_TIMEOUT  查询超时
 * @note    仅在主机模式下有效
 *          查询期间模块无法响应其他 AT 指令
 */
int16_t HC05_Inquiry(BTAddr *devices, uint8_t max_num, uint32_t timeout_ms);

/**
 * @brief   取消查询 (AT+INQC)
 * @return  BT_OK  取消成功
 */
BTStatus HC05_CancelInquiry(void);

#ifdef __cplusplus
}
#endif

#endif
