/**
 * WH-L101-L-P-H10 LoRa UART透传驱动 - SPL版本
 *
 * 接线:
 *   STM32 PA2 (USART2_TX) → LoRa Pin20 (RX)
 *   STM32 PA3 (USART2_RX) → LoRa Pin19 (TX)
 *   3V3 → VCC, GND → GND
 *
 * 数据协议:
 *   帧头: 0xAA 0x55
 *   类型: 1字节 (0x01=声速图, 0x02=异常图, 0x03=波形, 0x04=料位, 0x05=摘要)
 *   序号: 1字节 (分片序号, 从0开始)
 *   总片: 1字节
 *   长度: 1字节 (本帧数据长度)
 *   数据: N字节
 *   校验: 1字节 (XOR)
 *
 * WH-L101 TX缓存240字节, 每帧最多发128字节数据
 */

#ifndef __LORA_H
#define __LORA_H

#include "../../config.h"

/* 数据包类型 */
#define PKT_VMAP    0x01    /* 声速图(256字节, 分2帧, 每帧128字节) */
#define PKT_AMAP    0x02    /* 异常图(256字节, 分2帧) */
#define PKT_WAVE    0x03    /* 声发射波形 */
#define PKT_LEVEL   0x04    /* 料位 */
#define PKT_SUMMARY 0x05    /* 摘要(主异常+置信度+耗时) */
#define PKT_AE      0x06    /* 虫害监听报告(能量+频率+判定) */

/* 初始化USART2 (115200, 8N1) 透传模式, 仅TX/RX, 无控制引脚 */
void    LoRa_Init(void);

/* 发送一个数据帧 (自动加帧头+校验) */
void    LoRa_SendPacket(uint8_t type, uint8_t seq, uint8_t total,
                         uint8_t* data, uint8_t len);

/* 发送声速图 (自动分2帧, 声速float→uint8量化) */
void    LoRa_SendVMap(float* vmap, float vmin, float vmax);

/* 发送异常图 (自动分2帧) */
void    LoRa_SendAMap(uint8_t* amap, uint8_t mainAnom,
                      float conf, uint32_t elapsed_ms);

/* 发送摘要 */
void    LoRa_SendSummary(uint8_t mainAnom, float conf, uint32_t elapsed_ms);

/* 发送料位 */
void    LoRa_SendLevel(float level_mm, float siloH_mm);

/* 发送声发射波形 (512个12bit采样, 每点2字节大端, 共1024字节分8帧) */
void    LoRa_SendWave(uint16_t* samples, uint16_t len);

/* 发送原始字节 (透传) */
void    LoRa_SendRaw(uint8_t* data, uint16_t len);

/* 发送环境数据 (光照lux + 4路风扇转速% + 灯带开关) */
void    LoRa_SendEnv(uint16_t lux, const uint8_t* fan_pct, uint8_t led_on);

/* 取一条下行控制命令 (0xC5前缀+命令字节), 无命令返回0 */
uint8_t LoRa_GetCmd(void);

/* 解析一帧从板发来的数据帧(0xAA 0x55 ... XOR)
   返回1=取到完整且校验正确的一帧, 0=暂无; data缓冲至少 LORA_PKT_MAX 字节 */
uint8_t LoRa_PollFrame(uint8_t* type, uint8_t* data, uint8_t* len);

/* 给从板下发命令 (0xC5 + 命令字节, 见 config.h LORA_CMD_NODE_*) */
void    LoRa_SendCtrl(uint8_t cmd);

#endif /* __LORA_H */
