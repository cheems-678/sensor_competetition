/**
 * @file    esp8266.h
 * @brief   ESP8266 WiFi 模块驱动 (UART AT 指令)
 * @note    芯片: ESP8266, 接口: USART3 (PB10 TX, PB11 RX)
 *          默认波特率: 115200 (部分模块为 9600, 通过 AT+UART_DEF 切换)
 *          支持: Station/AP 模式, TCP/UDP 客户端
 *
 *          【本工程适配】智能环境检测系统原理图:
 *          ESP8266 使用 USART3 (PB10=TX, PB11=RX), RST=PA4, CH_PD/EN=PA5
 */
#ifndef __ESP8266_H
#define __ESP8266_H

#include <stm32f10x.h>
#include <stdint.h>

/*========================== 引脚配置 =========================*/
#define ESP_USART           USART3
#define ESP_USART_BAUD      115200
#define ESP_TX_PORT         GPIOB
#define ESP_TX_PIN          GPIO_Pin_10         /* PB10 = USART3_TX */
#define ESP_RX_PORT         GPIOB
#define ESP_RX_PIN          GPIO_Pin_11         /* PB11 = USART3_RX */
#define ESP_RST_PORT        GPIOA
#define ESP_RST_PIN         GPIO_Pin_4          /* PA4 = RST (可选) */
#define ESP_CH_PD_PORT      GPIOA
#define ESP_CH_PD_PIN       GPIO_Pin_5          /* PA5 = CH_PD/EN (可选) */

/*========================== WiFi 模式 ========================*/
#define ESP_MODE_STATION    1
#define ESP_MODE_AP         2
#define ESP_MODE_BOTH       3

/*========================== 传输协议 ========================*/
#define ESP_TCP             0
#define ESP_UDP             1
#define ESP_SSL             2

/*========================== 返回状态 =========================*/
typedef enum {
    ESP_OK = 0,
    ESP_ERR_TIMEOUT,
    ESP_ERR_NO_RESPONSE,
    ESP_ERR_CONNECT,
    ESP_ERR_DISCONNECT,
    ESP_ERR_SEND,
    ESP_ERR_WIFI,
} ESPStatus;

/*========================== 数据结构 =========================*/
typedef struct {
    char     ssid[32];               /* 连接的 WiFi SSID */
    int8_t   rssi;                   /* 信号强度 dBm */
    uint8_t  ip[4];                  /* 本机 IP */
} ESPWiFiInfo;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 USART3 并检测模块 */
ESPStatus   ESP_Init(void);

/* 基础 AT 指令 */
ESPStatus   ESP_AT(void);
ESPStatus   ESP_Reset(void);
ESPStatus   ESP_GetVersion(char *buf, uint8_t max_len);

/* WiFi 配置 */
ESPStatus   ESP_SetMode(uint8_t mode);
ESPStatus   ESP_JoinAP(const char *ssid, const char *pwd);
ESPStatus   ESP_QuitAP(void);
ESPStatus   ESP_GetIP(char *ip, uint8_t max_len);

/* TCP/UDP 客户端 */
ESPStatus   ESP_Connect(uint8_t type, const char *host, uint16_t port);
ESPStatus   ESP_Send(const uint8_t *data, uint16_t len);
ESPStatus   ESP_Close(void);

/* 高级工具 */
ESPStatus   ESP_HTTP_GET(const char *url, char *resp, uint16_t max_len);
int16_t     ESP_RecvData(uint8_t *buf, uint16_t max_len);
void        ESP_SetRecvCallback(void (*cb)(const uint8_t *data, uint16_t len));

#ifdef __cplusplus
}
#endif

#endif
