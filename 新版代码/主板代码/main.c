/**
 * @file    main.c
 * @brief   粮仓声学层析成像系统 -【主板】主程序 (STM32F103C8T6, SPL)
 *
 * 硬件 (实际接线):
 *   8路PZT收发  PA0(ADC/OPA2356) + PA4(DAC) + MUX(PA6/7/8, PB0=S3)
 *   LoRa WH-L101  USART2 PA2/PA3 (透传, 同时收发: 上连PC / 下连从板)
 *   4路DC4010风扇 PA1/PB1/PB8/PB9 (25kHz PWM)
 *   4路SG90舵机   PB15/PB14/PA11/PA12 (软PWM, 3窗+1门)
 *   舱外温湿度    PA5  DHT11 (单总线)   ← 原窗户1舵机位置
 *   BH1750光照    PB10/PB11 软件I2C
 *   蜂鸣器        PB12 (有源)
 *   WS2812B灯带   PB13 (12颗)
 *   LED状态       PB2
 *   按键          PC13=START, PC14=MODE, PC15=SAVE
 *
 * 系统功能:
 *   1. 层析成像: 8换能器28射线扫描 → ART重建16x16声速图 → 异常分类 → LoRa上传
 *   2. 声发射监听 (虫害检测): 采样3秒 → 能量+频率分析 → 判定 → LoRa上传
 *   3. 料位测量: 顶部换能器超声测距 → LoRa上传
 *   4. 环境监控: BH1750光照, 每5秒 PKT_ENV 上传 (lux + 风扇状态)
 *   5. 接收从板数据: 仓内温湿度/气压(PKT_NODE_ENV)、虫害声(PKT_NODE_SOUND/AUDIO)
 *   6. 自动通风: 对比仓内(从板)与舱外(本板DHT11)温湿度 → 自动调4路风扇转速
 *   7. LoRa下行控制: 风扇/窗户舵机/灯带/蜂鸣器远程开关; 也可命令从板立即上报
 *
 * 操作:
 *   PC13 START → 启动扫描/测量 (上电前按住 = PB8隔离诊断)
 *   PC14 MODE  → 切换工作模式 (蜂鸣器响 mode+1 声提示)
 *   PC15 SAVE  → 开/关灯带 (暖白)
 */

#include "config.h"
#include "modules/sensor/sensor.h"
#include "modules/tomography/tomography.h"
#include "modules/lora/lora.h"
#include "modules/fan/fan.h"
#include "modules/servo/servo.h"
#include "modules/buzzer/buzzer.h"
#include "modules/ws2812b/ws2812b.h"
#include "modules/bh1750/bh1750.h"
#include "modules/dht11/dht11.h"

/* ========== 系统时钟配置 (外部晶振8MHz × 9 = 72MHz) ========== */
static void RCC_72MHz(void) {
    /* HSE 开启 */
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    if (RCC_WaitForHSEStartUp() != SUCCESS) {
        /* HSE失败, 内部8MHz勉强用 */
        RCC_HSICmd(ENABLE);
        return;
    }

    /* 预取指缓存 */
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);

    /* AHB=72 APB1=36 APB2=72 */
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);

    /* PLL = HSE × 9 = 72MHz */
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

    /* PLL 作系统时钟 */
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08);
}

/* ========== 全局状态 ========== */
static uint8_t   g_mode    = MODE_TOMO;
static uint8_t   g_busy    = 0;
static uint8_t   g_led_on  = 0;         /* 灯带开关状态 */
static uint32_t  g_lastEnv = 0;         /* 上次环境上报时刻 */
static RayMeas   g_rays[NUM_RAYS];
static ScanRes   g_res;

/* ---- 舱外环境 (本板 DHT11 on PA5) ---- */
static DHT_Data  g_th_out;              /* 舱外温湿度 */
static uint32_t  g_lastDht = 0;

/* ---- 仓内数据 (来自从板, LoRa) ---- */
static NodeEnv   g_nodeEnv;             /* 仓内温湿度+气压 */
static NodeSound g_nodeSound;           /* 虫害声特征 */
static NodeAudio g_nodeAudio;           /* 声音包络 */

/* ---- 自动通风 ---- */
static uint8_t   g_ventAuto   = VENT_ENABLE;   /* 1=自动通风 */
static uint8_t   g_fanCur     = 0;             /* 当前自动转速% (用于限速渐变) */
static uint32_t  g_lastVent   = 0;
static uint8_t   g_ventReason = 0;             /* 0=不通风 1=偏热 2=偏湿 3=紧急湿度 */

/* ========== 蜂鸣器提示音 ========== */
static void beepN(uint8_t n, uint32_t ms) {
    uint8_t i;
    for (i = 0; i < n; i++) {
        Buzzer_Beep(ms);
        delay_ms(120);
    }
}

/* ========== LED闪n次 (辅助识别第几路) ========== */
static void ledN(uint8_t n) {
    uint8_t i;
    for (i = 0; i < n; i++) {
        LED_Set(1);
        delay_ms(150);
        LED_Set(0);
        delay_ms(150);
    }
    delay_ms(200);
}

/* ========== 风扇PWM自检 ==========
 * 每轮: 先"全停观察"5秒(长鸣1声, LED灭) -> 此时4个风扇都该停;
 *       再依次给4路100%各跑3秒, 进入某路前 蜂鸣(ch+1)声 + LED闪(ch+1)次。
 * 对应关系: 1声/1闪=PA1(风扇1), 2=PB1(风扇2), 3=PB8(风扇3), 4=PB9(风扇4)
 * loops = 循环遍数
 */
static void FanSelfTest(uint8_t loops) {
    uint8_t i, k;

    for (k = 0; k < loops; k++) {
        /* 阶段0: 全停观察 —— 4路全部0%, 此时任何还在转的风扇都是"不受控"的 */
        Fan_AllOff();
        LED_Set(0);
        Buzzer_Beep(600);                    /* 长鸣1声 = 全停观察阶段 */
        delay_ms(5000);

        for (i = 0; i < FAN_NUM; i++) {
            ledN((uint8_t)(i + 1));
            beepN((uint8_t)(i + 1), 80);         /* 响几声 = 第几路 */
            Fan_SetSpeed(i, 100);
            delay_ms(3000);
            Fan_SetSpeed(i, 0);
            delay_ms(500);
        }
    }
}

/* ========== PB8(风扇3) 单路隔离诊断 (无限循环) ==========
 * 完全绕开定时器: 4路引脚切成普通推挽GPIO, 逐个给电平。
 *   阶段1 (6秒): PB8 = 低电平, 其余也低电平,  LED灭 + 长鸣1声
 *   阶段2 (6秒): PB8 = 高电平, 其余仍低电平,  LED亮 + 短鸣2声
 * 用法: 上电前按住 START 键。用万用表直流档量 PB8 对 GND:
 *   阶段1应≈0V, 阶段2应≈3.3V。判读见下方注释。
 */
static void FanPb8DiagLoop(void) {
    uint8_t i;

    Fan_DirectMode();          /* 定时器停, 4路引脚变普通推挽输出(全低) */

    while (1) {
        /* 阶段1: 全部输出低电平 */
        for (i = 0; i < FAN_NUM; i++) Fan_DirectSet(i, 0);
        LED_Set(0);
        Buzzer_Beep(400);
        delay_ms(6000);

        /* 阶段2: 只有PB8输出高电平 */
        Fan_DirectSet(2, 1);
        LED_Set(1);
        beepN(2, 120);
        delay_ms(6000);
    }
}

/* ========== 风扇PWM自检 (无限循环, 便于万用表逐路测量) ========== */
static void FanSelfTestLoop(void) {
    while (1) FanSelfTest(1);
}

/* ========== 风扇引脚静态电平测试 (无限循环) ==========
 * 4路引脚一起输出高电平3秒(蜂鸣1声+LED亮) / 一起输出低电平3秒(蜂鸣2声+LED灭)。
 * 判断: 静态高能转但PWM不转 -> 频率/极性不匹配;
 *       静态高、低都不转 -> 驱动级或接线问题, 与程序无关。
 */
static void FanStaticTestLoop(void) {
    uint8_t i;

    Fan_DirectMode();
    while (1) {
        beepN(1, 150);
        for (i = 0; i < FAN_NUM; i++) Fan_DirectSet(i, 1);
        LED_Set(1);
        delay_ms(3000);

        beepN(2, 150);
        for (i = 0; i < FAN_NUM; i++) Fan_DirectSet(i, 0);
        LED_Set(0);
        delay_ms(3000);
    }
}

/* ========== 舱外温湿度采集 (本板 DHT11 on PA5) ========== */
static void DhtTask(void) {
    if ((SysTick_Ms() - g_lastDht) < TH_READ_PERIOD_MS) return;
    g_lastDht = SysTick_Ms();
    if (DHT_Read(&g_th_out) != 0) {
        g_th_out.valid = 0;                 /* 读失败保留上次值, 但标记无效 */
    }
}

/* ========== 从板数据接收 (PKT_NODE_*) ========== */
static void NodeTask(void) {
    static uint8_t buf[LORA_PKT_MAX];
    uint8_t type, len;

    while (LoRa_PollFrame(&type, buf, &len)) {
        switch (type) {
            case PKT_NODE_ENV:                       /* 仓内温湿度 + 气压 */
                if (len >= 10) {
                    int16_t  t_in = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
                    uint16_t rh   = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
                    uint16_t pr   = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
                    uint8_t  fl   = buf[8];

                    g_nodeEnv.th_ok   = (fl & 0x01) ? 1 : 0;
                    g_nodeEnv.bme_ok  = (fl & 0x02) ? 1 : 0;
                    g_nodeEnv.th_type = buf[9];
                    g_nodeEnv.t_in    = g_nodeEnv.th_ok ? ((float)t_in / 10.0f) : 0.0f;
                    g_nodeEnv.rh_in   = g_nodeEnv.th_ok ? ((float)rh  / 10.0f) : 0.0f;
                    g_nodeEnv.press_hpa = (fl & 0x04) ? ((float)pr / 10.0f) : -1.0f;
                    g_nodeEnv.stamp_ms  = SysTick_Ms();
                }
                break;

            case PKT_NODE_SOUND:                     /* 虫害声特征 */
                if (len >= 13) {
                    g_nodeSound.rms[0]  = (uint16_t)(((uint16_t)buf[0]  << 8) | buf[1]);
                    g_nodeSound.peak[0] = (uint16_t)(((uint16_t)buf[2]  << 8) | buf[3]);
                    g_nodeSound.freq[0] = (uint16_t)(((uint16_t)buf[4]  << 8) | buf[5]);
                    g_nodeSound.rms[1]  = (uint16_t)(((uint16_t)buf[6]  << 8) | buf[7]);
                    g_nodeSound.peak[1] = (uint16_t)(((uint16_t)buf[8]  << 8) | buf[9]);
                    g_nodeSound.freq[1] = (uint16_t)(((uint16_t)buf[10] << 8) | buf[11]);
                    g_nodeSound.level   = buf[12];
                    g_nodeSound.stamp_ms = SysTick_Ms();
                }
                break;

            case PKT_NODE_AUDIO:                     /* 声音包络(64点) */
                if (len >= MIC_ENV_POINTS) {
                    memcpy(g_nodeAudio.env, buf, MIC_ENV_POINTS);
                    g_nodeAudio.fresh = 1;
                }
                break;

            default:
                break;
        }
    }
}

/* ========== 自动通风控制 ==========
 * 目标转速 = 由"仓内 vs 舱外"的温差/湿度差决定:
 *   仓内比舱外高 ≥VENT_T_DIFF_ON  ℃   → 通风(偏热)
 *   仓内湿度比舱外高 ≥VENT_RH_DIFF_ON %RH → 通风(偏湿, 且舱外更干燥才有效)
 *   仓内湿度 ≥VENT_RH_URGENT 且舱外更干 → 强制全速
 * 转速 = VENT_MIN_SPEED + 差值×增益, 并做 VENT_RAMP_STEP 限幅渐变, 避免突变。
 * 从板数据超时(NODE_TIMEOUT_MS)则停风扇并置 g_ventReason=0。
 */
static void AutoVentTask(void) {
    uint32_t now = SysTick_Ms();
    uint8_t  target = 0, reason = 0;

    if ((uint32_t)(now - g_lastVent) < VENT_CTRL_PERIOD_MS) return;
    g_lastVent = now;

    if (!g_ventAuto) return;                   /* 手动模式不动风扇 */

    if ((uint32_t)(now - g_nodeEnv.stamp_ms) > NODE_TIMEOUT_MS) {
        /* 从板掉线: 停风扇(安全) */
        if (g_fanCur != 0) { Fan_AllOff(); g_fanCur = 0; }
        g_ventReason = 0;
        return;
    }

    if (g_nodeEnv.th_ok && g_th_out.valid) {
        float dT   = g_nodeEnv.t_in  - g_th_out.temp_c;
        float dRH  = g_nodeEnv.rh_in - g_th_out.hum_pct;
        float need = 0.0f;

        if (g_nodeEnv.rh_in >= VENT_RH_URGENT && g_th_out.hum_pct < g_nodeEnv.rh_in) {
            target = VENT_MAX_SPEED;           /* 仓内太潮, 舱外更干 → 全速 */
            reason = 3;
        } else {
            if (dRH > VENT_RH_DIFF_ON) { need += dRH * (float)VENT_GAIN_RH; reason = 2; }
            if (dT  > VENT_T_DIFF_ON)  { need += dT  * (float)VENT_GAIN_T;
                                         if (reason == 0) reason = 1; }
            if (need > 0.0f) {
                uint32_t sp = VENT_MIN_SPEED + (uint32_t)need;
                if (sp > VENT_MAX_SPEED) sp = VENT_MAX_SPEED;
                target = (uint8_t)sp;
            }
        }
    } else {
        /* 舱外数据不可用: 只按仓内湿度绝对值保守通风 */
        if (g_nodeEnv.th_ok && g_nodeEnv.rh_in >= VENT_RH_URGENT) {
            target = VENT_MIN_SPEED;
            reason = 2;
        }
    }

    /* 限幅渐变 */
    if (target != g_fanCur) {
        uint8_t prev = g_fanCur;
        if (target > g_fanCur) {
            g_fanCur = ((uint8_t)(target - g_fanCur) > VENT_RAMP_STEP)
                       ? (uint8_t)(g_fanCur + VENT_RAMP_STEP) : target;
        } else {
            g_fanCur = ((uint8_t)(g_fanCur - target) > VENT_RAMP_STEP)
                       ? (uint8_t)(g_fanCur - VENT_RAMP_STEP) : target;
        }

        if (g_fanCur == 0) {
            Fan_AllOff();
        } else if (prev == 0) {
            Fan_SetAllKick(g_fanCur, 300);     /* 从停到转: 先全速起转 */
        } else {
            Fan_SetAll(g_fanCur);
        }
    }

    g_ventReason = reason;
}

/* ========== 通风决策上报 (给PC上位机: 舱外 vs 仓内 + 转速) ========== */
static void SendVentPacket(void) {
    uint8_t  buf[16];
    int16_t  t_out = g_th_out.valid ? (int16_t)(g_th_out.temp_c * 10.0f) : (int16_t)0x8000;
    uint16_t rh_out = g_th_out.valid ? (uint16_t)(g_th_out.hum_pct * 10.0f) : 0xFFFF;
    int16_t  t_in = g_nodeEnv.th_ok ? (int16_t)(g_nodeEnv.t_in * 10.0f) : (int16_t)0x8000;
    uint16_t rh_in = g_nodeEnv.th_ok ? (uint16_t)(g_nodeEnv.rh_in * 10.0f) : 0xFFFF;
    uint16_t press = (g_nodeEnv.press_hpa > 100.0f) ? (uint16_t)(g_nodeEnv.press_hpa * 10.0f) : 0xFFFF;
    uint8_t  fl = 0;

    if (g_th_out.valid) fl |= 0x01;                                  /* 舱外有效 */
    if (g_nodeEnv.th_ok) fl |= 0x02;                                 /* 仓内有效 */
    if ((SysTick_Ms() - g_nodeEnv.stamp_ms) <= NODE_TIMEOUT_MS) fl |= 0x04;  /* 从板在线 */

    buf[0]  = (uint8_t)((uint16_t)t_out >> 8);
    buf[1]  = (uint8_t)((uint16_t)t_out & 0xFF);
    buf[2]  = (uint8_t)(rh_out >> 8);
    buf[3]  = (uint8_t)(rh_out & 0xFF);
    buf[4]  = (uint8_t)((uint16_t)t_in >> 8);
    buf[5]  = (uint8_t)((uint16_t)t_in & 0xFF);
    buf[6]  = (uint8_t)(rh_in >> 8);
    buf[7]  = (uint8_t)(rh_in & 0xFF);
    buf[8]  = (uint8_t)(press >> 8);
    buf[9]  = (uint8_t)(press & 0xFF);
    buf[10] = g_fanCur;                       /* 自动通风转速% */
    buf[11] = g_ventReason;                   /* 0=不通风 1=偏热 2=偏湿 3=紧急 */
    buf[12] = g_nodeSound.level;              /* 从板虫害判定 */
    buf[13] = fl;
    buf[14] = (uint8_t)Fan_GetSpeed(0);
    buf[15] = (uint8_t)(g_ventAuto ? 1 : 0);

    LoRa_SendPacket(PKT_VENT, 0, 1, buf, 16);
}

/* ========== 从板虫声数据转发 (给PC上位机: 特征 + 包络波形) ==========
 * 从板每5秒(快速模式2秒)上报一次声音; 主板原样转发给PC, 让上位机能画
 * "仓内虫声包络"波形并显示双通道RMS/峰值/主频。从板掉线时不转发。
 */
static void SendSoundPacket(void) {
    uint8_t buf[13];

    /* 从板掉线 → 不转发(避免PC显示过期数据) */
    if ((uint32_t)(SysTick_Ms() - g_nodeSound.stamp_ms) > NODE_TIMEOUT_MS) return;

    buf[0]  = (uint8_t)(g_nodeSound.rms[0]  >> 8);
    buf[1]  = (uint8_t)(g_nodeSound.rms[0]  & 0xFF);
    buf[2]  = (uint8_t)(g_nodeSound.peak[0] >> 8);
    buf[3]  = (uint8_t)(g_nodeSound.peak[0] & 0xFF);
    buf[4]  = (uint8_t)(g_nodeSound.freq[0] >> 8);
    buf[5]  = (uint8_t)(g_nodeSound.freq[0] & 0xFF);
    buf[6]  = (uint8_t)(g_nodeSound.rms[1]  >> 8);
    buf[7]  = (uint8_t)(g_nodeSound.rms[1]  & 0xFF);
    buf[8]  = (uint8_t)(g_nodeSound.peak[1] >> 8);
    buf[9]  = (uint8_t)(g_nodeSound.peak[1] & 0xFF);
    buf[10] = (uint8_t)(g_nodeSound.freq[1] >> 8);
    buf[11] = (uint8_t)(g_nodeSound.freq[1] & 0xFF);
    buf[12] = g_nodeSound.level;                 /* 0=正常 1=可疑 2=虫害 */

    LoRa_SendPacket(PKT_SOUND, 0, 1, buf, 13);

    /* 有新的一帧64点包络就跟着转发一次 */
    if (g_nodeAudio.fresh) {
        g_nodeAudio.fresh = 0;
        LoRa_SendPacket(PKT_AUDIO, 0, 1, g_nodeAudio.env, MIC_ENV_POINTS);
    }
}

/* ========== LoRa 下行命令执行 (来自PC上位机) ==========
 * 返回 ACK 状态码: ACK_OK / ACK_UNKNOWN_CMD / ACK_FORWARDED,
 * 由调用方打包成 PKT_ACK 发回 PC, 让上位机能确认命令是否送达。 */
static uint8_t HandleLoRaCmd(uint8_t cmd) {
    switch (cmd) {
        case LORA_CMD_FAN_ON:
            g_ventAuto = 0;                 /* 手动开风扇 → 退出自动通风 */
            g_fanCur   = 60;
            Fan_SetAllKick(60, 300);        /* 全速启动300ms后降到60% */
            beepN(1, 40);
            break;
        case LORA_CMD_FAN_OFF:
            g_ventAuto = 0;
            g_fanCur   = 0;
            Fan_AllOff();
            beepN(1, 40);
            break;
        case LORA_CMD_VENT_AUTO:
            g_ventAuto = 1;                 /* 恢复自动通风 */
            beepN(2, 60);
            break;
        case LORA_CMD_VENT_MANUAL:
            g_ventAuto = 0;                 /* 手动模式, 不再自动调转速 */
            beepN(3, 60);
            break;
        case LORA_CMD_NODE_REPORT:          /* 让从板立即上报一次 */
            LoRa_SendCtrl(LORA_CMD_NODE_REPORT);
            return ACK_FORWARDED;
        case LORA_CMD_NODE_FAST:
            LoRa_SendCtrl(LORA_CMD_NODE_FAST);
            return ACK_FORWARDED;
        case LORA_CMD_NODE_NORMAL:
            LoRa_SendCtrl(LORA_CMD_NODE_NORMAL);
            return ACK_FORWARDED;
        case LORA_CMD_NODE_AUDIO:
            LoRa_SendCtrl(LORA_CMD_NODE_AUDIO);
            return ACK_FORWARDED;
        case LORA_CMD_WIN_OPEN:
            Servo_OpenAll();
            beepN(2, 40);
            break;
        case LORA_CMD_WIN_CLOSE:
            Servo_CloseAll();
            beepN(2, 40);
            break;
        case LORA_CMD_LED_ON:
            WS2812B_SetBrightness(60);
            g_led_on = 1;
            beepN(1, 40);
            break;
        case LORA_CMD_LED_OFF:
            WS2812B_Off();
            g_led_on = 0;
            beepN(1, 40);
            break;
        case LORA_CMD_BEEP:
            beepN(3, 80);
            break;
        case LORA_CMD_FAN_TEST:
            FanSelfTest(1);
            break;
        default:
            return ACK_UNKNOWN_CMD;        /* 未知命令, 上位机会显示错误码 */
    }
    return ACK_OK;
}

/* ========== 环境监控: BH1750采集 + PKT_ENV上传 + 通风决策上报 ========== */
static void EnvTask(void) {
    uint16_t lux;
    uint8_t  fan_pct[4];
    int      i;

    if ((SysTick_Ms() - g_lastEnv) < 5000) return;
    g_lastEnv = SysTick_Ms();

    lux = BH1750_ReadLight();
    for (i = 0; i < FAN_NUM; i++) fan_pct[i] = Fan_GetSpeed((uint8_t)i);
    LoRa_SendEnv(lux, fan_pct, g_led_on);

    /* 舱外 vs 仓内 对比 + 风扇决策 (供PC上位机显示/记录) */
    SendVentPacket();

    /* 转发从板虫声特征 + 64点包络 (供PC上位机画波形) */
    SendSoundPacket();

    /* 从板虫害声判定为虫害 → 蜂鸣器提示 + 灯带闪红(若灯带本来是关的) */
    if (g_nodeSound.level >= 2 &&
        (uint32_t)(SysTick_Ms() - g_nodeSound.stamp_ms) < 15000UL) {
        beepN(3, 200);
        if (!g_led_on) {
            WS2812B_SetAll(80, 0, 0);
            WS2812B_Show();
            delay_ms(500);
            WS2812B_Off();
        }
    }
}

/* ========== 层析成像模式 ========== */
static void RunTomo(void) {
    uint32_t t0;
    float    rayT[NUM_RAYS];
    float    vmin, vmax;
    int      i;

    g_busy = 1;
    LED_Set(1);
    beepN(1, 60);

    /* 1. 扫描全部28条射线 */
    t0 = SysTick_Ms();
    Sensor_ScanAll(g_rays);

    /* 2. 提取穿越时间数组 (无效填充默认) */
    for (i = 0; i < NUM_RAYS; i++) {
        if (g_rays[i].valid) rayT[i] = g_rays[i].tof_ms;
        else                 rayT[i] = 200.0f / (V_GRAIN_MS * 1000.0f);
    }
    for (i = 0; i < NUM_RAYS; i++) g_res.rays[i] = rayT[i];

    /* 3. ART重建声速图 */
    Tomo_Reconstruct(rayT, g_res.vmap);

    /* 4. 异常分类 */
    Tomo_Classify(g_res.vmap, g_res.amap,
                  &g_res.main_anom, &g_res.conf);

    g_res.elapsed_ms = SysTick_Ms() - t0;

    /* 5. 上传到PC上位机 (LoRa无线) */
    /* 计算声速范围用于量化 */
    vmin = 999.0f; vmax = 0.0f;
    for (i = 0; i < GRID_PIX; i++) {
        float cx = (float)(i % GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        float cy = (float)(i / GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        if (cx*cx + cy*cy <= 90.0f*90.0f) {
            if (g_res.vmap[i] < vmin) vmin = g_res.vmap[i];
            if (g_res.vmap[i] > vmax) vmax = g_res.vmap[i];
        }
    }
    if (vmax - vmin < 0.01f) { vmax = vmin + 0.1f; }

    LoRa_SendVMap(g_res.vmap, vmin, vmax);
    LoRa_SendAMap(g_res.amap, g_res.main_anom,
                  g_res.conf, g_res.elapsed_ms);

    /* 6. 本地反馈: 异常 → 长鸣, 正常 → 短鸣 */
    if (g_res.main_anom != A_NORM) beepN(3, 300);
    else                           beepN(1, 60);

    LED_Set(0);
    delay_ms(2000);
    g_busy = 0;
}

/* ========== 声发射监听/虫害检测 ========== */
static void RunAE(void) {
    uint16_t aeBuf[512];
    float    energy, freqHz;
    uint16_t zc, pk;
    int16_t  prev, v;
    uint16_t i;
    uint8_t  detected;
    uint32_t s;

    g_busy = 1;
    LED_Set(1);
    beepN(1, 60);

    /* 监听3秒 (50kHz) */
    s = SysTick_Ms();
    Sensor_ListenAE(aeBuf, 512, 3000);

    /* 能量 + 过零率分析 */
    energy = 0;
    zc = 0;
    pk = 0;
    prev = (int16_t)aeBuf[0] - 2048;
    for (i = 0; i < 512; i++) {
        v = (int16_t)aeBuf[i] - 2048;
        energy += (float)v * v;
        if ((uint16_t)v > pk) pk = (uint16_t)v;
        if (((prev < 0) && (v >= 0)) || ((prev >= 0) && (v < 0))) zc++;
        prev = v;
    }
    energy = sqrtf(energy / 512.0f);
    freqHz = (float)zc / 2.0f * 50000.0f / 512.0f;

    if (energy > 200 && freqHz > 1000 && freqHz < 15000) {
        detected = 2;    /* 虫害 */
    } else if (energy > 100) {
        detected = 1;    /* 可疑 */
    } else {
        detected = 0;    /* 正常 */
    }

    /* 上传虫害监听报告 (LoRa): 能量+频率+判定 */
    {
        uint8_t ae_pkt[5];
        uint16_t e_int = (uint16_t)energy;
        uint16_t f_int = (uint16_t)freqHz;
        ae_pkt[0] = (uint8_t)(e_int >> 8);
        ae_pkt[1] = (uint8_t)(e_int & 0xFF);
        ae_pkt[2] = (uint8_t)(f_int >> 8);
        ae_pkt[3] = (uint8_t)(f_int & 0xFF);
        ae_pkt[4] = detected;
        LoRa_SendPacket(PKT_AE, 0, 1, ae_pkt, 5);
    }

    /* 上传声发射波形 (LoRa): 512点 12bit ADC, 供上位机显示 */
    LoRa_SendWave(aeBuf, 512);

    /* 本地反馈: 虫害3长鸣, 可疑2中鸣, 正常1短鸣 */
    if      (detected == 2) beepN(3, 300);
    else if (detected == 1) beepN(2, 150);
    else                    beepN(1, 60);

    LED_Set(0);
    delay_ms(2000);
    g_busy = 0;
}

/* ========== 料位测量 ========== */
static void RunLevel(void) {
    float L, v;
    int   nOK, i;

    g_busy = 1;
    LED_Set(1);
    beepN(1, 60);

    /* 5次平均 */
    L = 0; nOK = 0;
    for (i = 0; i < 5; i++) {
        v = Sensor_Level_mm();
        if (v > 1.0f && v < 2000.0f) { L += v; nOK++; }
        delay_ms(100);
    }
    if (nOK > 0) L /= (float)nOK;
    else         L = 0;

    /* 上调料位到PC上位机 (LoRa) */
    LoRa_SendLevel(L, 300.0f);

    /* 本地反馈: 测量完成1短鸣 */
    beepN(1, 60);

    LED_Set(0);
    delay_ms(2000);
    g_busy = 0;
}

/* ========== 主函数 ========== */
int main(void) {
    uint8_t cmd;

    /* 1. 系统时钟 */
    RCC_72MHz();

    /* 2. 各模块初始化 */
    Sensor_Init();         /* GPIO/ADC/DAC/MUX/SysTick/按键 */
    Tomo_Init();           /* 射线矩阵计算 */
    LoRa_Init();           /* LoRa USART2 (WH-L101透传) */
    Fan_Init();            /* 4路风扇 PWM (PA1/PB1/PB8/PB9) */
    Servo_Init();          /* 4路舵机 软PWM (TIM1 100us中断) */
    Buzzer_Init();         /* 有源蜂鸣器 (PB12) */
    WS2812B_Init();        /* WS2812B灯带 (PB13) */
    BH1750_Init();         /* 光照传感器 (PB10/PB11 软I2C) */
    DHT_Init();            /* 舱外温湿度 DHT11 (PA5), 内含1.1s稳定延时 */

    /* 3. 上电自检: 灯带绿色闪一下 → 蜂鸣器1声 → LED亮 */
    WS2812B_SetAll(0, 60, 0);
    WS2812B_Show();
    delay_ms(300);
    WS2812B_Off();
    beepN(1, 60);
    LED_Set(1);

    /* 3.1 风扇排查入口 (都是上电前按住对应按键):
     *     按住 START(PC13) -> PB8单路隔离诊断(静态电平, 绕开PWM) 无限循环
     *     按住 SAVE(PC15)  -> PWM自检无限循环: 全停5秒 + 每路100%跑3秒
     *     按住 MODE(PC14)  -> 引脚静态电平测试无限循环: 4路一起高3秒/低3秒
     *     FAN_BOOT_TEST=1  -> 每次上电自动跑一遍PWM自检 (调试完改回0)
     *   风扇对应: 1声/1闪=PA1, 2=PB1, 3=PB8, 4=PB9
     */
    if (Key_Read(0)) {
        FanPb8DiagLoop();       /* 不返回 */
    }
    if (Key_Read(2)) {
        FanSelfTestLoop();      /* 不返回 */
    }
    if (Key_Read(1)) {
        FanStaticTestLoop();    /* 不返回 */
    }
#if (FAN_BOOT_TEST)
    FanSelfTest(1);
#endif

    /* 4. 舵机归位 (全部窗户+门关闭) */
    Servo_CloseAll();

    /* 4.1 首次采集: 舱外温湿度 + 通知从板立即上报一次 */
    DHT_Read(&g_th_out);
    g_lastDht = SysTick_Ms();
    LoRa_SendCtrl(LORA_CMD_NODE_REPORT);

    /* 5. 主循环 */
    while (1) {
        /* --- LoRa下行控制命令 (来自PC上位机) --- */
        cmd = LoRa_GetCmd();
        if (cmd) {
            /* 执行并回 ACK, 上位机据此显示"主板已确认/错误码/超时" */
            uint8_t ack[2];
            ack[0] = cmd;
            ack[1] = HandleLoRaCmd(cmd);
            LoRa_SendPacket(PKT_ACK, 0, 1, ack, 2);
        }

        /* --- 接收从板数据 (仓内温湿度/气压/虫害声) --- */
        NodeTask();

        /* --- 舱外温湿度 (每2秒) --- */
        DhtTask();

        /* --- 自动通风: 对比仓内/舱外 → 调风扇转速 --- */
        AutoVentTask();

        /* --- 按键 (START=0, MODE=1, SAVE=2) --- */
        if (Key_Read(1) && !g_busy) {   /* MODE键 → 切模式 */
            g_mode = (g_mode + 1) % MODE_NUM;
            beepN((uint8_t)(g_mode + 1), 60);   /* 响mode+1声提示 */
        }

        if (Key_Read(0) && !g_busy) {  /* START键 → 运行当前模式 */
            switch (g_mode) {
                case MODE_TOMO:  RunTomo();  break;
                case MODE_AE:    RunAE();    break;
                case MODE_LEVEL: RunLevel(); break;
            }
        }

        if (Key_Read(2) && !g_busy) {   /* SAVE键 → 开/关灯带 */
            if (g_led_on) { WS2812B_Off();  g_led_on = 0; }
            else          { WS2812B_SetBrightness(60); g_led_on = 1; }
            beepN(1, 40);
        }

        /* --- 环境监控 (每5秒上报光照+风扇状态+通风决策) --- */
        EnvTask();

        delay_ms(10);
    }
}
