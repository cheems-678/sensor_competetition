/**
 * @file    bh1750.h
 * @brief   GY-302 / BH1750FVI 环境光强度传感器驱动 (I2C)
 * @note    BH1750FVI 是 ROHM 生产的数字环境光传感器:
 *          - 16 位 ADC, 直接输出 lux 值
 *          - 测量范围: 1 ~ 65535 lux
 *          - 分辨率: 1 lux (H 模式) / 0.5 lux (H2 模式)
 *          - I2C 接口, 7 位地址 0x23 (ADDR=L) 或 0x5C (ADDR=H)
 *          - 内置光电二极管 + ADC, 无需外部校准
 *          
 *          模块 GY-302 引脚:
 *          VCC → 3.3V~5V
 *          GND → GND
 *          SCL → PB3 (软件 I2C)
 *          SDA → PB4 (软件 I2C)
 *          ADDR → 悬空/LOW (地址 0x23)
 *          
 *          【本工程适配】智能安防系统原理图:
 *          BH1750 使用 PB3=SCL, PB4=SDA 软件模拟 I2C。
 *          原库使用硬件 I2C1 (PB6/PB7), 而 PB3/PB4 不是 I2C1 引脚,
 *          故副本中仅将底层 I2C 传输改为软件模拟, 上层 API 不变。
 *          PB3/PB4 是 JTAG 复用引脚, 初始化时自动禁用 JTAG。
 */
#ifndef __BH1750_H
#define __BH1750_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                      I2C 引脚配置 (软件模拟)                      *
*==================================================================*/
#define BH1750_SCL_PORT     GPIOB          /* SCL 端口              */
#define BH1750_SCL_PIN      GPIO_Pin_10    /* PB10 = SCL            */
#define BH1750_SDA_PORT     GPIOB          /* SDA 端口              */
#define BH1750_SDA_PIN      GPIO_Pin_11    /* PB11 = SDA            */

/*==================================================================*
 *                      I2C 设备地址                                 *
 *==================================================================*/
#define BH1750_ADDR         0x23           /* 7 位地址 (ADDR 悬空/LOW) */
#define BH1750_ADDR_ALT     0x5C           /* 备选地址 (ADDR=HIGH)    */

/* I2C 总线地址（含读写位） */
#define BH1750_WRITE        ((uint8_t)(BH1750_ADDR << 1 | 0))   /* 写地址 */
#define BH1750_READ         ((uint8_t)(BH1750_ADDR << 1 | 1))   /* 读地址 */

/*==================================================================*
 *                      BH1750 命令定义                              *
 *==================================================================*/
#define BH1750_CMD_PWR_DOWN     0x00    /* 掉电模式                     */
#define BH1750_CMD_PWR_ON       0x01    /* 上电 (等待指令)              */
#define BH1750_CMD_RESET        0x07    /* 复位 (需先 PWR_ON)           */

/* 连续测量模式 */
#define BH1750_CMD_CONT_H       0x10    /* 连续高分辨率 (1lux, 120ms)   */
#define BH1750_CMD_CONT_H2      0x11    /* 连续高分辨率2 (0.5lux, 120ms)*/
#define BH1750_CMD_CONT_L       0x13    /* 连续低分辨率 (4lux, 16ms)    */

/* 单次测量模式 (测量后自动掉电) */
#define BH1750_CMD_ONCE_H       0x20    /* 单次高分辨率 (1lux, 120ms)   */
#define BH1750_CMD_ONCE_H2      0x21    /* 单次高分辨率2 (0.5lux, 120ms)*/
#define BH1750_CMD_ONCE_L       0x23    /* 单次低分辨率 (4lux, 16ms)    */

/*==================================================================*
 *                      测量延时 (毫秒,参考值)                       *
 *==================================================================*/
#define BH1750_DELAY_H          180     /* 高分辨率模式测量延时          */
#define BH1750_DELAY_H2         180     /* 高分辨率2模式测量延时         */
#define BH1750_DELAY_L          30      /* 低分辨率模式测量延时          */

/*==================================================================*
 *                         API 返回状态码                            *
 *==================================================================*/
typedef enum {
    BH1750_OK = 0,          /* 操作成功                     */
    BH1750_ERR_I2C,         /* I2C 通信错误 (无应答/超时)   */
    BH1750_ERR_TIMEOUT,     /* 测量超时                     */
} BH1750Status;

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================*
 *                         API 函数声明                              *
 *==================================================================*/

/**
 * @brief   初始化 BH1750 传感器
 * @note    完成以下工作:
 *          1. 初始化软件 I2C (PB3 SCL, PB4 SDA)
 *          2. 发送上电命令 (PWR_ON)
 *          3. 设置默认模式为连续高分辨率 (CONT_H)
 * @return  BH1750_OK       初始化成功
 *          BH1750_ERR_I2C  I2C 通信失败 (检查接线/地址)
 */
BH1750Status BH1750_Init(void);

/**
 * @brief   设置测量模式
 * @param   mode  测量模式命令, 可选:
 *                - BH1750_CMD_CONT_H   连续高分辨率 (1lux, 推荐)
 *                - BH1750_CMD_CONT_H2  连续高分辨率2 (0.5lux)
 *                - BH1750_CMD_CONT_L   连续低分辨率 (4lux, 快速)
 *                - BH1750_CMD_ONCE_H   单次高分辨率 (省电)
 *                - BH1750_CMD_ONCE_H2  单次高分辨率2
 *                - BH1750_CMD_ONCE_L   单次低分辨率
 * @return  BH1750_OK       设置成功
 *          BH1750_ERR_I2C  I2C 通信失败
 * @note    连续模式: 传感器持续测量, 可直接读取最新值。
 *          单次模式: 测量一次后自动掉电, 每次读值前需重新触发,
 *                   适合电池供电的低功耗场景。
 */
BH1750Status BH1750_SetMode(uint8_t mode);

/**
 * @brief   读取环境光强度
 * @return  光照度值 (lux), 范围 0~65535
 * @note    返回值已根据公式转换为 lux:
 *          - CONT_H / ONCE_H 模式: lux = raw / 1.2
 *          - CONT_H2 / ONCE_H2 模式: lux = raw / 2.4
 *          - CONT_L / ONCE_L 模式: lux = raw * 3.334 / 1.2
 *          
 *          实际测量范围: 1 ~ 65535 lux
 *          - 0: 极暗 (或传感器未就绪)
 *          - 1~500: 室内 (靠窗较亮)
 *          - 500~5000: 阴天/室内强光
 *          - 5000~50000: 晴天室内/室外阴影
 *          - 50000~65535: 阳光直射
 *          
 *          测量时间: 高分辨率模式约 120ms, 低分辨率约 16ms。
 *          连续模式下无需等待, 直接读取最新结果。
 *          单次模式下需在 SetMode 后等待延时再读取。
 */
uint16_t BH1750_ReadLight(void);

/**
 * @brief   发送掉电命令
 * @note    传感器进入低功耗模式, I2C 总线仍可被其他设备使用。
 *          再次读取前需调用 BH1750_PowerOn() 唤醒。
 * @return  BH1750_OK       成功
 *          BH1750_ERR_I2C  I2C 通信失败
 */
BH1750Status BH1750_PowerDown(void);

/**
 * @brief   发送上电命令
 * @note    从掉电模式唤醒, 恢复测量。
 *          上电后传感器进入等待指令状态, 需调用 
 *          BH1750_SetMode() 开始测量。
 * @return  BH1750_OK       成功
 *          BH1750_ERR_I2C  I2C 通信失败
 */
BH1750Status BH1750_PowerOn(void);

/**
 * @brief   复位传感器 (需先上电)
 * @note    复位寄存器到默认值。
 *          必须先调用 BH1750_PowerOn() 才能复位。
 *          复位后需重新设置测量模式。
 * @return  BH1750_OK       成功
 *          BH1750_ERR_I2C  I2C 通信失败
 */
BH1750Status BH1750_Reset(void);

#ifdef __cplusplus
}
#endif

#endif


