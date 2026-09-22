/**
 * @file    bme280.h
 * @brief   BME280 温湿度/气压传感器驱动 (软件 I2C)
 * @note    BME280 是博世(Bosch)数字环境传感器, 集成:
 *          - 温度   测量范围 -40~85℃,   精度 ±0.5℃
 *          - 气压   测量范围 300~1100hPa, 精度 ±1hPa
 *          - 湿度   测量范围 0~100%RH,  精度 ±3%RH
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          BME280 使用软件模拟 I2C 挂在 PB8(SCL)/PB9(SDA)。
 *          原因: OLED 占用硬件 I2C1 (PB6/PB7), 而 BME280 需要
 *          接 PB8/PB9 (与 I2C1 重映射引脚相同), 无法同时使用
 *          硬件 I2C1 的两组引脚, 故改用软件 I2C。
 *          (原库为硬件 I2C, 已改写为软件模拟 I2C, 补偿算法不变)
 *
 *          模块接线:
 *          VCC → 3.3V, GND → GND
 *          SCL → PB8,  SDA → PB9
 *          CSB → VCC (拉高选择 I2C 模式)
 *          SDO → GND (地址 0x76) 或 VCC (地址 0x77)
 *
 *          注意: SCL/SDA 外部需接 4.7k 上拉电阻 (多数模块板上已带)。
 */
#ifndef __BME280_H
#define __BME280_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                  引脚配置宏 (用户可在此重定义)                    *
 *==================================================================*/
#define BME280_SCL_PORT     GPIOB
#define BME280_SCL_PIN      GPIO_Pin_8      /* PB8 = SCL */
#define BME280_SDA_PORT     GPIOB
#define BME280_SDA_PIN      GPIO_Pin_9      /* PB9 = SDA */

/*==================================================================*
 *                      I2C 设备地址                                 *
 *==================================================================*/
/* BME280 常见 7 位 I2C 从机地址 (不含读写位) */
#define BME280_I2C_ADDR     0x76u   /* SDO 接 GND */

/*==================================================================*
 *                       数据结构                                    *
 *==================================================================*/

/** @brief BME280 测量结果 */
typedef struct {
    float temp_c;       /* 温度 (℃)      */
    float press_hpa;    /* 气压 (hPa)    */
    float hum_pct;      /* 湿度 (%RH)    */
} BME280_Data;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   初始化 BME280
 * @return  0: 成功
 *          1: I2C 通信失败
 *          2: 芯片 ID 不匹配 (不是 BME280)
 * @note    初始化流程: 配置软件 I2C → 校验 ID → 软复位 →
 *          读取校准数据 → 配置为正常模式 (各 1 倍过采样)。
 */
uint8_t BME280_Init(void);

/**
 * @brief   读取温度/气压/湿度
 * @param   data  测量结果输出, 不允许为 NULL
 * @return  0: 成功  非 0: 失败
 */
uint8_t BME280_Read(BME280_Data *data);

#ifdef __cplusplus
}
#endif

#endif
