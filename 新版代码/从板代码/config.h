/**
 * 粮仓声学层析系统 - 【从板 / 传感器节点】全局配置
 *
 * STM32F103C8T6 (中容量, 64KB Flash, 20KB RAM)
 *
 * 从板职责:
 *   1. 单总线温湿度电缆 → 仓内温度/湿度        (PA6, 外部4.7k上拉)
 *   2. BME280           → 仓内气压(附温湿度)   (PB6/PB7 软件I2C)
 *   3. SPH0645LM4H ×2   → 粮堆内虫子活动声     (I2S2: PB12/PB13/PB15)
 *   4. LoRa WH-L101     → 把以上数据上报主板   (USART2: PA2/PA3)
 *
 * ------------------------------------------------------------------
 * 引脚分配 (按从板原理图, 2026-09-21 确认)
 * ------------------------------------------------------------------
 *   PA0  - 输入        LoRa HOST_WAKE (模块→MCU, 当前不用)
 *   PA1  - 输出        LoRa WAKE      (MCU→模块, 常态高电平)
 *   PA2  - USART2_TX   → LoRa Pin20 (UART_RX)
 *   PA3  - USART2_RX   ← LoRa Pin19 (UART_TX)
 *   PA4  - 输出        LoRa RELOAD    (外部R8 4.7k上拉到3V3, 常态高)
 *   PA6  - 单总线     温湿度电缆 DATA (外部R11 4.7k上拉到3V3)
 *   PB2  - 输出       LED 状态指示
 *   PB6  - 软I2C SCL  BME280(外接气压端子P2/P3)
 *   PB7  - 软I2C SDA  BME280
 *   PB12 - I2S2_WS    SPH0645 LRCLK(H3/H4 第4脚)
 *   PB13 - I2S2_CK    SPH0645 BCLK (H3/H4 第3脚)
 *   PB15 - I2S2_SD    SPH0645 DOUT (H3/H4 第5脚)
 *   PA13/PA14         SWDIO/SWCLK 调试
 *
 * 未接线(空闲): PA5 PA7 PA8 PA9 PA10 PA11 PA12 PA15
 *               PB0 PB1 PB3 PB4 PB5 PB8 PB9 PB10 PB11 PB14
 *               PC13 PC14 PC15
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ======= 系统参数 ======= */
#define SYS_CLK             72000000UL
#define delay_ms(x)         SysTick_Delay_Ms(x)
#define delay_us(x)         SysTick_Delay_Us(x)

/* ======= 单总线温湿度电缆 (仓内温湿度) =======
 * 3线电缆: VCC 3V3 / DATA / GND, DATA 外部 4.7k 上拉
 * 兼容 DHT11 与 AM2302(DHT22) 两种器件, 自动识别(见 dht11.c)
 */
#define TH_DATA_PORT        GPIOA
#define TH_DATA_PIN         GPIO_Pin_6
#define TH_TYPE_DHT11       0
#define TH_TYPE_AM2302      1
#define TH_TYPE_AUTO        2
#define TH_SENSOR_TYPE      TH_TYPE_AUTO   /* 0=DHT11 1=AM2302 2=自动识别 */
#define TH_READ_PERIOD_MS   2000           /* 采集周期(器件刷新周期≥2s) */

/* ======= BME280 气压 (外接气压端子 P2: PB6/PB7) =======
 * 引脚宏定义在 modules/bme280/bme280.h 里 (PB6=SCL, PB7=SDA, 地址0x76),
 * 这里只留说明, 避免两处重复定义。 */

/* ======= SPH0645LM4H 双麦克风 (I2S2 + DMA1_CH4) =======
 * H3: SEL=GND  → 左声道 ; H4: SEL=3V3 → 右声道
 * 两颗麦克风并联在同一组 BCLK/WS 上, 数据分左右声道回来。
 */
#define MIC_SAMPLE_RATE     16000          /* 采样率(Hz), BCLK=512kHz */
#define MIC_FRAME_SAMPLES   1600           /* 每帧每声道采样点数(100ms) */
#define MIC_BUF_SAMPLES     (MIC_FRAME_SAMPLES * 2)   /* 左右交织后的总点数 */
#define MIC_ENV_POINTS      64             /* 上报给主板的包络点数 */

/* 虫害判定阈值 (RMS 能量, 按 SPH0645 16bit 量程标定, 现场可调) */
#define MIC_RMS_QUIET       60             /* 低于此值=安静 */
#define MIC_RMS_SUSPECT     150            /* 超过=可疑 */
#define MIC_RMS_PEST        320            /* 超过=虫害 */
#define MIC_FREQ_MIN        1000           /* 虫害声频段下限(Hz) */
#define MIC_FREQ_MAX        12000          /* 虫害声频段上限(Hz) */
#define MIC_CAPTURE_PERIOD_MS 5000         /* 声音采集/上报周期 */

/* ======= 蜂鸣器 (从板若无此器件, 置0即可) ======= */
#define SLAVE_HAS_BUZZER    0              /* 1=从板有源蜂鸣器(PB12不能用! 与I2S冲突) */

/* ======= LED状态指示 (PB2) ======= */
#define LED_PORT            GPIOB
#define LED_PIN             GPIO_Pin_2

/* ======= LoRa (WH-L101-L-P-H10, USART2 透传) ======= */
#define LORA_USART          USART2
#define LORA_BAUD           115200
#define LORA_PKT_MAX        128

/* 控制引脚 (从板原理图: PA4=RELOAD, PA1=WAKE, PA0=HOST_WAKE) */
#define LORA_RELOAD_PORT    GPIOA
#define LORA_RELOAD_PIN     GPIO_Pin_4     /* 输出, 外部4.7k上拉, 常态高 */
#define LORA_WAKE_PORT      GPIOA
#define LORA_WAKE_PIN       GPIO_Pin_1     /* 输出, 常态高 */
#define LORA_HWAK_PORT      GPIOA
#define LORA_HWAK_PIN       GPIO_Pin_0     /* 输入(模块→MCU), 当前不用 */

/* LoRa 下行控制命令 (主板→从板, 前缀0xC5 + 命令字节) */
#define LORA_CMD_PREFIX     0xC5
#define LORA_CMD_NODE_REPORT 0xB1    /* 立即上报一次(温湿度+气压+声音) */
#define LORA_CMD_NODE_FAST   0xB2    /* 切换为快速上报(1s) */
#define LORA_CMD_NODE_NORMAL 0xB3    /* 恢复常规上报(2s) */
#define LORA_CMD_NODE_AUDIO  0xB4    /* 立刻采集并上报一帧声音包络 */
#define LORA_CMD_NODE_BEEP   0xB5    /* (预留)节点鸣叫 */

/* ======= 工作模式 ======= */
enum {
    RPT_NORMAL = 0,   /* 环境2s / 声音5s */
    RPT_FAST,         /* 环境1s / 声音2s */
    RPT_NUM
};

/* ======= LoRa数据包类型 (与主板 lora.h 保持一致) ======= */
#define PKT_VMAP    0x01
#define PKT_AMAP    0x02
#define PKT_WAVE    0x03
#define PKT_LEVEL   0x04
#define PKT_SUMMARY 0x05
#define PKT_AE      0x06
#define PKT_ENV     0x07
#define PKT_CTRL    0x09
#define PKT_NODE_ENV   0x10   /* 从板→主板: 仓内温湿度+气压 */
#define PKT_NODE_SOUND 0x11   /* 从板→主板: 虫害声特征(双通道RMS/峰值/主频) */
#define PKT_NODE_AUDIO 0x12   /* 从板→主板: 声音包络(64点, 0~255) */

/* ======= 从板上报数据结构 ======= */

/** 单总线温湿度电缆读数 */
typedef struct {
    float   temp_c;     /* 温度 ℃ */
    float   hum_pct;    /* 湿度 %RH */
    uint8_t valid;      /* 1=本次读取成功 */
    uint8_t type;       /* 0=DHT11, 1=AM2302 */
} TH_Data;

/** 声音分析结果 (单通道) */
typedef struct {
    uint16_t rms;       /* 均方根能量 (16bit量程) */
    uint16_t peak;      /* 峰值幅度 */
    uint16_t freq_hz;   /* 过零率估算主频 (Hz) */
    uint16_t events;    /* 超过门限的脉冲个数(虫鸣/啃食次数) */
} MicCh;

/** 双通道声音帧 */
typedef struct {
    MicCh    ch[2];              /* ch[0]=左(H3) ch[1]=右(H4) */
    uint8_t  level;              /* 0=正常 1=可疑 2=虫害 */
    uint8_t  env[MIC_ENV_POINTS];/* (左+右)/2 的幅度包络 0~255 */
} MicFrame;

/* ======= SysTick 延时 / LED (实现在 modules/systick/systick.c) ======= */
void     SysTick_Init(void);
void     SysTick_Delay_Ms(uint32_t ms);
void     SysTick_Delay_Us(uint32_t us);
uint32_t SysTick_Ms(void);
uint32_t SysTick_UsNow(void);      /* 微秒时刻(DWT周期计数换算) */
uint32_t SysTick_RawCycles(void);  /* 原始CPU周期计数(见 dwt_us.h) */
uint8_t  SysTick_DwtOk(void);      /* DWT周期计数器是否可用 */
void     LED_Set(uint8_t on);

#endif /* __CONFIG_H */
