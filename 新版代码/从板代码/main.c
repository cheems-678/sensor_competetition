/**
 * @file    main.c
 * @brief   粮仓监测系统 -【从板 / 传感器节点】主程序 (STM32F103C8T6, SPL)
 *
 * 从板职责 (按从板原理图 2026-09-21):
 *   1. 单总线温湿度电缆  PA6 (外部4.7k上拉)  → 仓内温度/湿度
 *   2. BME280/BMP280     PB6/PB7 软件I2C      → 仓内气压(附温湿度)
 *   3. SPH0645LM4H ×2    PB12/PB13/PB15 I2S2 → 粮堆内虫子活动声
 *   4. LoRa WH-L101      PA2/PA3 USART2       → 把以上数据上报主板
 *                          PA4=RELOAD PA1=WAKE PA0=HOST_WAKE
 *
 * 上报周期:
 *   常规: 环境(温湿度+气压) 2s 一次; 声音(特征+包络) 5s 一次
 *   快速: 主板发 0xC5 0xB2 后 → 环境1.2s / 声音2s
 *
 * 主板下行命令 (0xC5 + 命令字节, 见 config.h):
 *   0xB1 立即上报一次   0xB2 快速上报   0xB3 常规上报
 *   0xB4 立即采一帧声音 0xB5 (预留)
 *
 * 本地指示: LED(PB2) 每完成一次环境上报翻转一次(心跳)
 */

#include "config.h"
#include "modules/systick/systick.h"
#include "modules/lora/lora.h"
#include "modules/dht11/dht11.h"
#include "modules/bme280/bme280.h"
#include "modules/sph0645/sph0645.h"

/* ========== 系统时钟 (外部8MHz × 9 = 72MHz) ========== */
static void RCC_72MHz(void)
{
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    if (RCC_WaitForHSEStartUp() != SUCCESS) {
        RCC_HSICmd(ENABLE);            /* HSE起不来就用内部8MHz */
        return;
    }

    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);

    RCC_HCLKConfig(RCC_SYSCLK_Div1);   /* AHB=72M */
    RCC_PCLK1Config(RCC_HCLK_Div2);    /* APB1=36M */
    RCC_PCLK2Config(RCC_HCLK_Div1);    /* APB2=72M */

    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
}

/* ========== 全局状态 ========== */
static uint8_t  g_rpt_mode   = RPT_NORMAL;
static uint32_t g_lastEnv    = 0;
static uint32_t g_lastSound  = 0;
static uint8_t  g_led        = 0;
static uint8_t  g_bme_ok     = 0;

static TH_Data     g_th;
static BME280_Data g_bme;
static MicFrame    g_mic;

/* ========== 上报周期 ========== */
static uint32_t EnvPeriodMs(void)
{
    return (g_rpt_mode == RPT_FAST) ? 1200UL : (uint32_t)TH_READ_PERIOD_MS;
}
static uint32_t SoundPeriodMs(void)
{
    return (g_rpt_mode == RPT_FAST) ? 2000UL : (uint32_t)MIC_CAPTURE_PERIOD_MS;
}

/* ========== 环境上报 (温湿度电缆 + 气压) ========== */
static void DoEnvReport(void)
{
    if (DHT_Read(&g_th) != 0) {
        g_th.valid = 0;                /* 本次读取失败, 仍上报(带标志) */
    }

    if (g_bme_ok) {
        if (BME280_Read(&g_bme) != 0) g_bme_ok = 0;   /* 掉线则下次重试 */
    } else {
        g_bme_ok = (BME280_Init() == 0) ? 1 : 0;      /* 尝试(重新)初始化 */
    }

    LoRa_SendNodeEnv(&g_th, &g_bme, g_bme_ok);

    g_led ^= 1;                        /* LED心跳 */
    LED_Set(g_led);
}

/* ========== 声音采集上报 ========== */
static void DoSoundReport(uint8_t with_audio)
{
    if (Mic_Capture(&g_mic) != 0) return;              /* 超时, 本次跳过 */

    LoRa_SendNodeSound(&g_mic);
    if (with_audio) LoRa_SendNodeAudio(&g_mic);

    /* 判定为虫害/可疑时, 让LED快速闪两下提示现场人员 */
    if (g_mic.level >= 2) {
        uint8_t i;
        for (i = 0; i < 4; i++) {
            LED_Set(i & 1);
            delay_ms(80);
        }
        LED_Set(g_led);
    }
}

/* ========== 主板下行命令 ========== */
static void HandleCmd(uint8_t cmd)
{
    switch (cmd) {
        case LORA_CMD_NODE_REPORT:
            DoEnvReport();
            DoSoundReport(0);
            break;
        case LORA_CMD_NODE_FAST:
            g_rpt_mode = RPT_FAST;
            break;
        case LORA_CMD_NODE_NORMAL:
            g_rpt_mode = RPT_NORMAL;
            break;
        case LORA_CMD_NODE_AUDIO:
            DoSoundReport(1);
            break;
        default:
            break;
    }
}

/* ========== 主函数 ========== */
int main(void)
{
    uint8_t  i;
    uint8_t  cmd;
    uint32_t now;

    RCC_72MHz();
    SysTick_Init();
    LED_Set(0);

    /* ---- 各模块初始化 ---- */
    LoRa_Init();                    /* USART2 + 控制脚 */
    LoRa_WakeUp();                  /* WAKE = 高(正常工作) */
    Mic_Init();                     /* I2S2 + DMA (时钟持续输出) */
    DHT_Init();                     /* 内含 1.1s 器件稳定延时 */
    g_bme_ok = (BME280_Init() == 0) ? 1 : 0;

    /* ---- 上电指示: LED 闪 3 下 ---- */
    for (i = 0; i < 3; i++) {
        LED_Set(1); delay_ms(120);
        LED_Set(0); delay_ms(120);
    }
    g_led = 0;

    /* ---- 首报: 环境 + 声音各一次 ---- */
    DoEnvReport();
    DoSoundReport(1);
    g_lastEnv   = SysTick_Ms();
    g_lastSound = g_lastEnv;

    /* ---- 主循环 ---- */
    while (1) {
        /* 主板下行命令 */
        cmd = LoRa_GetCmd();
        if (cmd) HandleCmd(cmd);

        now = SysTick_Ms();

        /* 环境上报 */
        if ((uint32_t)(now - g_lastEnv) >= EnvPeriodMs()) {
            g_lastEnv = now;
            DoEnvReport();
        }

        /* 声音采集上报 */
        if ((uint32_t)(now - g_lastSound) >= SoundPeriodMs()) {
            g_lastSound = SysTick_Ms();
            DoSoundReport(1);
        }

        delay_ms(20);
    }
}
