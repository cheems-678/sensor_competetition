/**
 * WH-L101-L-P-H10 LoRa UART透传驱动 (从板/传感器节点) - SPL版本
 *
 * 接线 (从板原理图):
 *   STM32 PA2 (USART2_TX) -> LoRa Pin20 (UART_RX)
 *   STM32 PA3 (USART2_RX) <- LoRa Pin19 (UART_TX)
 *   STM32 PA4             -> LoRa Pin21 (Reload)   外部R8 4.7k上拉到3V3
 *   STM32 PA1             -> LoRa Pin23 (WAKE)
 *   STM32 PA0             <- LoRa Pin24 (HOST_WAKE)
 *   3V3 -> VCC(Pin13/14/17), GND -> GND
 *
 * 数据协议 (与主板一致):
 *   帧头: 0xAA 0x55
 *   类型: 1字节
 *   序号: 1字节 (分片序号, 从0开始)
 *   总片: 1字节
 *   长度: 1字节 (本帧数据长度, 最大128)
 *   数据: N字节
 *   校验: 1字节 (type^seq^total^len^data... 的XOR)
 *
 * 从板上报的包类型:
 *   PKT_NODE_ENV   0x10  仓内温湿度 + 气压
 *   PKT_NODE_SOUND 0x11  虫害声特征(双通道)
 *   PKT_NODE_AUDIO 0x12  声音包络(64点, 0~255)
 *
 * 主板下行: 0xC5 + 命令字节 (见 config.h LORA_CMD_NODE_*)
 */

#ifndef __LORA_H
#define __LORA_H

#include "../../config.h"
#include "modules/dht11/dht11.h"
#include "modules/bme280/bme280.h"
#include "modules/sph0645/sph0645.h"

/* 帧头 */
#define LORA_HDR0   0xAA
#define LORA_HDR1   0x55

/* 初始化USART2 (115200, 8N1) + 控制引脚 */
void    LoRa_Init(void);

/* 发送一个数据帧 (自动加帧头+校验) */
void    LoRa_SendPacket(uint8_t type, uint8_t seq, uint8_t total,
                        uint8_t* data, uint8_t len);

/* 发送原始字节 (透传) */
void    LoRa_SendRaw(uint8_t* data, uint16_t len);

/* ---- 从板上报 ---- */

/* 上报仓内温湿度+气压 (PKT_NODE_ENV) */
void    LoRa_SendNodeEnv(const TH_Data* th, const BME280_Data* bme, uint8_t bme_ok);

/* 上报虫害声特征 (PKT_NODE_SOUND) */
void    LoRa_SendNodeSound(const MicFrame* f);

/* 上报声音包络 (PKT_NODE_AUDIO, 64点) */
void    LoRa_SendNodeAudio(const MicFrame* f);

/* ---- 控制引脚 ---- */
void    LoRa_Reload(void);      /* 拉低RELOAD约1s: 让模块重载/恢复配置(按需使用) */
void    LoRa_WakeUp(void);      /* WAKE置高(正常工作电平) */
void    LoRa_Sleep(void);       /* WAKE置低(仅当模块开启了休眠功能才有意义) */

/* 取一条主板下行命令 (0xC5前缀+命令字节), 无命令返回0 */
uint8_t LoRa_GetCmd(void);

#endif /* __LORA_H */
