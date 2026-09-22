/**
 * @file    bh1750.c
 * @brief   GY-302 / BH1750FVI 环境光强度传感器驱动实现
 * @note    BH1750 通过 I2C 接口与 STM32 通信。本工程副本使用
 *          软件模拟 I2C (PB3=SCL, PB4=SDA), 原因: 原理图要求
 *          BH1750 接 PB3/PB4, 而这两个引脚不是硬件 I2C1 的引脚。
 *          (原库为硬件 I2C1, PB6=SCL, PB7=SDA。)
 *
 *          PB3/PB4 为 JTAG 复用引脚 (JTDO/NJTRST), 初始化时会
 *          自动禁用 JTAG (保留 SWD), 以将其复用为普通 GPIO。
 *
 *          PB10/PB11 为普通 GPIO, 无需禁用 JTAG。
 *
 *          BH1750 通信协议:
 *          主机 → 从机: START + ADDR_W + COMMAND + STOP
 *          主机 ← 从机: START + ADDR_R + [DATA_H] + [DATA_L] + STOP
 *
 *          光照度计算:
 *          高分辨率模式 (CONT_H / ONCE_H):
 *          lux = raw_value / 1.2
 *          例: raw=1200 → lux=1000
 *
 *          典型工作流程:
 *          1. BH1750_Init()      → 上电 + 默认连续高分辨率模式
 *          2. 等待 180ms         → 首次测量完成
 *          3. BH1750_ReadLight() → 读取光照度
 *          4. 重复步骤 3         → 连续模式自动更新
 */
#include "bh1750.h"

/* I2C 超时计数 (轮询等待次数) */
#define I2C_TIMEOUT         50000

/* 当前测量模式 (用于 ReadLight 时计算正确的 lux 值) */
static uint8_t current_mode;

/*==================================================================*
 *                      内部函数声明                                  *
 *==================================================================*/
static void        i2c_delay(void);
static void        i2c_scl_high(void);
static void        i2c_scl_low(void);
static void        i2c_sda_high(void);
static void        i2c_sda_low(void);
static uint8_t     i2c_sda_read(void);
static void        i2c_start(void);
static void        i2c_stop(void);
static void        i2c_ack(uint8_t ack);
static uint8_t     i2c_wait_ack(void);
static void        i2c_write_byte(uint8_t dat);
static uint8_t     i2c_read_byte(void);
static void        i2c_init(void);
static BH1750Status i2c_write_cmd(uint8_t cmd);
static BH1750Status i2c_read_raw(uint16_t *raw);

/*==================================================================*
 *                      模块初始化                                   *
 *==================================================================*/

/**
 * @brief   初始化 BH1750 传感器
 * @note    执行流程:
 *          1. 初始化软件 I2C (GPIO + 位操作)
 *          2. 发送 Power On 命令 (唤醒传感器)
 *          3. 设置默认模式: 连续高分辨率 (1lux, 120ms)
 *          4. 保存当前模式为 CONT_H
 * @return  BH1750_OK       初始化成功
 *          BH1750_ERR_I2C  I2C 通信失败
 */
BH1750Status BH1750_Init(void)
{
    BH1750Status st;

    /* 第一步: 初始化软件 I2C */
    i2c_init();

    /* 第二步: 发送上电命令, 唤醒传感器 */
    /* BH1750 上电后默认处于 Power Down 状态 */
    st = i2c_write_cmd(BH1750_CMD_PWR_ON);
    if (st != BH1750_OK) return st;

    /* 第三步: 设置默认测量模式为连续高分辨率 */
    current_mode = BH1750_CMD_CONT_H;
    st = i2c_write_cmd(BH1750_CMD_CONT_H);
    if (st != BH1750_OK) return st;

    return BH1750_OK;
}

/*==================================================================*
 *                      模式设置                                     *
 *==================================================================*/

/**
 * @brief   设置 BH1750 测量模式
 * @param   mode  测量模式命令
 * @return  BH1750_OK       设置成功
 *          BH1750_ERR_I2C  I2C 通信失败
 * @note    模式切换后, 传感器需要一定时间完成首次测量:
 *          高分辨率: ~120ms (建议等待 180ms)
 *          低分辨率: ~16ms (建议等待 30ms)
 *          单次模式测量完成后自动进入 Power Down 状态,
 *          下次读值前要重新 SetMode。
 */
BH1750Status BH1750_SetMode(uint8_t mode)
{
    BH1750Status st;

    /* 保存当前模式, 供 ReadLight 计算 lux 时使用 */
    current_mode = mode;

    /* 发送模式命令 */
    st = i2c_write_cmd(mode);

    return st;
}

/*==================================================================*
 *                      读取光照度                                   *
 *==================================================================*/

/**
 * @brief   读取环境光强度值
 * @return  光照度 (lux), 范围 0~65535
 * @note    从 BH1750 读取 2 字节原始 ADC 值, 根据当前模式
 *          转换为 lux:
 *          
 *          CONT_H / ONCE_H:   lux = raw / 1.2
 *          CONT_H2 / ONCE_H2: lux = raw / 2.4
 *          CONT_L / ONCE_L:   lux = raw / 1.2 (低分辨率精度较低)
 *          
 *          所有模式都使用同一个 16 位寄存器, 区别在于分辨率。
 *          连续模式下, 传感器内部持续更新寄存器值,
 *          每次读取获取最新测量结果。
 */
uint16_t BH1750_ReadLight(void)
{
    uint16_t raw = 0;
    uint32_t lux = 0;

    /* 从 BH1750 读取 2 字节原始数据 */
    if (i2c_read_raw(&raw) != BH1750_OK) {
        return 0;  /* 读取失败, 返回 0 */
    }

    /*
     * 根据当前测量模式转换:
     * 
     * 高分辨率 (H-mode):
     *   分辨率 1 lux, 测量范围 1~65535 lux
     *   lux = raw / 1.2
     *   使用整数运算: lux = raw * 5 / 6
     * 
     * 高分辨率2 (H-mode2):
     *   分辨率 0.5 lux, 测量范围 0.5~32767.5 lux
     *   lux = raw / 2.4 = raw * 5 / 12
     * 
     * 低分辨率 (L-mode):
     *   分辨率 4 lux, 测量范围 4~65532 lux
     *   精度较低但速度快
     *   lux = raw / 1.2 (同上)
     */
    switch (current_mode) {
        case BH1750_CMD_CONT_H2:
        case BH1750_CMD_ONCE_H2:
            /* H-mode2: raw / 2.4 = raw * 5 / 12 */
            lux = ((uint32_t)raw * 5) / 12;
            break;

        case BH1750_CMD_CONT_H:
        case BH1750_CMD_ONCE_H:
        default:
            /* H-mode: raw / 1.2 = raw * 5 / 6 */
            lux = ((uint32_t)raw * 5) / 6;
            break;
    }

    /* 限幅到 16 位无符号范围 */
    if (lux > 65535) lux = 65535;

    return (uint16_t)lux;
}

/*==================================================================*
 *                      电源管理                                     *
 *==================================================================*/

/**
 * @brief   掉电模式
 * @note    BH1750 进入低功耗 (电流约 1μA)。
 *          I2C 总线可被其他设备正常使用。
 *          唤醒需调用 BH1750_PowerOn() + BH1750_SetMode()。
 */
BH1750Status BH1750_PowerDown(void)
{
    return i2c_write_cmd(BH1750_CMD_PWR_DOWN);
}

/**
 * @brief   上电唤醒
 * @note    从掉电模式唤醒传感器。
 *          唤醒后传感器进入等待命令状态, 不会自动测量,
 *          需调用 BH1750_SetMode() 开始测量。
 */
BH1750Status BH1750_PowerOn(void)
{
    return i2c_write_cmd(BH1750_CMD_PWR_ON);
}

/**
 * @brief   复位
 * @note    复位 BH1750 内部寄存器到出厂默认值。
 *          必须先 PowerOn 才能复位。
 *          复位后需重新设置测量模式。
 */
BH1750Status BH1750_Reset(void)
{
    return i2c_write_cmd(BH1750_CMD_RESET);
}

/*==================================================================*
 *                      I2C 底层初始化 (软件模拟)                    *
 *==================================================================*/

/**
 * @brief  初始化软件 I2C 引脚
 * @note   配置 PB3 (SCL) 和 PB4 (SDA):
 *          - 均为推挽输出 (开漏也可, 模块自带 4.7kΩ 上拉)
 *          - SCL/SDA 初始输出高电平 (空闲状态)
 *          - PB3/PB4 是 JTAG 引脚, 必须先禁用 JTAG
 *            (GPIO_Remap_SWJ_JTAGDisable 保留 SWD)
 */
static void i2c_init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 GPIOB 时钟 (PB10/PB11 无需禁用 JTAG) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = BH1750_SCL_PIN | BH1750_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* 空闲状态: SCL=1, SDA=1 */
    GPIO_SetBits(BH1750_SCL_PORT, BH1750_SCL_PIN);
    GPIO_SetBits(BH1750_SDA_PORT, BH1750_SDA_PIN);
}

/*==================================================================*
 *                    软件 I2C 位操作底层                            *
 *==================================================================*/

/**
 * @brief   I2C 时序延时
 * @note    约 5μs, 使软件 I2C 时钟约 100kHz, 与硬件 I2C1 标准模式相当
 */
static void i2c_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 100; i++);
}

static void i2c_scl_high(void)
{
    GPIO_SetBits(BH1750_SCL_PORT, BH1750_SCL_PIN);
}

static void i2c_scl_low(void)
{
    GPIO_ResetBits(BH1750_SCL_PORT, BH1750_SCL_PIN);
}

static void i2c_sda_high(void)
{
    GPIO_SetBits(BH1750_SDA_PORT, BH1750_SDA_PIN);
}

static void i2c_sda_low(void)
{
    GPIO_ResetBits(BH1750_SDA_PORT, BH1750_SDA_PIN);
}

static uint8_t i2c_sda_read(void)
{
    if (GPIO_ReadInputDataBit(BH1750_SDA_PORT, BH1750_SDA_PIN) != Bit_RESET)
        return 1;
    return 0;
}

/**
 * @brief   产生 START 条件: SCL=1 时 SDA 由 1→0
 */
static void i2c_start(void)
{
    i2c_sda_high();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_low();
    i2c_delay();
    i2c_scl_low();
}

/**
 * @brief   产生 STOP 条件: SCL=1 时 SDA 由 0→1
 */
static void i2c_stop(void)
{
    i2c_sda_low();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_high();
    i2c_delay();
}

/**
 * @brief   发送 ACK(ack=1) 或 NACK(ack=0)
 */
static void i2c_ack(uint8_t ack)
{
    if (ack)
        i2c_sda_low();          /* ACK: SDA=0 */
    else
        i2c_sda_high();         /* NACK: SDA=1 */
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_scl_low();
    i2c_sda_high();
}

/**
 * @brief   等待从机应答
 * @return  1: 收到 ACK    0: 无 ACK (从机无应答/超时)
 */
static uint8_t i2c_wait_ack(void)
{
    uint32_t tout = I2C_TIMEOUT;

    i2c_sda_high();             /* 释放 SDA, 由从机拉低表示 ACK */
    i2c_delay();
    i2c_scl_high();
    i2c_delay();

    while (i2c_sda_read()) {    /* SDA=1 表示从机未应答 */
        if (--tout == 0) {
            i2c_scl_low();
            return 0;
        }
    }
    i2c_scl_low();
    return 1;
}

/**
 * @brief   发送一个字节 (MSB 先发)
 */
static void i2c_write_byte(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8; i++) {
        if (dat & 0x80)
            i2c_sda_high();
        else
            i2c_sda_low();
        i2c_delay();
        i2c_scl_high();
        i2c_delay();
        i2c_scl_low();
        dat <<= 1;
    }
}

/**
 * @brief   接收一个字节 (MSB 先收)
 * @return  读到的 8 位数据
 * @note    读字节前 SDA 置为输入浮空, 读完后恢复输出
 */
static uint8_t i2c_read_byte(void)
{
    uint8_t i, dat = 0;

    for (i = 0; i < 8; i++) {
        dat <<= 1;
        i2c_scl_high();
        i2c_delay();
        if (i2c_sda_read())
            dat |= 0x01;
        i2c_scl_low();
        i2c_delay();
    }
    return dat;
}

/*==================================================================*
 *                    I2C 命令写入 (主机→从机)                       *
 *==================================================================*/

/**
 * @brief   向 BH1750 发送单字节命令
 * @param   cmd  命令字节
 * @return  BH1750_OK       写入成功
 *          BH1750_ERR_I2C  通信失败 (无应答/超时)
 * @note    I2C 写入时序:
 *          START → 发送地址(写) → 等待 ACK → 发送命令 → 
 *          等待 ACK → STOP
 *          
 *          BH1750 的 I2C 协议非常简单: 写入单个命令字节即可
 *          完成模式设置、电源管理等功能, 无需多字节写操作。
 */
static BH1750Status i2c_write_cmd(uint8_t cmd)
{
    i2c_start();

    /* 发送设备地址 + 写位, 等待 ACK */
    i2c_write_byte(BH1750_WRITE);
    if (!i2c_wait_ack()) {
        i2c_stop();
        return BH1750_ERR_I2C;
    }

    /* 发送命令字节, 等待 ACK */
    i2c_write_byte(cmd);
    if (!i2c_wait_ack()) {
        i2c_stop();
        return BH1750_ERR_I2C;
    }

    i2c_stop();
    return BH1750_OK;
}

/*==================================================================*
 *                    I2C 数据读取 (主机←从机)                       *
 *==================================================================*/

/**
 * @brief   从 BH1750 读取 2 字节原始 ADC 数据
 * @param   raw  输出参数, 存放 16 位原始值 (MSB first)
 * @return  BH1750_OK       读取成功
 *          BH1750_ERR_I2C  通信失败
 * @note    I2C 读取时序:
 *          START → 发送地址(读) → 等待 ACK → 
 *          读 MSB (主机发送 ACK) → 读 LSB (主机发送 NACK) → 
 *          STOP
 *          
 *          BH1750 在连续模式下, 主机可以直接发起读取操作,
 *          无需先发送命令。传感器自动返回最新测量值的 MSB 和 LSB。
 *          
 *          数据格式:
 *          MSB (高字节) + LSB (低字节) = 16 位无符号整数
 *          例: MSB=0x04, LSB=0xB0 → raw=0x04B0=1200
 */
static BH1750Status i2c_read_raw(uint16_t *raw)
{
    uint8_t msb, lsb;

    i2c_start();

    /* 发送设备地址 + 读位, 等待 ACK */
    i2c_write_byte(BH1750_READ);
    if (!i2c_wait_ack()) {
        i2c_stop();
        return BH1750_ERR_I2C;
    }

    /* 读 MSB, 主机回 ACK (还要继续接收 LSB) */
    msb = i2c_read_byte();
    i2c_ack(1);

    /* 读 LSB, 主机回 NACK (最后一个字节) */
    lsb = i2c_read_byte();
    i2c_ack(0);

    i2c_stop();

    /* 组合 MSB 和 LSB 为 16 位原始值 */
    *raw = ((uint16_t)msb << 8) | lsb;

    return BH1750_OK;
}
