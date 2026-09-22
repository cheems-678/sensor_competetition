/**
 * @file    as608.h
 * @brief   AS608 光学指纹传感器驱动头文件 (UART 接口)
 * @note    AS608 是杭州晟元 (Synochip) 推出的指纹识别芯片:
 *          - 内嵌 DSP 加速器, 支持指纹采集/特征提取/比对/搜索
 *          - UART 通讯, 默认波特率 57600 (可选 9600~115200)
 *          - 模块内置指纹库 FLASH, 可存储 120~300 枚指纹
 *          - 支持 1:1 比对 (Verify) 和 1:N 搜索 (Search)
 *          
 *          模块接线:
 *          VCC → 3.3V
 *          GND → GND
 *          TX  → PA3 (USART2_RX, 接模块的 TX)
 *          RX  → PA2 (USART2_TX, 接模块的 RX)
 *          TOUCH → PA1, GPIO 输入 (手指触碰检测, 低电平有效)
 *          
 *          【本工程适配】智能安防系统原理图:
 *          AS608 使用 USART2 (PA2=TX, PA3=RX, 57600)
 *          PA1 = TOUCH/WKUP 手指触摸检测输入 (可选)
 *
 *          通信协议:
 *          命令包:  EF 01 FF FF FF FF 01 <Len_H> <Len_L> <Cmd> [Params] <CS_H> <CS_L>
 *          应答包:  EF 01 FF FF FF FF 07 <Len_H> <Len_L> <Conf> [Data] <CS_H> <CS_L>
 *          
 *          本驱动使用轮询方式 (Polling), 不占用中断。
 *          如需中断方式, 用户可自行添加 USART2_IRQHandler。
 *          
 *          注意:
 *          - 默认密码为 0x00000000, Init 函数会自动验证
 *          - 注册时需要将同一手指按压两次 (采集两张图像)
 *          - 对于 RGB 灯模块, 可通过 SetLed 控制 (需模块支持)
 */
#ifndef __AS608_H
#define __AS608_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                      UART 引脚配置 (USART2)                        *
 *==================================================================*/
#define AS608_USART             USART2
#define AS608_TX_PORT           GPIOA
#define AS608_TX_PIN            GPIO_Pin_2      /* PA2 = USART2_TX */
#define AS608_RX_PORT           GPIOA
#define AS608_RX_PIN            GPIO_Pin_3      /* PA3 = USART2_RX */

/* 手指触摸检测引脚 (TOUCH/WKUP, 低电平有效, 可选) */
#define AS608_TOUCH_PORT        GPIOA
#define AS608_TOUCH_PIN         GPIO_Pin_1      /* PA1 = TOUCH */

/*==================================================================*
 *                      通讯参数                                      *
 *==================================================================*/
#define AS608_BAUDRATE          57600       /* 默认波特率              */
#define AS608_RX_TIMEOUT        500000      /* 接收超时 (轮询次数)     */
#define AS608_RETRY_COUNT       150         /* 等待手指重试次数        */
#define AS608_RETRY_DELAY       10          /* 重试间隔 (ms)           */

/*==================================================================*
 *                      默认地址和密码                                *
 *==================================================================*/
#define AS608_DEFAULT_ADDR      0xFFFFFFFF  /* 默认模块地址            */
#define AS608_DEFAULT_PASSWORD  0x00000000  /* 默认密码 (部分模块需)    */

/*==================================================================*
 *                      指令码                                        *
 *==================================================================*/
#define AS608_CMD_GET_IMAGE     0x01        /* 采集指纹图像            */
#define AS608_CMD_IMG2TZ        0x02        /* 图像生成特征            */
#define AS608_CMD_MATCH         0x03        /* 比对两个特征            */
#define AS608_CMD_SEARCH        0x04        /* 搜索指纹库              */
#define AS608_CMD_REGMODEL      0x05        /* 合并生成模板            */
#define AS608_CMD_STORE         0x06        /* 存储模板                */
#define AS608_CMD_LOAD_CHAR     0x07        /* 读取特征缓冲区          */
#define AS608_CMD_UP_CHAR       0x08        /* 上传特征                */
#define AS608_CMD_DOWN_CHAR     0x09        /* 下载特征                */
#define AS608_CMD_UP_IMAGE      0x0A        /* 上传图像                */
#define AS608_CMD_DOWN_IMAGE    0x0B        /* 下载图像                */
#define AS608_CMD_DELETE        0x0C        /* 删除指纹                */
#define AS608_CMD_EMPTY         0x0D        /* 清空指纹库              */
#define AS608_CMD_SET_SYS_PARA  0x0E        /* 设置系统参数            */
#define AS608_CMD_VALID_PWD     0x0F        /* 验证密码                */
#define AS608_CMD_SET_PWD       0x12        /* 设置密码                */
#define AS608_CMD_GET_RAND_CODE 0x14        /* 获取随机数              */
#define AS608_CMD_GET_SYS_PARA  0x15        /* 读取系统参数            */
#define AS608_CMD_SET_ADDR      0x15        /* 设置模块地址 (部分)     */

/*==================================================================*
 *                      应答确认码                                    *
 *==================================================================*/
#define AS608_CONF_OK           0x00        /* 成功                    */
#define AS608_CONF_RECV_ERR     0x01        /* 接收包错误              */
#define AS608_CONF_NO_FINGER    0x02        /* 无手指                  */
#define AS608_CONF_ENROLL_FAIL  0x03        /* 录入失败                */
#define AS608_CONF_OVER_DISORDER 0x04       /* 图像太乱                */
#define AS608_CONF_OVER_UPLOAD  0x05        /* 特征点太少              */
#define AS608_CONF_NO_MATCH     0x06        /* 比对不匹配              */
#define AS608_CONF_NOT_FOUND    0x07        /* 未搜索到                */
#define AS608_CONF_ENROLL_MISMATCH 0x08     /* 两次图像不匹配          */
#define AS608_CONF_BAD_IMAGE    0x09        /* 图像质量差              */
#define AS608_CONF_MERGE_FAIL   0x0A        /* 特征合并失败            */
#define AS608_CONF_ADDR_EXIST   0x0B        /* 地址已存在              */
#define AS608_CONF_INVALID_PWD  0x0C        /* 密码错误                */
#define AS608_CONF_TEMPLATE_EMPTY 0x0D      /* 模板库为空              */
#define AS608_CONF_NO_SPACE     0x0F        /* 无存储空间              */
#define AS608_CONF_TEMPLATE_OVERFLOW 0x10   /* 模板溢出                */
#define AS608_CONF_COM_ERR      0x11        /* 通讯错误                */
#define AS608_CONF_INVALID_BAUD 0x12        /* 波特率无效              */
#define AS608_CONF_INVALID_PARAM 0x1A       /* 参数无效                */

/*==================================================================*
 *                      系统参数数据结构                              *
 *==================================================================*/
typedef struct {
    uint16_t status;        /* 状态寄存器                            */
    uint16_t capacity;      /* 指纹库容量                            */
    uint16_t security;      /* 安全等级 (1~5)                       */
    uint32_t addr;          /* 模块地址                              */
    uint16_t baud;          /* 波特率 (代码值, 非实际频率)           */
    uint16_t packet_len;    /* 数据包大小                            */
} AS608_SysPara;

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================*
 *                         API 函数声明                              *
 *==================================================================*/

/**
 * @brief   初始化 AS608 指纹模块
 * @note    执行流程:
 *          1. 初始化 USART2 (PA2 TX, PA3 RX, 57600 波特)
 *          2. 验证密码 (默认 0x00000000)
 *          3. 读取系统参数确认模块状态
 * @return  0: 成功
 *          非 0: 失败 (AS608_CONF_xxx 错误码)
 */
uint8_t AS608_Init(void);

/**
 * @brief   检测手指是否触碰传感器 (读取 TOUCH/WKUP 引脚)
 * @return  1: 有手指触碰
 *          0: 无手指
 * @note    本工程 PA1 接 AS608 的 TOUCH/WKUP 引脚 (低电平有效)。
 *          部分模块此引脚未引出或电平极性不同, 若不接则恒返回 0。
 */
uint8_t AS608_IsTouched(void);

/**
 * @brief   验证模块密码
 * @param   password  32 位密码 (默认 0x00000000)
 * @return  0: 密码正确, 非 0: 密码错误
 */
uint8_t AS608_VerifyPassword(uint32_t password);

/**
 * @brief   设置新密码
 * @param   password  新密码 (32 位)
 * @return  0: 成功, 非 0: 失败
 */
uint8_t AS608_SetPassword(uint32_t password);

/**
 * @brief   读取系统参数
 * @param   para  输出参数, 存放系统参数结构体
 * @return  0: 成功, 非 0: 失败
 */
uint8_t AS608_GetSysPara(AS608_SysPara *para);

/**
 * @brief   获取指纹库已存储的指纹数量
 * @return  已存储的指纹数
 * @note    通过读取系统参数中的状态寄存器获取。
 *          读取失败返回 0xFFFF。
 */
uint16_t AS608_GetStoredCount(void);

/**
 * @brief   采集指纹图像
 * @return  AS608_CONF_OK (0x00):      采集成功
 *          AS608_CONF_NO_FINGER (0x02): 无手指
 *          其他: 错误码
 * @note    检测到手指并采集图像。
 *          返回 AS608_CONF_NO_FINGER 时, 应用层可间隔
 *          一定时间后重试。
 */
uint8_t AS608_GetImage(void);

/**
 * @brief   将图像生成特征存入指定缓冲区
 * @param   buffer_id  缓冲区号 (1 或 2)
 * @return  0: 成功, 非 0: 失败
 * @note    AS608 内部有两个特征缓冲区 (CharBuffer1 和 CharBuffer2)。
 *          注册时需要依次将两次采集的特征分别存入缓冲区 1 和 2。
 *          搜索时需要将采集的特征存入缓冲区 1。
 */
uint8_t AS608_Img2Tz(uint8_t buffer_id);

/**
 * @brief   合并两个特征生成模板 (存入 CharBuffer1+2)
 * @return  0: 成功, 非 0: 失败
 * @note    用于注册流程第三步:
 *          采集图像1 → Img2Tz(1) → 采集图像2 → Img2Tz(2) → RegModel
 */
uint8_t AS608_RegModel(void);

/**
 * @brief   将模板存入指纹库
 * @param   id  指纹 ID 号 (1 ~ capacity)
 * @return  0: 成功, 非 0: 失败
 * @note    存储前需确保 ID 未被占用。
 *          若返回 AS608_CONF_NO_SPACE (0x0F), 表示指纹库已满。
 */
uint8_t AS608_Store(uint16_t id);

/**
 * @brief   搜索指纹库 (1:N 识别)
 * @param   page_id  输出参数, 匹配到的指纹 ID
 * @param   score    输出参数, 匹配分数
 * @return  0: 找到匹配, 非 0: 未找到 (AS608_CONF_NOT_FOUND)
 * @note    需要先调用 GetImage + Img2Tz(1) 采集并生成特征。
 *          搜索范围为整个指纹库 (0 ~ capacity-1)。
 */
uint8_t AS608_Search(uint16_t *page_id, uint16_t *score);

/**
 * @brief   注册新指纹 (完整流程)
 * @param   id  要存储的指纹 ID
 * @return  0: 注册成功
 *          AS608_CONF_NO_FINGER: 未检测到手指
 *          AS608_CONF_BAD_IMAGE: 图像质量差
 *          AS608_CONF_NO_SPACE:  指纹库已满
 *          其他: 注册失败
 * @note    注册流程 (函数内部自动完成):
 *          1. 采集指纹图像1 → Img2Tz(1)
 *          2. 抬起手指, 等待重新按压同一手指
 *          3. 采集指纹图像2 → Img2Tz(2)
 *          4. RegModel → 合并生成模板
 *          5. Store(id) → 存入指纹库
 *          
 *          每次采集前会重试 AS608_RETRY_COUNT 次
 *          (约 1.5 秒), 超时返回 AS608_CONF_NO_FINGER。
 *          
 *          调用示例:
 *          res = AS608_Enroll(1);
 *          if (res == 0) { }  // 注册成功
 */
uint8_t AS608_Enroll(uint16_t id);

/**
 * @brief   识别指纹 (完整流程)
 * @param   page_id  输出参数, 匹配到的指纹 ID
 * @param   score    输出参数, 匹配分数
 * @return  0: 识别成功
 *          AS608_CONF_NO_FINGER: 未检测到手指
 *          AS608_CONF_NOT_FOUND: 指纹库中无匹配
 *          其他: 识别失败
 * @note    识别流程:
 *          1. 采集指纹图像 (重试直到检测到手指)
 *          2. Img2Tz(1) → 生成特征
 *          3. Search → 搜索指纹库
 *          
 *          分数 score 越高, 匹配越可靠。
 *          典型阈值: score > 50 可视为有效匹配。
 */
uint8_t AS608_Identify(uint16_t *page_id, uint16_t *score);

/**
 * @brief   删除指定 ID 的指纹
 * @param   id  要删除的指纹 ID (1 ~ capacity)
 * @return  0: 成功, 非 0: 失败
 */
uint8_t AS608_Delete(uint16_t id);

/**
 * @brief   批量删除指纹
 * @param   start  起始 ID
 * @param   count  删除数量
 * @return  0: 成功, 非 0: 失败
 */
uint8_t AS608_DeleteMulti(uint16_t start, uint16_t count);

/**
 * @brief   清空整个指纹库
 * @return  0: 成功, 非 0: 失败
 * @note    清空后所有指纹被删除, 不可恢复。
 *          执行后 StoredCount = 0。
 */
uint8_t AS608_Empty(void);

/**
 * @brief   设置安全等级
 * @param   level  安全等级 (1~5)
 *                 1: 最低安全, 匹配最快, 误识率高
 *                 5: 最高安全, 匹配最慢, 误识率低
 * @return  0: 成功, 非 0: 失败
 * @note    默认等级通常为 3。
 *          等级越高, 比对时要求的匹配分数越高。
 */
uint8_t AS608_SetSecurityLevel(uint8_t level);

/**
 * @brief   直接发送命令并接收响应 (底层接口)
 * @param   cmd       指令码
 * @param   params    参数字节数组 (可为 0)
 * @param   param_len 参数长度
 * @param   conf      输出参数, 存放应答确认码
 * @param   resp_data 输出参数, 存放应答中的附加数据 (可为 0)
 * @param   resp_len  输出参数, 附加数据长度
 * @return  0: 通讯成功, 非 0: 通讯失败
 * @note    高级用户可用此接口发送任意指令。
 */
uint8_t AS608_SendCmd(uint8_t cmd, const uint8_t *params,
                      uint8_t param_len, uint8_t *conf,
                      uint8_t *resp_data, uint8_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif
