/**
 * @file    dht11.h
 * @brief   单总线温湿度传感器驱动 (DHT11 / AM2302-DHT22)
 *
 * 硬件: 3线温湿度电缆/模块 —— VCC 3V3 / DATA / GND, DATA 需 4.7k 上拉到 3V3。
 *   从板: DATA = PA6 (外部 R11 4.7k)  → 仓内温湿度
 *   主板: DATA = PA5                  → 仓外温湿度
 * 引脚与器件类型在各自 config.h 中用 TH_DATA_PORT/TH_DATA_PIN/TH_SENSOR_TYPE 配置。
 *
 * 两种器件协议同为 40bit 帧:
 *   | 湿度高 | 湿度低 | 温度高 | 温度低 | 校验和 |
 *   校验和 = 前4字节之和的低8位
 *   DHT11 : 湿度/温度高字节是整数, 低字节恒为0 (分辨率 1)
 *   AM2302: 湿度/温度为16位, 分辨率 0.1, 温度高字节 bit7=符号
 * 本驱动按上述差异自动识别器件(也可用 TH_SENSOR_TYPE 固定死)。
 */
#ifndef __DHT11_H
#define __DHT11_H

#include <stm32f10x.h>
#include <stdint.h>
#include "../../dwt_us.h"   /* DWT 周期计数器访问宏(不依赖 CMSIS 是否导出 DWT) */

/* 本驱动只使用 config.h 中的宏: TH_DATA_PORT / TH_DATA_PIN / TH_SENSOR_TYPE */

/** @brief 一次温湿度读数 */
typedef struct {
    float   temp_c;    /* 温度 (℃)   */
    float   hum_pct;   /* 湿度 (%RH)  */
    uint8_t type;      /* 0=DHT11, 1=AM2302(DHT22) */
    uint8_t valid;     /* 1=本次读数有效 */
} DHT_Data;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化数据引脚(输入上拉)并初始化微秒时间基
 * @note   上电后器件需要约1s稳定时间, 建议 Init 后延时1s再读。
 */
void    DHT_Init(void);

/**
 * @brief  读取一次温湿度
 * @param  d  输出, 不允许为 NULL
 * @return 0=成功(校验通过)  1=失败(无应答/超时/校验错/数值越界)
 * @note   两次调用间隔应 ≥1s (器件刷新周期约2s, 太快会读到旧值甚至失败)
 */
uint8_t DHT_Read(DHT_Data *d);

/** @brief 获取上次识别到的器件类型: 0=DHT11 1=AM2302 0xFF=还没识别成功 */
uint8_t DHT_GetType(void);

/** @brief 器件类型名称, 用于上报/打印 */
const char* DHT_TypeName(uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H */
