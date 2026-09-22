/**
 * 粮仓声学层析成像系统 - 全局配置 (Standard Peripheral Library)
 *
 * STM32F103C8T6 (中容量, 64KB Flash, 20KB RAM)
 *
 * 引脚分配 (实际接线, 2026-09 确认版):
 *   PA0  - ADC1_IN0    接收信号 (OPA2356 OUT)
 *   PA1  - TIM2_CH2    风扇1 PWM (DC4010 12V)
 *   PA2  - USART2_TX   LoRa (WH-L101)
 *   PA3  - USART2_RX   LoRa (WH-L101)
 *   PA4  - DAC_OUT1    压电驱动信号 (激励输出)
 *   PA5  - 单总线     舱外温湿度 DHT11 (DATA, 模块自带4.7k上拉)
 *   PA6  - GPIO        CD74HC4067 S0
 *   PA7  - GPIO        CD74HC4067 S1
 *   PA8  - GPIO        CD74HC4067 S2
 *   PA9  - 空闲        (USART1_TX, TFT屏/语音模块暂不使用)
 *   PA10 - 空闲        (USART1_RX, TFT屏/语音模块暂不使用)
 *   PA11 - GPIO(软PWM) 舵机 窗户4 (SG90)
 *   PA12 - GPIO(软PWM) 舵机 门 (SG90)
 *   PB0  - GPIO        CD74HC4067 S3 (接地, 8通道模式)
 *   PB1  - TIM3_CH4    风扇2 PWM (DC4010 12V)
 *   PB2  - GPIO        LED状态指示
 *   PB8  - TIM4_CH3    风扇3 PWM (DC4010 12V)
 *   PB9  - TIM4_CH4    风扇4 PWM (DC4010 12V)
 *   PB10 - GPIO(软I2C) BH1750 SCL
 *   PB11 - GPIO(软I2C) BH1750 SDA
 *   PB12 - GPIO        蜂鸣器 (有源)
 *   PB13 - GPIO        WS2812B 灯带
 *   PB14 - GPIO(软PWM) 舵机 窗户3 (SG90)
 *   PB15 - GPIO(软PWM) 舵机 窗户2 (SG90)
 *   PC13 - GPIO输入    按键 START
 *   PC14 - GPIO输入    按键 MODE
 *   PC15 - GPIO输入    按键 SAVE
 *   SWDIO/SWCLK        ST-Link调试
 *
 * 未使用模块 (代码保留但不编译): mq4/rain/soil_moisture/
 *   sound/oled/as608/hc_sr501/flame/esp8266/hc05/key_matrix/tft
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "stm32f10x.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ======= 系统参数 ======= */
#define SYS_CLK             72000000UL
#define ADC_SAMPLE_RATE     1000000UL     /* 1MHz */
#define ADC_BUF_SIZE        512
#define delay_ms(x)         SysTick_Delay_Ms(x)
#define delay_us(x)         SysTick_Delay_Us(x)

/* ======= 声学参数 ======= */
#define NUM_TX              8
#define NUM_RAYS            28           /* C(8,2) = 28 */
#define GRID_N              16
#define GRID_PIX            (GRID_N*GRID_N)
#define PIXEL_MM            12.5f

#define V_GRAIN_MS          0.30f
#define V_AIR_MS            0.343f

/* ART重建 */
#define ART_ITER            40
#define ART_LAMBDA          0.25f
#define ART_V_MIN           0.0f
#define ART_V_MAX           2.0f

/* 激励脉冲 */
#define TX_FREQ             40000
#define TX_CYCLES           5
#define TX_AMP              3000

/* ======= 多路选择开关 CD74HC4067 ======= */
#define MUX_S0_PORT         GPIOA
#define MUX_S0_PIN          GPIO_Pin_6
#define MUX_S1_PORT         GPIOA
#define MUX_S1_PIN          GPIO_Pin_7
#define MUX_S2_PORT         GPIOA
#define MUX_S2_PIN          GPIO_Pin_8
#define MUX_S3_PORT         GPIOB
#define MUX_S3_PIN          GPIO_Pin_0   /* S3接地 -> 仅用通道0~7 */

/* ======= 风扇PWM (4路) ======= */
#define FAN_NUM             4
/* 0=风扇1(PA1,TIM2_CH2) 1=风扇2(PB1,TIM3_CH4)
   2=风扇3(PB8,TIM4_CH3)  3=风扇4(PB9,TIM4_CH4) */
#define FAN_PWM_FREQ_HZ     25000       /* PWM频率(Hz)。2线直流风扇若在25kHz下不转,
                                           可改成 1000~5000 再试 */

/* 逐路反相标志(bit0=风扇1/PA1, bit1=风扇2/PB1, bit2=风扇3/PB8, bit3=风扇4/PB9)
   某一路的驱动级是"低电平导通"(P沟道MOS/PNP/低有效驱动板)时, 把对应位置1。
   例: 只有 PB8 那一路反相 -> 填 0x04
   备注: 之前只有全局 FAN_ACTIVE_LOW, 现改为逐路, 便于单独处理某一路接反。 */
#define FAN_ACTIVE_LOW_CH   0x00
#define FAN_ACTIVE_LOW      0           /* 兼容旧配置: 置1相当于 0x0F 全部反相 */
#define FAN_BOOT_TEST       1           /* 1=上电自检(含"全停观察"阶段+逐路3秒) */

/* ======= 舵机 (4路, 软件PWM 50Hz) =======
 * 原来 PA5 上的"窗户1"舵机已拆掉, PA5 改接 DHT11(舱外温湿度)。
 * 现有: 0=窗户2(PB15) 1=窗户3(PB14) 2=窗户4(PA11) 3=门(PA12) */
#define SERVO_NUM           4
#define SERVO_ANGLE_CLOSE   0           /* 关闭角度 */
#define SERVO_ANGLE_OPEN    90          /* 打开角度 */
/* 软件PWM: 100us中断, 200步=20ms, 0.5ms=5步, 1.5ms=15步 */
#define SERVO_PERIOD        200         /* 20ms / 100us */
#define SERVO_MIN_PULSE     5           /* 0.5ms = 5步 */
#define SERVO_MAX_PULSE     15          /* 1.5ms = 15步 (0~90度) */

/* ======= 舱外温湿度 DHT11 (单总线, PA5) =======
 * 与从板(仓内)的温湿度电缆对比, 决定风扇转速。
 * TH_SENSOR_TYPE: 0=DHT11 1=AM2302(DHT22) 2=自动识别(推荐) */
#define TH_DATA_PORT        GPIOA
#define TH_DATA_PIN         GPIO_Pin_5
#define TH_TYPE_DHT11       0
#define TH_TYPE_AM2302      1
#define TH_TYPE_AUTO        2
#define TH_SENSOR_TYPE      TH_TYPE_AUTO
#define TH_READ_PERIOD_MS   2000           /* 舱外温湿度采集周期(器件刷新周期≥1s) */

/* ======= 自动通风控制 (仓内数据来自从板LoRa) =======
 * 规则: 仓内比仓外"又热又潮"时开风扇通风, 差值越大转速越高;
 *       仓内湿度超过危险值且仓外更干燥时强制通风。
 *       仓外太湿(比仓内还潮)时不做除湿通风, 免得越吹越潮。 */
#define VENT_ENABLE         1              /* 1=上电即自动通风 0=仅手动(LoRa命令) */
#define VENT_T_DIFF_ON      2.0f           /* 仓内比仓外高 ≥2℃ 开始通风 */
#define VENT_RH_DIFF_ON     5.0f           /* 仓内比仓外高 ≥5%RH 开始通风 */
#define VENT_RH_URGENT      75.0f          /* 仓内湿度 ≥75%RH 视为紧急, 强制通风 */
#define VENT_MIN_SPEED      40             /* 通风时最低转速 % */
#define VENT_MAX_SPEED      100            /* 最高转速 % */
#define VENT_GAIN_T         8              /* 每高1℃增加 8% 转速 */
#define VENT_GAIN_RH        2              /* 每高1%RH增加 2% 转速 */
#define VENT_RAMP_STEP      20             /* 每次调节最大变化量 %, 防止转速突变 */
#define VENT_CTRL_PERIOD_MS 1000           /* 控制周期 */
#define NODE_TIMEOUT_MS     30000UL        /* 多久收不到从板数据就判定掉线 → 停风扇 */
#define NODE_ENV_FRESH_MS   15000UL        /* 从板环境数据的"新鲜"时限 */

/* ======= BH1750 (软件I2C) ======= */
#define BH1750_SCL_PORT     GPIOB
#define BH1750_SCL_PIN      GPIO_Pin_10
#define BH1750_SDA_PORT     GPIOB
#define BH1750_SDA_PIN      GPIO_Pin_11
#define BH1750_ADDR         0x23

/* ======= 蜂鸣器 (有源, PB12) ======= */
#define BUZZER_PORT         GPIOB
#define BUZZER_PIN          GPIO_Pin_12

/* ======= WS2812B 灯带 (PB13) ======= */
#define WS2812_PORT         GPIOB
#define WS2812_PIN          GPIO_Pin_13
#define WS2812_NUM          12          /* LED数量 */

/* ======= LED状态指示 (PB2) ======= */
#define LED_PORT            GPIOB
#define LED_PIN             GPIO_Pin_2

/* ======= LoRa (WH-L101, USART2 透传) ======= */
#define LORA_USART          USART2
#define LORA_BAUD           115200
#define LORA_PKT_MAX        128

/* LoRa 下行控制命令 (透传模式, 前缀0xC5 + 命令字节) */
#define LORA_CMD_PREFIX     0xC5
#define LORA_CMD_FAN_ON     0xA1    /* 全部风扇开 60% */
#define LORA_CMD_FAN_OFF    0xA2    /* 全部风扇关 */
#define LORA_CMD_WIN_OPEN   0xA3    /* 全部窗户+门打开 */
#define LORA_CMD_WIN_CLOSE  0xA4    /* 全部窗户+门关闭 */
#define LORA_CMD_LED_ON     0xA5    /* 灯带开 (暖白60%) */
#define LORA_CMD_LED_OFF    0xA6    /* 灯带关 */
#define LORA_CMD_BEEP       0xA7    /* 蜂鸣器短鸣 */
#define LORA_CMD_FAN_TEST   0xA8    /* 风扇自检: 依次4路100%各跑3秒 */
#define LORA_CMD_VENT_AUTO  0xA9    /* 恢复自动通风 */
#define LORA_CMD_VENT_MANUAL 0xAA   /* 切到手动(不再自动调风扇) */
#define LORA_CMD_NODE_REPORT 0xB1   /* 让从板立即上报一次 */
#define LORA_CMD_NODE_FAST   0xB2   /* 让从板快速上报(1.2s/2s) */
#define LORA_CMD_NODE_NORMAL 0xB3   /* 让从板恢复常规上报(2s/5s) */
#define LORA_CMD_NODE_AUDIO  0xB4   /* 让从板立刻采一帧声音包络 */

/* ======= 工作模式 ======= */
enum {
    MODE_TOMO = 0,   /* 层析成像 */
    MODE_AE,         /* 虫害声发射监听 */
    MODE_LEVEL,      /* 料位 */
    MODE_NUM
};

/* ======= 异常类型 ======= */
enum {
    A_NORM = 0,
    A_VOID,          /* 空洞(声速偏高,红) */
    A_WET,           /* 受潮(声速偏低,蓝) */
    A_HOT,           /* 发热(橙) */
    A_CMP,           /* 压实(黄) */
    A_INS,           /* 虫害 */
    A_NUM
};

/* ======= LoRa数据包类型 ======= */
#define PKT_VMAP    0x01
#define PKT_AMAP    0x02
#define PKT_WAVE    0x03
#define PKT_LEVEL   0x04
#define PKT_SUMMARY 0x05
#define PKT_AE      0x06
#define PKT_ENV     0x07    /* 环境数据(光照+风扇状态) */
#define PKT_VOICE   0x08    /* 预留 */
#define PKT_CTRL    0x09    /* 下行控制命令 */
#define PKT_NODE_ENV   0x10  /* 从板→主板: 仓内温湿度+气压 */
#define PKT_NODE_SOUND 0x11  /* 从板→主板: 虫害声特征(双通道RMS/峰值/主频) */
#define PKT_NODE_AUDIO 0x12  /* 从板→主板: 声音包络(64点 0~255) */
#define PKT_VENT       0x13  /* 主板→PC: 舱外vs仓内对比 + 风扇决策 */
#define PKT_SOUND      0x14  /* 主板→PC: 转发从板虫声特征(双通道RMS/峰值/主频+等级) */
#define PKT_AUDIO      0x15  /* 主板→PC: 转发从板声音包络(64点 0~255) */
#define PKT_ACK        0x16  /* 主板→PC: 命令确认 data=[cmd, status] 2字节 */

/* PKT_ACK 的 status 码 */
#define ACK_OK          0x00  /* 命令已在主板执行成功 */
#define ACK_UNKNOWN_CMD 0x01  /* 未知命令 */
#define ACK_FORWARDED   0x02  /* 已转发给从板(从板稍后自行上报数据) */

/* ======= 单射线测量结果 ======= */
typedef struct {
    float    tof_ms;
    float    amp_db;
    uint8_t  valid;
} RayMeas;

/* ======= 扫描结果 ======= */
typedef struct {
    float     vmap[GRID_PIX];
    uint8_t   amap[GRID_PIX];
    float     rays[NUM_RAYS];
    uint8_t   main_anom;
    float     conf;
    uint32_t  elapsed_ms;
} ScanRes;

/* ======= 从板上报数据 (LoRa 解析结果) ======= */
#define MIC_ENV_POINTS      64      /* 从板声音包络点数 */

/** 仓内环境 (来自从板 PKT_NODE_ENV) */
typedef struct {
    float    t_in;          /* 仓内温度 ℃    */
    float    rh_in;         /* 仓内湿度 %RH  */
    float    press_hpa;     /* 仓内气压 hPa  */
    uint8_t  th_ok;         /* 温湿度电缆有效 */
    uint8_t  bme_ok;        /* 气压有效 */
    uint8_t  th_type;       /* 0=DHT11 1=AM2302 0xFF=未知 */
    uint32_t stamp_ms;      /* 收到时刻 */
} NodeEnv;

/** 虫害声 (来自从板 PKT_NODE_SOUND) */
typedef struct {
    uint16_t rms[2];        /* 左右声道RMS能量 */
    uint16_t peak[2];       /* 左右声道峰值   */
    uint16_t freq[2];       /* 左右声道主频Hz */
    uint8_t  level;         /* 0=正常 1=可疑 2=虫害 */
    uint32_t stamp_ms;
} NodeSound;

/** 声音包络 (来自从板 PKT_NODE_AUDIO) */
typedef struct {
    uint8_t  env[MIC_ENV_POINTS];
    uint8_t  fresh;         /* 1=有新数据待处理 */
} NodeAudio;

/* ======= SysTick 延时 ======= */
void     SysTick_Init(void);
void     SysTick_Delay_Ms(uint32_t ms);
void     SysTick_Delay_Us(uint32_t us);
uint32_t SysTick_Ms(void);
uint32_t SysTick_UsNow(void);      /* 微秒时刻(DWT周期计数换算) */
uint32_t SysTick_RawCycles(void);  /* 原始CPU周期计数(见 dwt_us.h) */
uint8_t  SysTick_DwtOk(void);      /* DWT周期计数器是否可用 */

/* ======= LED ======= */
void     LED_Set(uint8_t on);

#endif /* __CONFIG_H */
