/**
 * 粮仓声学层析成像系统 - 全局配置
 * MCU: STM32F103C8T6 (Cortex-M3, 72MHz, 64KB Flash, 20KB RAM)
 *
 * 引脚分配:
 *   PA0  - ADC1_IN0   -> 接收信号放大输出 (OPA2356 OUT)
 *   PA1  - ADC1_IN1   -> 备用接收通道
 *   PA2  - USART2_TX  -> 调试串口
 *   PA3  - USART2_RX  -> 调试串口
 *   PA4  - DAC_OUT1   -> 激励信号输出 (压电驱动)
 *   PA5  - DAC_OUT2   -> 备用激励
 *   PA6  - TIM3_CH1   -> 换能器阵列选择 S0 (74HC138 A)
 *   PA7  - TIM3_CH2   -> 换能器阵列选择 S1 (74HC138 B)
 *   PB0  - GPIO       -> 换能器阵列选择 S2 (74HC138 C)
 *   PB1  - GPIO       -> 发射/接收切换 (TX/RX relay)
 *   PB2  - GPIO       -> LED状态指示
 *   PB10 - USART3_TX  -> TFT显示屏 (SPI软件模拟时不用)
 *   PB11 - USART3_RX
 *   PB12 - GPIO       -> TFT CS
 *   PB13 - GPIO       -> TFT SCK  (软件SPI)
 *   PB14 - GPIO       -> TFT MISO
 *   PB15 - GPIO       -> TFT MOSI
 *   PA8  - GPIO       -> TFT DC/RS
 *   PA9  - USART1_TX  -> 上位机通信
 *   PA10 - USART1_RX
 *   PA11 - GPIO       -> TFT RST
 *   PA12 - GPIO       -> TFT BL
 *   PC13 - GPIO       -> 按键 START (启动扫描)
 *   PC14 - GPIO       -> 按键 MODE  (切换模式)
 *   PC15 - GPIO       -> 按键 SAVE  (保存数据)
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f1xx_hal.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// ============ 系统参数 ============
#define SYS_CLK_MHZ         72
#define ADC_SAMPLE_RATE_HZ  1000000    // ADC采样率 1MHz
#define ADC_BUFFER_SIZE     1024       // 单次采样缓冲区

// ============ 声学参数 ============
#define NUM_TRANSDUCERS     8          // 换能器数量
#define NUM_RAYS            28         // 射线路径数 (C(8,2)=28)
#define GRID_SIZE           16         // 重建网格 16x16
#define GRID_PIXELS         (GRID_SIZE * GRID_SIZE)  // 256
#define PIXEL_SIZE_MM       12.5       // 每像素对应12.5mm (200mm/16)
#define SOUND_AIR_MS        0.343      // 空气声速 m/ms (20°C)
#define SOUND_GRAIN_MS      0.30       // 小麦中近似声速 m/ms
#define TX_FREQ_HZ          40000      // 激励频率 40kHz
#define TX_BURST_COUNT      5          // 激励脉冲数(5个周期)
#define TX_AMPLITUDE       3000       // DAC输出幅度 (12位DAC, 0-4095)
#define AMP_GAIN_DB         60         // 接收放大增益

// ============ ART重建参数 ============
#define ART_ITERATIONS     50         // ART迭代次数
#define ART_RELAXATION     0.25       // 松弛因子
#define ART_MIN_VALUE      0.0       // 重建值下限
#define ART_MAX_VALUE      2000.0    // 重建值上限 (m/s)

// ============ 引脚定义 ============
// ADC
#define ADC_RX_PIN         GPIO_PIN_0
#define ADC_RX_PORT        GPIOA
#define ADC_RX_CHANNEL     ADC_CHANNEL_0

// DAC
#define DAC_TX_PIN         GPIO_PIN_4
#define DAC_TX_PORT        GPIOA

// 74HC138 译码器选择 (8路换能器切换)
#define MUX_A_PIN          GPIO_PIN_6
#define MUX_A_PORT        GPIOA
#define MUX_B_PIN          GPIO_PIN_7
#define MUX_B_PORT        GPIOA
#define MUX_C_PIN          GPIO_PIN_0
#define MUX_C_PORT        GPIOB

// 收发切换继电器
#define TX_RX_SEL_PIN      GPIO_PIN_1
#define TX_RX_SEL_PORT    GPIOB

// LED
#define LED_PIN            GPIO_PIN_2
#define LED_PORT          GPIOB

// TFT (软件SPI)
#define TFT_CS_PIN        GPIO_PIN_12
#define TFT_CS_PORT       GPIOB
#define TFT_SCK_PIN       GPIO_PIN_13
#define TFT_SCK_PORT      GPIOB
#define TFT_MISO_PIN      GPIO_PIN_14
#define TFT_MISO_PORT     GPIOB
#define TFT_MOSI_PIN      GPIO_PIN_15
#define TFT_MOSI_PORT     GPIOB
#define TFT_DC_PIN        GPIO_PIN_8
#define TFT_DC_PORT       GPIOA
#define TFT_RST_PIN       GPIO_PIN_11
#define TFT_RST_PORT      GPIOA
#define TFT_BL_PIN        GPIO_PIN_12
#define TFT_BL_PORT       GPIOA

// 按键
#define BTN_START_PIN     GPIO_PIN_13
#define BTN_START_PORT   GPIOC
#define BTN_MODE_PIN      GPIO_PIN_14
#define BTN_MODE_PORT     GPIOC
#define BTN_SAVE_PIN      GPIO_PIN_15
#define BTN_SAVE_PORT     GPIOC

// ============ 工作模式 ============
typedef enum {
    MODE_TOMOGRAPHY = 0,    // 层析成像模式
    MODE_ACOUSTIC_EMISSION, // 声发射监听模式(虫害)
    MODE_LEVEL,              // 料位测量模式
    MODE_COUNT
} WorkMode;

// ============ 异常类型 ============
typedef enum {
    ANOMALY_NONE = 0,
    ANOMALY_VOID,           // 空洞
    ANOMALY_WET,             // 受潮
    ANOMALY_HOT,             // 发热
    ANOMALY_COMPACTED,       // 压实
    ANOMALY_INSECT,          // 虫害
    ANOMALY_COUNT
} AnomalyType;

// ============ 测量结果 ============
typedef struct {
    float velocityMap[GRID_PIXELS];  // 声速分布图
    float rayData[NUM_RAYS];         // 每条射线的穿越时间(ms)
    float rayAmplitude[NUM_RAYS];    // 每条射线的信号衰减(dB)
    AnomalyType anomalyMap[GRID_PIXELS]; // 异常分类图
    AnomalyType primaryAnomaly;      // 主要异常类型
    float confidence;                // 置信度
    uint32_t scanTime_ms;           // 扫描耗时
} ScanResult;

// ============ 采样缓冲区 ============
typedef struct {
    uint16_t raw[ADC_BUFFER_SIZE];   // ADC原始数据
    float timeOfFlight_ms;          // 穿越时间
    float amplitude_dB;             // 信号幅度
    bool valid;                      // 数据有效性
} RayMeasurement;

#endif // __CONFIG_H
