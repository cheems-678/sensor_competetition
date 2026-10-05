/**
 ******************************************************************************
 * @file    main.c
 * @brief   仓眸探微 —— 嵌入式声学层析成像与害虫智能识别监测系统 (仿真平台版)
 *
 * @details 本文件为 puliedu 嵌入式虚拟仿真平台的 STM32F103C8T6 应用主程序，
 *          与真实硬件主板共用同一套 0xAA 0x55 通信协议，可被 pc_host.py /
 *          web_host 直接解析，形成"仿真 ↔ 上位机"的同源证据链。
 *
 * 一、引脚分配 (2026-10-01 定稿 · OLED + 4 路 PWM 风扇版，
 *     与 docs/仿真平台外设选型与引脚分配.md 一致)
 *   PA0  - GPIO 输入下拉         超声波 ECHO (HC-SR04 OUT; 电平轮询计时)
 *   PA1  - 空闲 (**舱外** DHT11 首选候选脚; 实际接哪根由开机扫描测定)
 *   PA2  - USART2_TX           **RS485 风速传感器 B(−)**  (2026-10-03 新增; 原超声波 TRIG)
 *   PA3  - USART2_RX           **RS485 风速传感器 A(+)**  (2026-10-03 新增; 原报警灯)
 *                                 ★★ 平台没有 MAX485 收发器 —— 不需要它: 平台把这类 485
 *                                   器件简化成"一对串口线", A→RX、B→TX 直连即可(半边双工
 *                                   简化成全双工)。**实物**才要加 MAX485/SP3485:
 *                                   A→A、B→B, RO→PA3、DI→PA2, DE/RE 并接一个空闲 GPIO 切换方向。
 *                                 ★ 接反的现象是 USART2 一个字节都收不到; 发 'V' 打开逐字节
 *                                   hex 打印, 线上有没有东西、接没接反, 当场就能分开。
 *                                 ★ 为什么占用 PA2/PA3: USART1=调试/协议帧、USART3=ESP8266,
 *                                   C8T6 只有 3 路 USART, 只剩 USART2, 而它的脚是固定死的。
 *   PA4  - ADC1_IN4            **光敏电阻模块 (photosensitive_resistance) AO** = 舱外光照
 *                                 ★ 平台这个元件**自己把电压灌进 ADC**(前端算好接线后直接
 *                                   send analog_type → ADCx_IN4), 固件零协议代码, 读通道 4 即可
 *                                 ★ 原为"舱外 DHT11 候选 / DHT_IN_PA14=0 时的仓内 DHT11 备用脚",
 *                                   现已让给光照 —— DHT_IN_ALT 已改到 **PA8**
 *                                   (PA4 → PB2 → PA8; PB2 已在 2026-10-04 让给北斗软串口 RX)
 *   PA5  - GPIO 推挽 + **软件 PWM**  **补光 LED**（阴极接地，阳极串 10K 电阻后接本脚）
 *                                 ★ 高电平点亮；亮度由占空比连续调节（0~100%），
 *                                   占空比 = 100 − 舱外光照(%)，即**环境越暗补光越强**。
 *                                 ★★ PA5 在 STM32F103 上**没有任何定时器通道**
 *                                   （TIM2→PA15/PB3/PA2/PA3，TIM3→PB4/PB5/PB0/PB1，
 *                                    TIM1→PA8~PA11，TIM4→PB6~PB9），**做不了硬件 PWM**。
 *                                   所以这里用 TIM1 微秒时基做**软件 PWM**：100Hz（10 个
 *                                   1ms 片组成 10ms 周期），复用主循环原本那 10ms 空等，
 *                                   不额外占时间。100Hz 人眼看不出闪烁。
 *   PA6  - ADC1_IN6            **MQ-135 AO（气体浓度）** (2026-10-02 起; 原粉尘 VO)
 *   PA7  - GPIO 输入上拉       **MQ-135 DO（硬件阈值报警，低电平=超阈值）**
 *   PA8  - 输入上拉 (单总线)    **DHT_IN_ALT** —— DHT_IN_PA14=0 时仓内 DHT11 的数据线
 *                                 (原 GP2Y1014AU 粉尘 LED; 粉尘作废后释放, 2026-10-04 由 PB2 改来)
 *   PA9  - USART1_TX           虚拟终端 / 上位机 (协议帧 + ASCII 日志)
 *   PA10 - USART1_RX           虚拟终端 / 上位机 (下行命令注入)
 *   PA11 - GPIO 推挽           光诱惑虫灯 (植物补光灯)
 *   PA12 - GPIO 推挽           有源蜂鸣器
 *   PA13 - (随 SWJ 一起释放, 本设计未使用)
 *   PA14 - 单总线              **仓内** DHT11 (默认; 需全关 SWJ, 见下方说明)
 *   PA15 - TIM2_CH1 PWM        风机 FAN1   ← 原 JTAG JTDI
 *   PB0  - GPIO 输入上拉        **火焰传感器 DO（舱外火源检测 → 报警）** (2026-10-03 新增)
 *                                 ★ "有火"到底是高电平还是低电平由服务端定, 前端读不到 ⇒
 *                                   做成运行时变量 g_flame_alow, 串口发 'F' 现场翻;
 *                                   每次采集把**原始电平**打进串口, 拿火源靠近看它翻不翻即可定案
 *                                 ★ 原为 dht_cand[] 里的舱外 DHT11 候选脚, 已同步剔除
 *   PB1  - GPIO 推挽           超声波 TRIG (HC-SR04 IN)  ← 2026-10-03 从 PA2 让位而来
 *                                 ★ 曾长期在 dht_cand[] 扫描表里, 占位后**已同步剔除**
 *                                 ★ 2026-10-04: 北斗模块的 RX 也接这里 —— **不冲突**:
 *                                   北斗只往外发, 我们从不向它写, 那根线功能上不用;
 *                                   模块 RX 是**高阻输入**, 不会被 PB1 上的 TRIG 脉冲影响。
 *   PB2  - 输入上拉(软件串口RX) **北斗 BK_BDS 的 TX → PB2** (2026-10-04 新增)
 *                                 ★ 软件串口(bit-bang)收 NMEA, 位宽由 TIM1 微秒时基量(四-B 节)。
 *                                   为什么不用硬件 USART: USART1/2/3 已全占(终端/485/ESP)。
 *                                 ★ 它原是 DHT_IN_ALT 兼 dht_cand[] 候选脚 —— **已同步剔除**
 *                                   (单总线扫描会把这根线拉高拉低, 把 NMEA 数据流搅乱)。
 *   PB3  - TIM2_CH2 PWM        风机 FAN2   ← 原 JTAG JTDO
 *   PB4  - TIM3_CH1 PWM        风机 FAN3   ← 原 JTAG NJTRST
 *   PB5  - TIM3_CH2 PWM        风机 FAN4
 *   PB6  - GPIO 开漏(软 I2C)   OLED SCL
 *   PB7  - GPIO 开漏(软 I2C)   OLED SDA
 *   PB10 - USART3_TX           ESP8266 (AT 指令 / TCP 上传网页数据)
 *   PB11 - USART3_RX           ESP8266
 *   PB12 - GPIO 推挽           **报警灯 AlarmLight 信号线** ← 2026-10-03 从 PA3 让位而来
 *                                 (红→5V / 黑→PB12; 原 SK6812 数据线那根位置不变, 只是挪了脚)
 *                                 ★ 亮灯电平(高/低)由 ALARM_ACTIVE_LOW 定, 开机 alarm_probe() 两轮实测
 *                                 ★ 同样已从 dht_cand[] 剔除
 *   PC13 - 空闲 (**雨量 AO 接在这里, 但读不出模拟值** ——
 *                                 ★ STM32F103C8 的 ADC 通道只有 PA0~PA7/PB0/PB1,
 *                                   **PC13 不是 ADC 脚**; 本功能只需 DO 那一位, 故 AO 不用)
 *   PC14 - 输入上拉            **雨量传感器 DO** (BK_Raindrop 数字输出; 下雨 → 停全部风机)
 *   PC15 - 空闲 (原 RGB 状态灯 蓝。★ PC13~15 与 RTC 晶振复用, 禁止开 LSE)
 *
 *   ★★ 调试口(SWJ) 的取舍 —— 本版最需要留意的一处
 *     PA15/PB3/PB4 是 JTAG 脚 (JTDI/JTDO/NJTRST), PA14 是 SWD 的 SWCLK。
 *     要把它们当普通 IO / PWM 用, 就必须关掉对应的调试功能:
 *       SWJ_CFG=010 (SWJ_JTAGDisable) → 释放 PA15/PB3/PB4, **SWD 下载口保留**
 *       SWJ_CFG=100 (SWJ_Disable)     → 再释放 PA13/PA14, **下载/调试口就没了**
 *     4 路风扇固定用 PA15/PB3/PB4(没有替代脚), 所以关 JTAG 是必然;
 *     仓内 DHT11 接 PA14 则进一步要求关 SWD, 由 DHT_IN_PA14 决定:
 *       =1 (默认, 仿真平台用): 全关 SWJ。仿真平台是虚拟 MCU, 无任何影响;
 *                              ★ 但真实板子从此连不上 ST-Link, 详见该宏处的说明
 *       =0 (上实物板前改这个): 只关 JTAG、保住 SWD, 仓内 DHT11 顺延到 PA4
 *
 *   说明 (2026-10-01 方案调整): 原 3.5 寸 8080 并口彩屏 tftlcd3_5 占用 PB0~PB15
 *   全口，已取消；改用 i2c_oled_128x64_096 (SSD1306, 128x64 单色, I2C) 做本地
 *   信息显示 —— 软件 I2C 只用 PB6/PB7 两根线，PB 口整体释放。
 *   完整信息 (16x16 声速热力图 / 异常图 / 趋势 / 波形 / 通风控制) 改由 ESP8266
 *   经 USART3 上传，在 PC/手机浏览器打开网页端查看 (见 web_host/)。
 *
 *   4 路风扇 = 粮仓四个通风口, 由「仓内 vs 舱外」的温湿度差分级驱动 (见十二节
 *   alarm_update 的控制律): 差值越大 → 开的风机越多、每台转速越高。
 *   两台 DHT11 的分工: 舱外一路(脚自动扫描) + 仓内一路(固定 PA14)。
 *
 *   三串口分工: USART1 = 调试与协议帧 (虚拟终端可见, 可导出给 sim_bridge.py);
 *               USART2 = RS485 风速传感器专口 (Modbus RTU 主站, 9600 8N1, 见六-B 节);
 *               USART3 = ESP8266 专口 (AT 建网 + HTTP 响应), 三者互不干扰。
 *
 * 二、通信协议 (与 pc_host.py 严格一致)
 *   帧格式: 0xAA 0x55 | type | seq | total | len | data[len] | XOR
 *   上行: 0x01 VMAP(256B/2帧)  0x02 AMAP(256B/2帧)  0x04 LEVEL(4B)
 *         0x05 SUMMARY(7B)     0x07 ENV(8B)         0x13 VENT(16B)
 *         0x14 SOUND(13B)      0x16 ACK(2B)
 *   下行: 0xC5 | cmd
 *         0xA1 通风开  0xA2 通风关  0xA5 诱虫灯开  0xA6 诱虫灯关
 *         0xA7 蜂鸣    0xA9 自动    0xAA 手动
 *         0xD0/0xD1/0xD2 害虫等级注入 0/1/2 (本作品扩展)
 *         另: 虚拟终端直接发 ASCII '0'/'1'/'2' 也可注入虫害等级
 *
 * 三、声学层析 (单射线 + 开机自标定 + 环境场建模)
 *   超声波测得 TOF t，标定声程 L=200mm，则等效声速 v = 2000*L/t (m/s)。
 *   开机首次有效读数作为基线 v0，偏差 r = v/v0 - 1：
 *     r > 0 → 声速偏快 → 空洞/虫害通道；r < 0 → 声速偏慢 → 压实/受潮。
 *   16×16 网格由 r 驱动的旋转高斯场 + 温湿度/气体/粉尘修正合成，
 *   并按阈值分类为异常图 AMAP (0正常 1空洞 2受潮 3发热 4压实 5虫害 6发霉)。
 ******************************************************************************
 */

#include "sys.h"
#include "stm32f10x_conf.h"
#include "delay.h"
#include "adc.h"
#include <math.h>

/* font.h 在 HARDWARE/LCD/lcd.c 中也会被包含, 直接包含会与本文件产生
 * asc2_1608 的重复定义符号; 这里先重命名再包含, 使本翻译单元内的字库
 * 成为独立符号, 避免链接期 multiple definition 错误。 */
#define asc2_1608  gt_font_1608
#define asc2_1206  gt_font_1206
#include "font.h"
#undef asc2_1608
#undef asc2_1206

/*==============================================================================
 *                          一、全局配置
 *============================================================================*/

/* ---------- 功能开关 ---------- */
#define USE_ASCII_LOG       1     /* 1: USART1 同时输出可读 ASCII 状态行 (虚拟终端截图用) */
/* 超声波测距方式。2026-10-01 起**必须是 0**:
 * 4 台 PWM 风扇要用 PA15/PB3 (TIM2_CH1/CH2) 和 PB4/PB5 (TIM3_CH1/CH2),
 * TIM2 一旦做"部分重映射", CH1 就不在 PA0 上了 —— 原来的输入捕获收不到 ECHO。
 * 改成电平轮询(拿 TIM1 微秒时基量 ECHO 高电平宽度), 量程精度不受影响。 */
#define USE_TIM_CAPTURE     0
#define USE_WIND485         1     /* RS485 风速传感器 (ZTS_3000_FSJT 类, Modbus RTU 主站)。
                                   * ★ 它必须占**一路 USART** —— USART1/USART3 已分别给
                                   *   虚拟终端与 ESP8266, 而 C8T6 只有 3 路 USART, 于是
                                   *   用 USART2, 它的脚是**固定死的 PA2/PA3** ⇒ 原来压在
                                   *   这两根线上的超声波 TRIG 让到 PB1、报警灯让到 PB12,
                                   *   **接线要跟着改这两根**(见文件头引脚表)。
                                   * 0 = 整路摘除: 不采样 + 判据不参与 + 屏/网页/串口三处 --,
                                   *   同时也是 **Flash 没余量时的逃生舱**(这一路约省 1.5KB)。 */
/* 原 RGB_ACTIVE_LOW(共阳/共阴开关) 随 PC13~15 三色灯一并取消。—— 2026-10-02 起
 * 状态灯换成 PA3 上的 **AlarmLight 报警灯**(四-B 节): 单色、只有亮/灭, 但"哪种电平
 * 才算点亮"仍取决于模块怎么接, 所以这个开关以 ALARM_ACTIVE_LOW 的名字回来了
 * (开机 alarm_probe() 两种电平各驱动一次即可定案)。 */

/* ---------- 4 路 PWM 风扇 (BK_PWM_FAN_4) ----------
 * 4 台风机 = 粮仓的 4 个通风口, 由「仓内 vs 舱外」的温湿度差分级驱动 (见十二节)。
 *
 * 引脚/定时器通道是**唯一解**, 不能随手改 —— 平台 MCU 的 Timer 通道与引脚是硬绑定的:
 *   FAN1  PA15 = TIM2_CH1 ┐TIM2 部分重映射(CH3/CH4 仍留在 PA2/PA3,
 *   FAN2  PB3  = TIM2_CH2 ┘ 特意不用"全重映射", 否则 CH3/CH4 会跑到 PB10/PB11,
 *                           而那两根是 ESP8266 的 USART3 —— 复用脚会打架)
 *                           ★ 2026-10-03 补正: PA2/PA3 现在是 **USART2(485 风速)** 的脚
 *                             (原先这里写"是 ESP8266 的 USART3", 那是不对的)。
 *                             我们**只**用 TIM2 的 CH1/CH2, 从不配置 CH3/CH4, 所以不冲突;
 *                             但切记**别把 TIM2 全重映射打开**, 那会把 USART2/ESP8266 一起挤掉。
 *   FAN3  PB4  = TIM3_CH1 ┐TIM3 部分重映射(CH3/CH4 仍留在 PB0/PB1)
 *   FAN4  PB5  = TIM3_CH2 ┘
 * 这 4 根脚原本是 JTAG 口(PA15=JTDI / PB3=JTDO / PB4=NJTRST), 必须先把 JTAG
 * 关掉才能当普通复用脚用 —— 见 GPIO_AllInit() 的 SWJ 配置。
 *
 * PWM 频率 = 72MHz / (PSC+1) / (ARR+1) = 72M/72/1000 = **1kHz**。 */
#define FAN_USE_TIM_PWM     1     /* 1 = 硬件 PWM(AF 复用输出, 正道);
                                     0 = 软件 PWM(GPIO 位翻转兜底, 平台不模拟
                                         定时器输出时用, 见 fan_soft_cycle()) */
#define FAN_PWM_ARR         999   /* 自动重装值: 999 → 1000 级(0.1% 分辨率), 1kHz */

/* ---------- OLED (i2c_oled_128x64_096 / SSD1306) ----------
 * 128x64 单色, I2C 接口, 7 位地址 0x3C (8 位写地址 0x78)。
 * 用软件 I2C (GPIO 开漏 + 延时) 而非硬件 I2C1: 仿真平台对 GPIO 时序是可见的,
 * 软件 I2C 不依赖平台是否模拟 I2C 外设寄存器, 通用性最好。 */
#define OLED_HW_I2C         0     /* 0 = 软件 I2C (GPIO 位翻转, 默认);
                                     1 = 硬件 I2C1 (PB6/PB7 复用开漏, 400kHz) */
#define OLED_I2C_ADDR       0x78  /* 8 位写地址 (= 7 位 0x3C << 1), 软件 I2C 用 */
#define OLED_I2C_ADDR7      0x3C  /* 7 位地址, 硬件外设用 */
#define OLED_W              128
#define OLED_H              64
#define OLED_PAGES          (OLED_H / 8)      /* 8 页 */
#define OLED_BUF_SIZE       (OLED_W * OLED_PAGES)   /* 1024 字节显存 */
#define OLED_I2C_DELAY_US   2     /* 软件 I2C 半周期延时; 太快平台可能采不到 */

/* ---------- OLED 5 行文本的 y 坐标 ----------
 * 字高 12px, 屏高 64 → 正好 5 行 (L0 反白当标题栏)。
 * 定义放在这里(而不是 UI 那一节): 开机诊断屏在文件前部就要用到。 */
#define UI_L0               0
#define UI_L1               12
#define UI_L2               24
#define UI_L3               36
#define UI_L4               48

/* ---------- ESP8266 (USART3) ----------
 * 1: 开机走一遍 AT 建网 + 建立 TCP Server 流程 (真实硬件可直连浏览器访问),
 *    并把网页 JSON 通过 AT+CIPSEND 打出; 0: 完全不占用 USART3。 */
#define USE_ESP8266         1
/* ESP8266 要接入的 WiFi。
 * 平台的那个 ESP8266_01S 元件**没有配网入口**(面板里只有手册/简介), 所以 SSID 只能
 * 写死在代码里 —— 这里放两组, 开机按顺序逐个尝试, 命中即停:
 *   第 1 组 = 6103 / 61034148  ← 实际要连的热点 (2026-10-01 用户提供)
 *   第 2 组 = 平台 WIFISET 面板的默认值 (HTPL / 1234567890), 仅作兜底
 * 想只留一组就删掉第 2 组; 想再加就往后追加, 循环会自动多试一组。 */
#define WIFI_SSID           "6103"            /* 第 1 组 SSID (实际热点) */
#define WIFI_PASS           "61034148"        /* 第 1 组密码 */
#define WIFI_SSID2          "HTPL"            /* 第 2 组 SSID (兜底) */
#define WIFI_PASS2          "1234567890"      /* 第 2 组密码 */
#define WIFI_HTTP_PORT      80                /* 手机浏览器访问 http://<ip>/ */

/* ---------- 声学参数 ---------- */
#define PATH_MM             200.0f    /* 标定声程 (mm)，即超声波穿过粮堆的等效路径 */
#define GRID_N              16
#define GRID_PIX            (GRID_N * GRID_N)
#define TOMO_SIGMA          3.2f      /* 异常场高斯扩散半径 (格) */

/* ---------- OLED 单色语义 ---------- */
#define OLED_BLACK          0     /* 像素灭 */
#define OLED_WHITE          1     /* 像素亮 */
#define OLED_NORMAL         0
#define OLED_INVERSE        1

/* ---------- 上电自检 ---------- */
#define BOOT_SELFTEST       1     /* 1: 开机刷全屏→清屏→显示标题, 一眼确认 I2C 通路 */

/* ---------- DHT11 诊断屏 (确认接线无误后可改 0, 开机更快) ----------
 * 1: 开机先扫一遍所有空闲脚找 DHT11, 再停两屏:
 *      第 1 屏 结论屏: OUT/IN 温湿度 + 4 路风扇档位 + SWJ 取舍状态
 *      第 2 屏 明细屏: 逐个候选脚的实测应答宽度 + 最终选中的脚
 *    同样的内容会从 USART1 打印一份, 便于导出日志。 */
#define DHT_DIAG_MODE       1
#define DHT_DIAG_HOLD_MS    3000  /* 每屏停留时间 (ms) */

/*==============================================================================
 *                          二、引脚宏定义
 *============================================================================*/

/* ---- OLED (i2c_oled_128x64_096): 软件 I2C, SCL=PB6 / SDA=PB7 ----
 * 波形要能被平台的元件模型解析, 所以走最朴素的 GPIO 位翻转时序;
 * SDA 用「输出低 = 推挽拉低 / 输出高 = 切上拉输入释放总线」的经典做法,
 * 这样不依赖模块是否带外部上拉电阻, 平台内外都稳。 */
#define OLED_SCL_H()    (GPIOB->BSRR = GPIO_Pin_6)
#define OLED_SCL_L()    (GPIOB->BRR  = GPIO_Pin_6)

/* PB7 在 GPIOB->CRL 的 bits[31:28] */
#define OLED_SDA_OUT()  do{ GPIOB->CRL = (GPIOB->CRL & ~(0xFU << 28)) | (0x3U << 28); }while(0)
#define OLED_SDA_IN()   do{ GPIOB->CRL = (GPIOB->CRL & ~(0xFU << 28)) | (0x8U << 28); }while(0)
#define OLED_SDA_L()    do{ GPIOB->BRR  = GPIO_Pin_7; OLED_SDA_OUT(); }while(0)
#define OLED_SDA_H()    do{ GPIOB->BSRR = GPIO_Pin_7; OLED_SDA_IN();  }while(0)
#define OLED_SDA_R()    ((GPIOB->IDR & GPIO_Pin_7) ? 1 : 0)

/* 超声波: ECHO → PA0 (电平轮询量高电平宽度), TRIG → **PB1**
 * ★ 2026-10-03: TRIG 从 PA2 挪到 PB1 —— PA2/PA3 让给了 USART2 的 485 风速传感器。
 *   TRIG 是纯 GPIO 输出, 换脚零代价; 占位规矩照旧: **从 dht_cand[] 同步剔除 PB1**。 */
#define HC_TRIG_PORT    GPIOB
#define HC_TRIG_PIN     GPIO_Pin_1
#define HC_TRIG_H()     (HC_TRIG_PORT->BSRR = HC_TRIG_PIN)
#define HC_TRIG_L()     (HC_TRIG_PORT->BRR  = HC_TRIG_PIN)
#define HC_ECHO_IN()    ((GPIOA->IDR & GPIO_Pin_0) ? 1 : 0)

/* ---- 粉尘 GP2Y1014AU (2026-10-02 起**整路作废**: PA6 让给 MQ-135 的 AO) ----
 *   ★ 与"气体作废"完全同一条规矩: **作废 ≠ 读成 0**。DUST_ENABLE=0 后:
 *     ① 不再采样(dust_read() 与 PA8 的 LED 驱动**连同它们的宏**都不参与编译);
 *     ② 告警判据 / 通风判据 / 层析融合里摘掉;
 *     ③ 屏 / 网页 / 串口**三处一律显示 --**。
 *   代价: 平台没有气压/风压传感器, "气压异常"这条替代量原本是**气体 + 粉尘**两条腿,
 *   现在只剩气体一条。
 * ★ 这个开关**必须定义在粉尘那两个驱动宏之前**, 它们才能被 #if 一起关掉 ——
 *   否则"整路作废"只是不采样, 引脚定义还留着, 排障时又会把人带偏。 */
#define DUST_ENABLE     0
#if DUST_ENABLE
/* GP2Y1014AU 粉尘: LED → PA8 (低电平点亮), VO → PA6 (ADC1_IN6) */
#define GP2Y_LED_ON()   (GPIOA->BRR  = GPIO_Pin_8)
#define GP2Y_LED_OFF()  (GPIOA->BSRR = GPIO_Pin_8)
#endif

/* 执行器 */
#define LAMP_ON()       (GPIOA->BSRR = GPIO_Pin_11)    /* 光诱惑虫灯 */
#define LAMP_OFF()      (GPIOA->BRR  = GPIO_Pin_11)
#define BEEP_ON()       (GPIOA->BSRR = GPIO_Pin_12)    /* 有源蜂鸣器 */
#define BEEP_OFF()      (GPIOA->BRR  = GPIO_Pin_12)

/* 4 路 PWM 风扇 (BK_PWM_FAN_4): 引脚→定时器通道的对应关系见文件头「4 路 PWM 风扇」注释。
 * 用数组而不是 4 组宏, 是因为后面风扇循环/软件 PWM 都要按下标访问。 */
static GPIO_TypeDef *const fan_gpio[4] = { GPIOA,       GPIOB,      GPIOB,      GPIOB      };
static const uint16_t      fan_pin [4] = { GPIO_Pin_15, GPIO_Pin_3, GPIO_Pin_4, GPIO_Pin_5 };
/*            FAN1 PA15=TIM2_CH1   FAN2 PB3=TIM2_CH2   FAN3 PB4=TIM3_CH1   FAN4 PB5=TIM3_CH2 */

#if !FAN_USE_TIM_PWM
static void fan_pin_on (uint8_t i) { fan_gpio[i]->BSRR = fan_pin[i]; }   /* 仅软件 PWM 用 */
#endif
static void fan_pin_off(uint8_t i) { fan_gpio[i]->BRR  = fan_pin[i]; }

/* ---- 状态灯 二选一 (同一根信号线; 2026-10-03 起为 PB12) ---------------------
 *   LAMP_KIND = 1  AlarmLight 报警灯  ← 现行 (单色, 只有亮/灭, 靠闪烁节奏报等级)
 *   LAMP_KIND = 0  SK6812 单总线 RGBW 灯 (已弃用; 代码整段留在下面 #if 里, 随时可回)
 * 两种灯的驱动方式完全不同, 所以用一个开关切换, 而不是把旧代码删干净 ——
 * 旧灯珠那套是"猜平台时序", 新报警灯那套是"测电平极性", 都踩过坑, 留着有参考价值。 */
#define LAMP_KIND        1

/* ---- 报警灯 AlarmLight (信号线 → PB12; 2026-10-03 由 PA3 让位) --------------
 * 元件 `AlarmLight` 的模块底图右侧只有两根引线(红 / 黑), 本设计的接法:
 *        红 → 5V 电源符号        黑 → PB12 (2026-10-03 由 PA3 让位; 实物接线要挪)
 *
 * ★ 平台怎么判"亮灯": 前端**只画亮灯的贴图** ——
 *     index-*.js 里 alarmLight 组件的渲染就是
 *       `status === "1" ? <image href="led-*.png" 175x170/> : 空`,
 *     status 来自后端 `AlarmLight_type:"updateAlarmLight"` 下发的 alarmLight_status。
 *   也就是说"灯亮不亮"由**服务端 netlist 算出的电流**决定 —— 这是"负载跨在两个网络
 *   之间"的模型: 5V 那端是电源, 单片机这端**拉低**才有电流通过 ⇒ 默认低电平点亮。
 *   若实测相反, 把 ALARM_ACTIVE_LOW 改成 0 即可; 开机 alarm_probe() 会两种电平
 *   各驱动 2 秒并报轮次, 灯在哪一轮亮一目了然, 不用猜。
 *
 * ★ 另一种"看着像没亮"的情况: 平台**一个 status 都没下发**时, 连亮灯贴图都不画,
 *   屏上就只剩元件底图里那个暗灯罩(与 SK6812 那次的"黑罩"是同一回事)。
 *   ⇒ 看不出差别时先分清是"平台没下发"还是"极性反了", 别急着改接线。
 *   (接线也可以自查: 那根信号线落在模块右侧引线的外端, 另一端接 5V 电源符号) */
#define ALARM_ENABLE     1                /* 0 = 报警灯关闭(其它功能不受影响) */
#define ALARM_PORT       GPIOB
#define ALARM_PIN        GPIO_Pin_12      /* ★ 2026-10-03 从 PA3 让位给 USART2_RX(485 风速-A)。
                                           *   接线要跟着挪: 那根信号线现在接 **PB12**
                                           *   (红/5V 那端不动)。极性判据与排障办法一字不变。 */
#define ALARM_ACTIVE_LOW 1                /* 1 = 单片机拉低点亮(5V 在另一端, 有电流通路);
                                             0 = 拉高点亮 */
#define ALARM_SET_ON()   do{ if (ALARM_ACTIVE_LOW) ALARM_PORT->BRR  = ALARM_PIN; \
                             else                  ALARM_PORT->BSRR = ALARM_PIN; }while(0)
#define ALARM_SET_OFF()  do{ if (ALARM_ACTIVE_LOW) ALARM_PORT->BSRR = ALARM_PIN; \
                             else                  ALARM_PORT->BRR  = ALARM_PIN; }while(0)

/* 闪烁节奏: 正常 = 灭; 预警 = 慢闪 500ms; 报警 = 快闪 150ms。
 * 单色灯只有"亮/灭"两态, 用**节奏**表达等级 —— 而且闪起来比常亮更抓眼, 现场演示更明显。 */
#define ALARM_SLOW_MS    500u
#define ALARM_FAST_MS    150u

/* =========================== 光敏电阻 + 光控补光灯 (2026-10-02) ===========================
 *
 * 需求: "加一个光敏电阻模块, ADC 接 PA4, 通过感知舱外光照强弱来控制灯光",
 *       "补光灯信号线接 PA5"。
 *
 * ---- 一、光敏电阻模块 (photosensitive_resistance) → PA4 / ADC1_IN4 ----
 * ★ 这个元件是**主动型模拟源**: 从平台前端分块里读到它的组件实现 ——
 *     watch: photosensitive_resistance_adc → {
 *         send({ type:"analog_type", id:`${node}.ADCx_IN${ch}`, data: 该元件的 voltage })
 *     }
 *   也就是**前端自己算出它接在哪个 ADC 引脚, 再把电压直接灌进仿真**。
 *   ⇒ 固件**一行协议代码都不用写**, 直接读 ADC 通道 4 就拿到光照电压。
 *     (同一机理的还有 potentiometer 电位器、adc 滑条 —— 都是"前端灌 ADC"型)
 *
 * ★ 但"电压 → 光照强弱"的**方向**读不到: 电压是服务端(photosensitive_resistance_adc)
 *   算出来的, 前端只负责转发 ⇒ **跟报警灯一样: 不猜, 实测**。
 *   判别办法: 把光敏模块面板上的滑条拖到 0% 和 100%, 各看一次串口的 raw/mV
 *   (每次采集都会打 [GT] LGT raw=… mv=… ) —— 哪个端电压高就是哪端亮。
 *   若"0%(最暗)反而电压高", 把 LIGHT_INVERT 改成 1 即可, 一行的事。
 *
 * ---- 二、补光 LED (普通 LED, 阴极接地 / 阳极串 10K 接 PA5, 软件 PWM 调光) ----
 * ★ 这个元件与报警灯**完全同构**, 也是被动两态负载:
 *     前端组件 methods:{} 是**空的、没有任何 send**;
 *     渲染只有一句 `plant_light[名字].data === 1 ? <image href="lightUp-*.png"/> : 空`
 *   ⇒ 亮不亮由**服务端 netlist 算出的电流**决定(跟报警灯同一个模型),
 *     且**没收到下发时连亮灯贴图都不画**, 只剩元件底图(即"看着像黑的")。
 *   ⇒ 报警灯仍要**开机实测极性**(2 轮); 而这盏补光 LED 电路明确(阴极接地,
 *     阳极经电阻接 PA5) ⇒ **高电平点亮**, 不必扫极性, 开机只做"能亮/能灭"自检。
 *
 * ★ 代价: 它占的是 PA5 —— **原来 MQ-135 气体传感器的 AO(ADC1_IN5)**。
 *   ★ 2026-10-02 再调整: MQ-135 的 **AO 改接 PA6**(原粉尘 VO), **DO 接 PA7**,
 *     于是气体这一路**恢复**; 而粉尘整路**作废**(DUST_ENABLE=0), 让出 PA6。
 *     三处显示与判据的口径与"气体作废"时完全一致: 不采样 + 判据摘掉 + 显示 -- 。
 *   按"绝不给看起来正常的兜底值"的规矩, 作废不是"给 0", 而是**整路标记为不可用**:
 *   GAS_ENABLE=0 → 不再采样、告警判据里摘掉, 且**屏/网页/串口三处一律显式 --**。
 *   想把这路要回来: 把 GAS_ENABLE 改回 1, 同时 MQ-135 的 AO 必须挪到别的空 ADC 脚
 *   (PA5 已经归补光灯了), 并把 GLAMP_ENABLE 改 0。
 * ====================================================================================== */

/* ---- 光敏电阻 (舱外光照) ---- */
#define LIGHT_ENABLE     1
#define LIGHT_ADC_CH     ADC_Channel_4     /* PA4 = ADC12_IN4 (用户指定) */
#define LIGHT_AVG_N      8                 /* 采样次数(取平均, 与 MQ-135 同口径) */
#define LIGHT_MV_FULL    3300u             /* 满量程电压 mV (3.3V 参考) */
#define LIGHT_ADC_FULL   4095u             /* 12 位 ADC 满量程(仅备查: 定标后不再用它做除数) */
/* ★★ 2026-10-04 定标端点 (raw)。平台把滑条百分比经**服务端模型**换算成电压再送进来,
 *   各元件满量程并不相同(实测 MQ-135 满偏只有 1287), 所以**不能**再拿 4095 硬除 ——
 *   那正是"滑条拉满、网页只显示 33%"的根因。
 *   现场用串口命令锁定真实端点: q=暗端(0%)  r=亮端(100%)   ← 见 rx_poll
 *   ★ 下面 1287 是**暂借** MQ-135 的实测值, 光敏必须按上面步骤标定一次才准。 */
#define LIGHT_ADC_LO     0u                /* 滑条最暗处的 raw */
#define LIGHT_ADC_HI     1287u             /* 滑条最亮处的 raw (★ 待实测标定) */
#define LIGHT_INVERT     0                 /* 0 = 电压越高越亮; 1 = 电压越高越暗
                                            * ★ 方向在服务端, 前端读不出来 ⇒ 实测后定,
                                            *   判别方法见上面"一、"那段注释 */
#define LIGHT_DARK_PCT   30                /* ★ 改成**连续调光**后, 此阈值不再作为开关(duty = 100 − light_pct);
                                    *    仅保留备查: 若要改成"暗到某一档才开始补光"的分段式, 改这里
                                            * (阴雨天/夜间补光; 阈值按现场需要调) */

/* ---- 补光 LED (PA5, 软件 PWM 调光) ---- */
#define GLAMP_ENABLE     1                 /* 0 = 补光 LED 整路关闭 (PA5 交回普通 IO) */
#define GLAMP_PORT       GPIOA
#define GLAMP_PIN        GPIO_Pin_5        /* 补光 LED: 阳极经 10K 电阻接本脚, 阴极接地 */
#define GLAMP_ACTIVE_HIGH 1                /* 1 = **高电平点亮** (本接法就是它: 阴极接地,
                                            *   阳极经限流电阻接 PA5, 只有 PA5 输出高才有电流)。
                                            * ★ 现场若发现常亮/不亮, 发字符 'P'(等价 0xC5 0xB9)
                                            *   即可运行时翻转, 试准后再改这里固化。 */

/* ---- 补光 LED 的软件 PWM ----
 * ★★ PA5 在 STM32F103 上**没有任何定时器通道** —— TIM2 的通道在 PA15/PB3/PA2/PA3,
 *    TIM3 在 PB4/PB5/PB0/PB1, TIM1 在 PA8~PA11, TIM4 在 PB6~PB9, 没有一个是 PA5。
 *   所以**硬件 PWM 不可能**, 只能用 TIM1 微秒时基做软件 PWM。
 * 做法: 每个主循环节拍本来就空等 10ms, 把这段时间切成 GLED_PWM_SLICES 个 1ms 片,
 *   前 n 片输出"亮"电平、其余输出"灭"电平 —— 于是
 *     PWM 频率 = 1 / 10ms = 100Hz (人眼看不出闪烁)
 *     占空比分辨率 = 10 级 (每级 10%)
 *   **不额外占用时间**: 原本 delay_ms(10) 也是空等, 现在把空等换成带输出的忙等。
 * 注意: 用 us_dly 而不是 delay_us —— 后者在平台上是桩(可能瞬间返回), us_dly 走
 *   TIM1 计数且带守卫(计数器停摆会自动退回并置 g_us_ok=0), 不会死等。 */
#define GLED_PWM_SLICES  10u               /* 每个 10ms 节拍切成几个 PWM 片 (10 → 100Hz) */
#define GLED_DUTY_MAX    100u              /* 占空比上限 (%) */
#define GLAMP_FORCE_MS   5000u             /* 手动强制的最长保持时间, 超时自动回 AUTO */

/* ★ 光照读不到时灯不再"死黑", 而是**慢闪 500ms** 当 noData 指示; 下面这个周期
 *   常量只影响"闪多快", 不影响任何判断逻辑。 */
#define GLAMP_BLINK_MS   500u

/* ★ 驱动**一律走 glamp_drive()**, 它读运行时变量 g_glamp_ahigh 而不是读宏。
 *   这里**故意不再保留 GLAMP_SET_ON/OFF 宏** —— 留着宏就一定会有人去用它,
 *   一用就绕过运行时极性, 排障时又得再往平台传一遍固件。 */

/* ---- SK6812 单总线 RGBW 状态灯 (LAMP_KIND=0 时才编译; 已弃用, 仅备查) --------
 * 原 PC13/PC14/PC15 三路开关灯只能出 8 种颜色(每个通道非亮即灭), 换成单总线
 * 全彩灯珠后可以按 0~255 精确给出颜色。单颗灯 32 位, 依次发 R/G/B/W 各 8 位,
 * 级联时前一颗把剩余数据串给下一颗。
 * 位时序由 sk_tm 指向哪一套决定(见"四-B"节)。**具体参数在 SK_TRY 表里**:
 *   真实灯珠规格 800kHz(T0H≈0.30µs / T1H≈0.60µs / 位周期 1.25µs / 复位 >80µs)
 *   要在发送期间把 US_TIM 临时切 72MHz; 放大到 µs 级则用 1MHz 时基即可。
 *   ★ 仿真平台把这两个码判成一样宽(0.3µs 低于它的分辨率) -> 32 位全 1 -> 灯发纯白;
 *     而放大到 3/20µs 又会被平台整帧丢弃(前端连彩色圆都不画)。两头都不对,
 *     所以开机做**时序扫描**(sk_lamp_sweep)现场找平台认的那一套。 */
#define SK6812_ENABLE   1                 /* 0 = 本灯关闭 (PC13~15 已释放, 不会自动退回) */
#define SK_PORT         GPIOB
#define SK_PIN          GPIO_Pin_12       /* DIN = PB12 (随报警灯一起让位; 原为 PA3) */
#define SK_NUM          1                 /* 级联灯珠数; 只接 1 颗就保持 1 */
#define SK_ON()         (SK_PORT->BSRR = SK_PIN)
#define SK_OFF()        (SK_PORT->BRR  = SK_PIN)

/* ---- MQ-135 气体传感器 (2026-10-02 **恢复**, 且换了脚) ----
 * 现: AO → **PA6 (ADC1_IN6)**, DO → **PA7** (低电平=超阈值)。
 *   ★ 为什么换脚: PA6 原来是粉尘 GP2Y1014AU 的 VO。用户要 MQ-135 的 AO 接 PA6,
 *     并选择了"粉尘整路作废" —— 于是 PA6 归气体, 粉尘那一路按同一条规矩**整路停用**
 *     (DUST_ENABLE=0: 不采样 + 判据摘除 + 三处显示 --), 而不是"读成 0 再拿去比阈值"。
 *   ★ 顺带: PA5 上的补光灯也就不用再和气体抢脚了; PA8(原粉尘 LED)释放为空闲。 */
#define GAS_ENABLE      1
#define MQ_DO_ALARM()   ((GPIOA->IDR & GPIO_Pin_7) == 0)

/* 气体这一路的开关在"八、MQ-135"那一段(DUST_ENABLE 已提到本文件靠前处定义)。 */

/* ---- 火焰传感器 (Flame Sensor, 2026-10-03 新增) ----
 * 模块供电 3.0~5.0V; 这里只用它的 **DO**(板上比较器的数字输出) → **PB0**,
 * 用来检测**舱外**是否有火源, 检出即参与报警(最高档)。
 *   ★ PB0 原本在 dht_cand[] 里(舱外 DHT11 的备选扫描脚)。现给火焰传感器后
 *     **同步从扫描表剔除** —— 单总线扫描会把这个脚拉高拉低, 既干扰 DO 读数,
 *     也可能被误当成 DHT 的应答脉冲。
 *   ★ "有火"是高电平还是低电平**不猜**: 平台的判据在服务端, 前端只收已解好的值。
 *     默认 1 = 低电平=检测到火焰(KY-026 一类模块最常见的接法);
 *     实测相反时串口发 'F'(等价 0xC5 0xBA) 当场翻转, 一次编译试全两种组合。 */
#define FLAME_ENABLE     1
#define FLAME_PORT       GPIOB
#define FLAME_PIN        GPIO_Pin_0
#define FLAME_ACTIVE_LOW 1              /* 1 = DO 低电平=检测到火焰; 0 = 高电平=有火 */
/* 原始电平(与极性无关, 排障就看它): 1 = PB0 为高 */
#define FLAME_RAW()      ((uint8_t)((FLAME_PORT->IDR & FLAME_PIN) ? 1u : 0u))

/* ---- 雨量传感器 BK_Raindrop (DO → PC14; 2026-10-04 新增) --------------------
 *  用途: 检测是否下雨 —— 有雨就**强制停掉全部风机**(下雨时舱外是高湿空气,
 *        开风机等于往仓里灌潮, 属于帮倒忙)。
 *  ★ 只取 **DO**(数字量): 模块的 AO 接在 PC13, 而 PC13 **不是 ADC 脚**
 *    (C8T6 的 ADC 通道只有 PA0~PA7 / PB0 / PB1), 读不出模拟值 —— 好在本功能
 *    只需要"下没下雨"这一位, AO 那根线可以不管。
 *  ★ "有雨"是高电平还是低电平由服务端定, 前端读不到 ⇒ 与火焰传感器同一套办法:
 *    做成运行时变量 g_rain_alow, 串口发 'R'(**大写**, 小写 r 被光照标定占了) 现场翻;
 *    每次采集把**原始电平**同时打进串口与网页, 拿水/湿布靠近看它翻不翻即可定案。 */
#define RAIN_ENABLE      1
#define RAIN_PORT        GPIOC
#define RAIN_PIN         GPIO_Pin_14
static uint8_t g_rain_raw  = 0;      /* PC14 原始电平: 1 = 高 (与极性无关, 排障就看它) */
static uint8_t g_rain_on   = 0;      /* 1 = 按当前极性判为"正在下雨" */
static uint8_t g_rain_alow = 1;      /* 1 = DO 低电平 = 下雨; 0 = 高电平 = 下雨 */
#define RAIN_RAW()       ((uint8_t)((RAIN_PORT->IDR & RAIN_PIN) ? 1u : 0u))

/* ---- 北斗定位 BK_BDS (模块 TX → PB2, 软件串口 RX; 2026-10-04 新增) -----------
 *  用途: 灾害现场/粮仓实时定位, 网页端显示并带动画。
 *  ★ 平台这个模块**会真的吐数据**: 设置面板可填纬度(N/S)/经度(E/W), 默认
 *    北京 4002.229934N / 11618.096855E, 并每 2s 更新一次 time 字段
 *    (见平台分块 index-e551b627.js 的 BK_BDS_SET 组件)。所以模块 TX 上有连续数据流。
 *  ★ C8T6 的三路硬件 USART 全占(USART1 终端/协议帧、USART2 485、USART3 ESP8266)
 *    ⇒ 只能用**软件串口**(bit-bang)在 PB2 上收; 位宽拿 TIM1 微秒时基量(四-B 节)。
 *  ★ 只做**接收** —— 北斗只往外发, 我们不向它写任何东西, 所以模块的 RX(PB1)不用接,
 *    也就与 PB1 上的超声波 TRIG 天然不打架(UART 接收端是高阻输入)。
 *  ★ 波特率不一定对得上: 串口发 'B'(大写) 在 4800/9600/19200/115200 间循环试;
 *    发 'N'(大写) 把最近一行 NMEA 原文打出来看有没有可读文本。 */
#define BDS_ENABLE       1
#define BDS_PORT         GPIOB
#define BDS_PIN          GPIO_Pin_2
#define BDS_LINE_MAX     100
#define BDS_RAW()        ((uint8_t)((BDS_PORT->IDR & BDS_PIN) ? 1u : 0u))

/* 解析结果 —— **没收到就显式无效**, 绝不给一个编造的坐标 (与温度不给 25.0℃ 同理) */
static uint8_t  g_bds_state = 0;     /* 0=一行都没收到 1=收到 NMEA 但未定位 2=已定位 */
static int32_t  g_bds_lat   = 0;     /* 纬度 ×1e6 (绝对值) */
static int32_t  g_bds_lon   = 0;     /* 经度 ×1e6 (绝对值) */
static uint8_t  g_bds_ns    = 0;     /* 0 = N, 1 = S */
static uint8_t  g_bds_ew    = 0;     /* 0 = E, 1 = W */
static uint16_t g_bds_lines = 0;     /* 收到的"完整且以 $ 开头"的 NMEA 行数 */
static uint32_t g_bds_baud  = 9600u; /* 当前波特率 (串口 'B' 循环切换; 体检可自动定) */
static uint16_t g_bds_badsum = 0;    /* 校验和不对的行数 */
static char     g_bds_last[BDS_LINE_MAX] = {0};   /* 最近一行原文 ('N'/网页看; 已洗成安全字符) */

/* ---- 线路体检 (回答"模块到底有没有往这根线上发") ----------------------------
 * ★ 为什么必须做: 收不到数据时, "线没接/模块没往外发" 与 "我们波特率/极性收错了"
 *   是两件完全不同的事, 页面只报一句"未收到"根本分不开。这里直接量线上的电平行为
 *   (翻不翻、一位多宽、高占比多少), 一句话就能定案 —— 与"先分层"同一条规矩。
 * ★ 这些量全部**无条件保留**: 页面 JSON 的 bd/bl 字段要读它们, 不能藏进 #if。 */
static uint8_t  g_bds_inv    = 0u;   /* 1 = 线路反相 (空闲=低电平) */
static uint8_t  g_bds_manual = 0u;   /* 1 = 人手动定过波特率; 体检不再自动改 */
static uint16_t g_bds_edges  = 0u;   /* 累计电平翻转次数 (0 = 这根线一直不动) */
static uint16_t g_bds_bytes  = 0u;   /* 累计收到的字节数 (含没成行的残段) */
static uint16_t g_bds_lo_min = 0u;   /* 观察到的最短**低**电平 (µs); 0 = 还没测到 */
static uint16_t g_bds_hi_min = 0u;   /* 观察到的最短**高**电平 (µs) */
static uint32_t g_bds_hi_n   = 0u;   /* 体检期间采样到高电平的次数 */
static uint32_t g_bds_all_n  = 0u;   /* 体检期间总采样次数 */
static uint32_t g_bds_baud_m = 0u;   /* 由最短脉宽反推的波特率 (0 = 还没测出) */
static uint32_t g_bds_pb_ms  = 0u;   /* 上次体检的时刻(millis), 控制体检节奏 */

/* 线路的**有效电平**(已按极性解释) —— 接收器一律用它。
 * 反相的模块(空闲=低)靠 g_bds_inv 翻回来, 不用改接线。 */
#define BDS_LEV()   ((uint8_t)(g_bds_inv ? (BDS_RAW() ^ 1u) : BDS_RAW()))

/* DHT11 数据线 (单总线) —— 引脚可换, 开机能自动认
 * 引脚用"编号"表示: 低 4 位 = 引脚号, bit4 = 端口 (0=PA, 1=PB)。
 *   DHT_PA(3) = PA3 ;  DHT_PB(5) = PB5
 * 为什么要可换 + 自动探测: 接线是人接的, 固件里写死一根脚经常和实际不符 ——
 * 表现就是屏上温湿度永远 --.-, 人却在怀疑代码。开机把所有空闲脚都做一次完整
 * 握手, 哪个脚应答就用哪个脚, 并把结果明明白白报到 OLED/串口/网页上,
 * 而不是悄悄用一个写死的"假温度"顶上。 */
#define DHT_PA(n)          ((uint8_t)(n))
#define DHT_PB(n)          ((uint8_t)(0x10u | (n)))
#define DHT_PIN_DEFAULT    DHT_PA(1)   /* 舱外 DHT11 默认脚: PA1
                                        * ★ 本默认值原为 PA3; 现已让给 PA3 上的状态灯
                                        *   (SK6812/报警灯, 四-B 节)并改到 PA1,
                                        *   PA3 已从 dht_cand[] 剔除 —— 扫描不会再碰它。
                                        *   注: 这是**代码默认值**, 与实际接线无关(本项目接 PA1) */
static uint8_t g_dht_pin = DHT_PIN_DEFAULT;

/* ---- 第 2 路 DHT11: 仓内温湿度, 固定接 PA14 (2026-10-01 用户指定) ----
 * 分工: 这一路模拟**仓内**, 上面那一路(自动扫描)模拟**舱外**;
 *       两者的温差/湿差就是 4 路风扇的驱动依据(见十二节 alarm_update)。
 *
 * ★ PA14 = SWCLK。要把它当普通 GPIO, 必须把**整个 SWJ(JTAG + SWD)** 关掉
 *   (GPIO_Remap_SWJ_Disable, 即 SWJ_CFG = 100) —— 只关 JTAG(SWJ_CFG=010) 是
 *   不够的, 那样 PA14 仍是 SWCLK。
 *   代价: **真实硬件上关掉 SWD 后 ST-Link 就再也连不上芯片**(下载/调试全断),
 *   只能把 BOOT0 拉高进系统存储器启动、用串口 ISP 重烧才能救回来。
 *   仿真平台没有这个顾虑(虚拟 MCU 不检查 SWJ), 所以这里默认开。
 *   ★ 上实物板之前请把 DHT_IN_PA14 改 0 —— 那时数据线改到 DHT_IN_ALT,
 *     一个脚的事, 却能把"把自己锁在芯片外面"的风险彻底避免。
 *
 * ★ 2026-10-02: DHT_IN_ALT 从 **PA4 改到 PB2** —— 因为 PA4 已让给光敏电阻模块的
 *   ADC(见九节 light_read)。为什么不是 PA13: DHT_IN_PA14=0 时只关 JTAG、SWD 仍占着
 *   PA13(SWDIO), 它并不空闲; 而 PC13~15 要把 dht_gpio() 扩到第三个端口才能用。
 * ★ 2026-10-04: 再 **PB2 → PA8** —— PB2 归了**北斗软串口 RX**(见上), 不能再当
 *   DHT 备用脚。PA8 原本随粉尘整路作废而释放, 空闲、也不受 SWJ 影响。 */
#define DHT_IN_PA14        1           /* 1 = 仓内 DHT11 接 PA14 (需全关 SWJ); 0 = 接 DHT_IN_ALT */
#define DHT_IN_ALT         DHT_PA(8)   /* DHT_IN_PA14=0 时的替代脚 (2026-10-04 由 PB2 改来) */
#if DHT_IN_PA14
  #define DHT_IN_PIN       DHT_PA(14)
#else
  #define DHT_IN_PIN       DHT_IN_ALT
#endif

static uint8_t g_dht2_pin = DHT_IN_PIN;   /* 仓内 DHT11 的脚 (固定, 不参与扫描) */

static GPIO_TypeDef *dht_gpio(uint8_t id) { return (id & 0x10u) ? GPIOB : GPIOA; }
static uint8_t       dht_num(uint8_t id)  { return (uint8_t)(id & 0x0Fu); }

/* 推挽输出 50MHz */
static void dht_pin_out(uint8_t id)
{
    GPIO_TypeDef *G  = dht_gpio(id);
    uint8_t       p  = dht_num(id);
    uint8_t       sh = (uint8_t)((p & 7u) * 4u);
    volatile uint32_t *cr = (p < 8) ? &G->CRL : &G->CRH;
    *cr = (*cr & ~(0xFU << sh)) | (0x3U << sh);
}

/* 上拉输入 (释放总线, 靠上拉保持高) */
static void dht_pin_in(uint8_t id)
{
    GPIO_TypeDef *G  = dht_gpio(id);
    uint8_t       p  = dht_num(id);
    uint8_t       sh = (uint8_t)((p & 7u) * 4u);
    volatile uint32_t *cr = (p < 8) ? &G->CRL : &G->CRH;
    G->BSRR = (uint32_t)1u << p;                      /* 先写 ODR=1 -> 上拉 */
    *cr = (*cr & ~(0xFU << sh)) | (0x8U << sh);
}

static uint8_t dht_get(uint8_t id) { return (uint8_t)((dht_gpio(id)->IDR >> dht_num(id)) & 1u); }
static void    dht_set(uint8_t id) { dht_gpio(id)->BSRR = (uint32_t)1u << dht_num(id); }
static void    dht_clr(uint8_t id) { dht_gpio(id)->BRR  = (uint32_t)1u << dht_num(id); }

/*==============================================================================
 *                          三、全局运行状态
 *============================================================================*/

typedef struct {
    /* 传感器 —— 舱外 DHT11 (脚由开机扫描自动认, 默认 PA1) */
    int16_t   temp_x10;      /* 舱外温度 ×10 (℃) */
    int16_t   rh_x10;        /* 舱外湿度 ×10 (%RH) */
    uint8_t   dht_ok;        /* 1 = 舱外 DHT11 至少成功读出过一次; 0 = 从未读到(显示 --.-) */
    uint8_t   dht_ever;      /* 1 = 历史上有过有效读数 (本次可能暂时失败, 值仍可用) */
    /* 传感器 —— 仓内 DHT11 (固定 PA14, 见 DHT_IN_PA14) */
    int16_t   in_temp_x10;   /* 仓内温度 ×10 (℃) */
    int16_t   in_rh_x10;     /* 仓内湿度 ×10 (%RH) */
    uint8_t   dht_in_ok;     /* 1 = 仓内 DHT11 读到; 0 = 没读到(显式报错, 不给兜底值) */
    uint8_t   dht_in_ever;   /* 1 = 历史上有过有效读数 */
    uint16_t  dust_ug;       /* 粉尘浓度 (µg/m³); DUST_ENABLE=0 时恒 0 且三处显示 -- */
    uint16_t  gas_pct_x10;   /* 气体浓度 (‰，即 0~1000); GAS_ENABLE=0 时恒 0 且三处显示 -- */
    uint16_t  gas_adc;       /* MQ-135 AO 原始 ADC 码 (定标用, 串口 graw= 打印) */
    uint8_t   gas_do;        /* MQ-135 DO: 1=超阈值 */
    uint8_t   gas_ok;        /* 1 = 本次 AO 读数有效; 0 = ADC 超时 → 三处显示 --, 不参与判据。
                              *   ★ 与 light_ok 同一口径 —— MQ-135 没接时 AO 本来就接近 0V,
                              *     若照样显示 "0.0‰", 就把"没接/读失败"伪装成"空气干净"了。 */

    /* 火焰传感器 (Flame Sensor DO → PB0, 检测舱外火源) —— 2026-10-03 新增
     * ★ 这里**故意没有 flame_ok**: DO 是"一位"数字线, 平台不下发状态 与
     *   "线上真是低电平"在电气上无法区分(不像 MQ-135 能用 AO 的 ADC 超时判无效)。
     *   所以不假装能自诊断, 而是把**原始电平 flame_raw 一路报出去**(屏/串口/网页
     *   三处都给 DO 的实际电平), 让"线接没接上、有没有翻转"由人当场核对 ——
     *   宁可暴露不确定性, 也不编一个看起来正常的"设备状态正常"。 */
    uint8_t   flame_raw;     /* PB0 原始电平: 1 = 高, 0 = 低 (与极性无关) */
    uint8_t   flame;         /* 1 = 按当前极性判为"检测到火源" */

    /* 光敏电阻模块 (舱外光照, PA4/ADC1_IN4) —— 见九节 light_read() */
    uint16_t  light_adc;     /* ADC 原始码 0~4095 (每次采集都从串口打出, 用来标定方向) */
    uint16_t  light_mv;      /* 换算电压 mV */
    uint8_t   light_pct;     /* 光照强度 0~100 (%) */
    uint8_t   light_ok;      /* 1 = 读到有效光照; 0 = 无效(显示 --, 且不参与补光决策) */
    uint8_t   glamp_on;      /* 补光 LED(PA5)当前状态: 1 = 占空比 > 0 (即"亮着") */
    int32_t   tof_us;        /* 超声波飞行时间 (µs)，<0 表示超时无效 */
    uint16_t  dist_mm;       /* 测距结果 (mm) */
    /* RS485 风速传感器 (ZTS_3000_FSJT 类, Modbus RTU) —— 2026-10-03 新增, 见六-B 节
     * ★ 与火焰同一口径: **接不到就报 --**, 不给"看起来正常"的兜底风 ——
     *   所以这里有一个显式的 wind_ok, 而不是"读不到就当 0 m/s 无风"。 */
    int16_t   wind_x10;      /* 风速 ×10 (m/s) = 保持寄存器原值 (量程 0~600 ⇒ 0~60.0 m/s) */
    int32_t   wind_press_x10;/* 动压 ×10 (Pa): q = 0.5*ρ*v², ρ=1.2 ⇒ q = 0.6·v² (由风速算得) */
    uint16_t  wind_raw;      /* 本次读到的寄存器原值 (与平台元件上显示的数对照用) */
    uint8_t   wind_ok;       /* 1 = 本次 Modbus 应答三样全对; 0 = 无应答/校验错 -> 三处显示 -- */

    /* 层析 */
    float     v_meas;        /* 等效声速 (m/s) */
    float     v_base;        /* 开机自标定基线声速 (m/s) */
    float     r_dev;         /* 声速相对偏差 (v/v0 - 1) */
    uint8_t   vmap[GRID_PIX];/* 声速图 (0~255 归一化) */
    uint8_t   amap[GRID_PIX];/* 异常图 (0~6) */
    uint8_t   main_anom;     /* 主异常类型 */
    uint16_t  conf_x1000;    /* 置信度 ×1000 */
    float     phase;         /* 扫描相位 (rad) */

    /* 决策 */
    uint8_t   pest;          /* 害虫等级 0/1/2 (上位机/终端注入) */
    uint8_t   alarm;         /* 报警等级 0正常 1轻度 2重度 */
    uint8_t   lamp_on;       /* 诱虫灯 */
    uint8_t   vent_auto;     /* 自动通风标志 */
    uint8_t   fan_pct;       /* 通风总强度 (0~100) = 4 台中的最大值, 上报/显示用 */
    uint8_t   fan[4];        /* 4 路风扇各自 PWM 占空比 0~100 (FAN1~FAN4) */
    uint8_t   fan_on;        /* 已启用的风扇台数 0~4 */
    uint8_t   vent_why;      /* 通风原因 0=不需要 1=温差 2=湿差 3=仓内超标 4=手动 */

    /* 时间 */
    uint32_t  ms;
} SysState;

static SysState g;

/* WiFi 联网状态 (OLED 标题栏与网页共用; 定义在此处供 ui_draw 引用)
 * 两级状态, 都只报实测结果、**不给"看起来正常"的兜底值**:
 *   g_esp_at   = 模块对 AT 有没有应答 (0 = 一个字都没回 = 模块没通或没上电)
 *   g_wifi_ok  = 有没有真的连上某一组 SSID
 * 于是屏上右上是三态之一:
 *   WIFI:OK  模块应答 + 联网成功
 *   WIFI:AT  模块在说话, 但没进任何路由器 (模块可能处在自身 AP 模式)
 *   WIFI:--  模块完全没应答 —— 与 DHT11 的 --.- 同理, 不许伪装成 ON */
static uint8_t g_esp_at   = 0;
static uint8_t g_wifi_ok  = 0;
static uint8_t g_wifi_idx = 0xFF;    /* 命中的 SSID 组号; 0xFF = 一组都没连上 */

static const char *wifi_str(void)
{
    if (!g_esp_at)  return "WIFI:--";
    if (!g_wifi_ok) return "WIFI:AT";
    return "WIFI:OK";
}

/*==============================================================================
 *                          四、时间基准 (TIM4 自由运行，无中断)
 *============================================================================*/
static uint16_t g_last_cnt = 0;
static uint8_t  g_tb_ok = 0;      /* TIM4 是否已正常计数 (部分仿真平台不模拟 TIM4) */

static void TimeBase_Init(void)
{
    TIM_TimeBaseInitTypeDef t;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    TIM_TimeBaseStructInit(&t);
    t.TIM_Period        = 65535;
    t.TIM_Prescaler     = 719;          /* 72MHz/720 = 100kHz → 10µs/tick */
    t.TIM_ClockDivision = 0;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &t);
    TIM_Cmd(TIM4, ENABLE);
    g_last_cnt = 0;
}

static void TimeBase_Update(void)
{
    uint16_t c = TIM_GetCounter(TIM4);
    uint16_t d = (uint16_t)(c - g_last_cnt);
    g_last_cnt = c;

    if (d) g_tb_ok = 1;
    if (g_tb_ok) {
        g.ms += (uint32_t)d / 100u;         /* 10µs → ms */
    } else {
        /* 兜底: 仿真平台若不模拟 TIM4, 按主循环节拍 (delay_ms(10)) 估算 */
        g.ms += 10;
    }
}

static uint32_t millis(void)
{
    TimeBase_Update();
    return g.ms;
}

/*==============================================================================
 *                四-B、微秒/毫秒延时 (TIM1 独立时基, 不依赖 SysTick)
 *
 *  ★ 2026-10-01: 时基从 TIM3 迁到 TIM1 —— TIM3 的 CH1/CH2 (PB4/PB5) 被 4 路
 *    风扇 PWM 征用了, 同一个定时器的时基与 PWM 没法共存(PWM 要求 ARR 定周期,
 *    时基要求 ARR 拉满 65535 才有 65.5ms 量程, 二者矛盾)。
 *    定时器当"纯计数器"用**不占任何引脚**, 所以高级定时器 TIM1 完全胜任:
 *    只 TIM_Cmd 使能计数, 不配置任何输出通道即可。
 *    ★ 以后要再挪时基: 只改下面 US_TIM / US_TIM_CLK 两行 + APB1/APB2 前缀。
 *
 *  平台 delay.c 的 delay_us/delay_ms 是"配 SysTick 忙等 COUNTFLAG"的实现。
 *  如果仿真平台只把 SysTick 做成了"标志位读得到"的桩(并不真计数), 延时就会
 *  瞬间返回 —— 现象非常迷惑人: 程序一路跑通、OLED 正常刷新、串口照打日志,
 *  但 DHT11 这类微秒级单总线传感器**永远不应答**, 因为 18ms 起始脉冲被压成了
 *  几微秒。所以给单总线单独配一个不依赖 SysTick 的时基: US_TIM 自由运行
 *  @1MHz (1µs/拍), 由定时器外设自己计数。
 *
 *  开机自检两件事, 结果直接显示在诊断屏上:
 *    g_us_ok  : US_TIM 是否真的在走 (不走就退回 delay_us, 绝不把程序挂死)
 *    g_sys_ok : 平台的 delay_us(1000) 是否真的花掉约 1000µs (以 US_TIM 为准)
 *============================================================================*/
#define US_TIM           TIM1
#define US_TIM_CLK()     RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE)

static uint8_t  g_us_ok    = 0;   /* 1 = US_TIM 计数已确认可用 */
static uint8_t  g_sys_ok   = 0;   /* 1 = 平台 delay_us() 真的在计时 */
static uint16_t g_sys_us1k = 0;   /* 实测 delay_us(1000) 耗时 (µs) */

static void us_dly_init(void)
{
    TIM_TimeBaseInitTypeDef t;
    uint16_t c0, c1;

    US_TIM_CLK();
    TIM_TimeBaseStructInit(&t);
    t.TIM_Period        = 0xFFFF;
    t.TIM_Prescaler     = 71;             /* 72MHz / 72 = 1MHz -> 1µs/拍 */
    t.TIM_ClockDivision = 0;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(US_TIM, &t);
    TIM_SetCounter(US_TIM, 0);
    TIM_Cmd(US_TIM, ENABLE);

    /* 自检 1: US_TIM 是否真的在计数 */
    c0 = TIM_GetCounter(US_TIM);
    delay_us(1000);
    c1 = TIM_GetCounter(US_TIM);
    if (c0 != c1) g_us_ok = 1;

    /* 自检 2: 平台的 delay_us(1000) 实际花了多久 (拿 US_TIM 当尺子) */
    if (g_us_ok) {
        c0 = TIM_GetCounter(US_TIM);
        delay_us(1000);
        c1 = TIM_GetCounter(US_TIM);
        g_sys_us1k = (uint16_t)(c1 - c0);
        g_sys_ok   = (g_sys_us1k >= 800u && g_sys_us1k <= 1250u) ? 1 : 0;
    }
}

/* 微秒延时 (n ≤ 65535; US_TIM 不可用时退回平台 delay_us) */
static void us_dly(uint16_t n)
{
    uint16_t c0, c1;
    uint32_t guard = 0;

    if (!g_us_ok) { delay_us(n); return; }
    c0 = TIM_GetCounter(US_TIM);
    for (;;) {
        c1 = TIM_GetCounter(US_TIM);
        if ((uint16_t)(c1 - c0) >= n) return;
        if (++guard > 400000u) { g_us_ok = 0; return; }   /* 计数器停了 -> 退回 SysTick */
    }
}

/* ★ 2026-10-04: US_TIM(TIM1, 1µs/拍) 当前计数 —— DHT/超声波/485/北斗 共用同一个时基。
 *   原先这份定义写在 `#if USE_WIND485` 里(485 收帧超时用); 北斗软串口也要用,
 *   而两个宏各自独立开关 —— 各写一份会在"都为 1"时重复定义, "都为 0"时又找不到。
 *   提到这里只定义一次, 并用 || 兜住: 谁开都能用, 都不开就不编(免 unused 警告)。 */
#if USE_WIND485 || BDS_ENABLE
static uint16_t us_now(void) { return TIM_GetCounter(US_TIM); }
#endif

/* 毫秒延时 (内部按 60ms 分段, 适配 16 位计数器 65.5ms 上限) */
static void ms_dly(uint16_t n)
{
    while (n > 60u) { us_dly(60000u); n = (uint16_t)(n - 60u); }
    if (n) us_dly((uint16_t)(n * 1000u));
}

#if LAMP_KIND == 0
/*==============================================================================
 *       四-B、SK6812 单总线 RGBW 状态灯 (DIN = PA3) —— 已弃用, 保留备查
 *
 *  ⚠ 本节整段只在 LAMP_KIND=0 时编译。2026-10-02 状态灯已换成 PA3 上的
 *    `AlarmLight` 报警灯(单色、只有亮/灭), 原因: 这颗灯珠的 0.3µs 位宽在平台上
 *    根本分不开 —— 标准时序发出来是纯白, 放大时序又被整帧丢弃, 六轮候选扫描
 *    也没救回来。单色报警灯只要一个电平, 没有位宽问题, 演示更稳。
 *
 *  为什么不能拿延时函数糊过去:
 *    1) 平台的 delay_us 是 SysTick 桩, 可能瞬间返回(见四节), 位宽会全错;
 *    2) 标准 800kHz 的 0 码 0.30µs 与 1 码 0.60µs 只差 0.3µs, 就算拿 72MHz 时基
 *       量出来了, **仿真平台也分辨不了** —— 实测表现是每一位都被判成 1,
 *       32 个 1 就是 0xFF/0xFF/0xFF → 灯**一直发纯白**。对照 DHT11 能读通,
 *       是因为它的 0/1 码差 44µs(26 vs 70), 比 SK6812 大 150 倍。
 *
 *  做法: 每一位都用"从位起点算起的拍数"定位, 定位是**绝对对齐**的(高低电平共用
 *  同一个起点 c0), 抖动不会逐位累积。用哪套参数由 sk_tm 决定, 候选表见下面 SK_TRY:
 *  "标准 800kHz"档在发送前把 US_TIM 临时切到 72MHz, 发完立刻恢复; "放大"档就用
 *  自检通过的 1MHz 时基 —— 两档共用同一个定时器。
 *
 *  ★ 切到 72MHz 的那一档期间**绝不能调用 us_dly()/ms_dly()** —— 它们假设 1µs/拍。
 *  ★ 若上电自检的实际显示顺序不对(不少模块其实是 GRBW 而非 RGBW), 只需
 *    调换 sk_show() 里那 4 个 sk_send_byte() 的次序, 时序本身不用动。
 *============================================================================*/
/* ---------- 两套位时序 (2026-10-02 实测后的定案) ----------
 * 为什么要两套 —— 这是**踩过的坑**, 记在这里免得再走一遍:
 *
 *   最初只做了"标准 800kHz"(0 码 0.30µs / 1 码 0.60µs), 为了分开这 0.3µs 的差别,
 *   发送前把 US_TIM 从 1MHz 临时切到 72MHz。仿真平台照做了 —— 可 0.3µs 这个尺度
 *   **低于平台的时间分辨率**: 平台眼里两个码一样宽, 于是每一位都判成了 1,
 *   32 个 1 = 0xFF/0xFF/0xFF/0xFF = **纯白**(这正是"灯一直发白光"的来源)。
 *
 *   对照: DHT11 在平台上读得通, 是因为它的 0/1 码差 **44µs**(26 vs 70µs);
 *   SK6812 只差 0.3µs, 小了 150 倍 —— 平台分不开。
 *
 * 定案: 不再"猜一套写死", 改成**开机现场扫** —— 见四-B 节的 sk_lamp_sweep():
 *   6 轮候选(含"发反相色"两轮)各发一帧, 哪一轮灯珠亮红就定哪一套,
 *   然后把 SK_TM_PICK 改成它的号。参数与判读都写在 SK_TRY 表那一带。 */
typedef struct {
    uint16_t t0h;      /* 0 码高电平宽度 (时基拍) */
    uint16_t t1h;      /* 1 码高电平宽度 */
    uint16_t bit;      /* 整个位周期 */
    uint16_t rst;      /* 复位低电平 */
    uint8_t  use72m;   /* 1 = 发送前把 US_TIM 临时切 72MHz */
} sk_timing_t;

/* 两套"参考"时序现在都是下面 SK_TRY 表里的行, 不再单独定义:
 *   标准 0.31/0.60µs @1.25µs = 第 1 行 STD ;  放大 3/20µs @30µs = 第 5 行 SLWI */

/* ---- 候选时序表 + 开机扫描自检 (2026-10-02) --------------------------------
 * 平台把**解码放在服务端编译好的 libsim-BK_SK6812.so 里**(前端 moduleUtils
 * 把每个元件编成 libsim-<type>.so), 前端只拿得到"已经解好的颜色"
 * (BK_SK6812_protocol = updateLed, 载荷 {id,R,G,B}); 判据在前端读不到
 * —— 所以只能实测。已经实测到两件事实:
 *   标准规格 0.31/0.60µs @1.25µs -> 灯珠发**纯白**
 *        (= 32 位全 1 = 两个码没被分开, 平台把每一位都判成 1)
 *   放大     3/20µs @30µs         -> **完全没有颜色下发**
 *        (前端 ledArr 里没有这个键 -> isShow() 为假 -> 连线都不画;
 *         你看到的那个黑圆是**元件底图里的灯罩**, 不是"灯在发黑光")
 * 这两种现象同时成立, 最像"模块内部把数据反了相": 反相后 0.95/0.65µs 的高电平
 * 都算长 -> 全 1(白); 而 27µs 的低电平会被当成复位, 整帧报废 -> 什么都没有。
 * 所以下面 6 轮里专门有两轮**发反相色**(要红就发 0,255,255):
 * 第 4 轮若亮红, 就说明确实反相 -> 正常工作也按反相发。
 * 每轮 0.5s 亮 + 0.25s 灭, 共约 4.5s; 烧写一次就能定案。 */
typedef struct {
    sk_timing_t tm;                    /* 这一轮用哪套时序 */
    uint8_t     r, g, b;               /* 这一轮发什么颜色 */
    const char *label;
} sk_try_t;

static const sk_try_t SK_TRY[] = {
    { {  22u, 43u,  90u, 6500u, 1u }, 255u,   0u,   0u, "1/6 STD " },  /* 0.31/0.60µs @1.25µs */
    { {   7u, 43u,  90u, 6500u, 1u }, 255u,   0u,   0u, "2/6 XNAR" },  /* 0 码压到 0.10µs */
    { {  29u, 58u,  90u, 6500u, 1u }, 255u,   0u,   0u, "3/6 SPEC" },  /* 0.40/0.81µs 教科书 */
    { {  29u, 58u,  90u, 6500u, 1u },   0u, 255u, 255u, "4/6 INV " },  /* 同上 + **发反相色** */
    { {   3u, 20u,  30u,  400u, 0u },   0u, 255u, 255u, "5/6 SLWI" },  /* 放大时序 + 发反相色 */
    { {  29u, 58u, 144u, 6500u, 1u }, 255u,   0u,   0u, "6/6 2US " },  /* 位周期放宽到 2µs */
};
#define SK_TRY_N     ((uint8_t)(sizeof(SK_TRY) / sizeof(SK_TRY[0])))

/* 扫描自检开关; 以及**扫描之后正常工作用第几套**(看扫描结果改成亮红的那一号) */
#define SK_SWEEP_ENABLE 1
#define SK_TM_PICK      1

/* 当前使用的时序 */
static const sk_timing_t *sk_tm = &SK_TRY[0].tm;

static uint8_t g_sk_ok  = 0;                        /* 1 = 本灯可用 */
static uint8_t g_sk_r   = 0;                        /* 已发出的红分量(与下两个一起去重用) */
static uint8_t g_sk_g   = 0;                        /* 已发出的绿分量 */
static uint8_t g_sk_b   = 0;                        /* 已发出的蓝分量 */

/* 等 US_TIM 计数从 c0 起走过 cyc 拍。带守卫: 计数器若停住就放弃, 绝不死循环。
 * guard 给到 20 万 —— 放大时序的复位要等 400 拍, 若平台每圈推进的"模拟时间"
 * 很小(圈数远大于拍数), 3 万的上限会被误触发; 20 万留足余量, 又不至于真卡住时
 * 干等太久。 */
static void sk_wait(uint16_t c0, uint16_t cyc)
{
    uint32_t guard = 0;
    while ((uint16_t)(TIM_GetCounter(US_TIM) - c0) < cyc) {
        if (++guard > 200000u) { g_sk_ok = 0; return; }
    }
}

static void sk_send_byte(uint8_t b)
{
    uint8_t  i;
    uint16_t c0;

    for (i = 0; i < 8u && g_sk_ok; i++) {
        c0 = TIM_GetCounter(US_TIM);
        SK_ON();
        sk_wait(c0, (b & 0x80u) ? sk_tm->t1h : sk_tm->t0h);  /* 高电平宽度决定 0/1 */
        SK_OFF();
        sk_wait(c0, sk_tm->bit);                             /* 补足整个位周期 */
        b = (uint8_t)(b << 1);
    }
}

static void sk_show(void)
{
    uint8_t  i;
    uint16_t c0;

    if (!SK6812_ENABLE || !g_sk_ok) return;

    if (sk_tm->use72m) {            /* 只有标准 800kHz 才需要切高速时基 */
        TIM_PrescalerConfig(US_TIM, 0, TIM_PSCReloadMode_Immediate);   /* -> 72MHz */
        TIM_SetCounter(US_TIM, 0);
    }

    __disable_irq();               /* 位时序期间不能被打断 (放大时序约 1ms) */
    for (i = 0; i < SK_NUM && g_sk_ok; i++) {
        sk_send_byte(g_sk_r);              /* ★ 若实测颜色顺序不符, 调换这 4 行 */
        sk_send_byte(g_sk_g);
        sk_send_byte(g_sk_b);
        sk_send_byte(0u);                  /* W 通道: 本设计不用, 恒 0 */
    }
    __enable_irq();

    SK_OFF();                              /* 复位: 数据线拉低 */
    c0 = TIM_GetCounter(US_TIM);
    sk_wait(c0, sk_tm->rst);

    if (sk_tm->use72m) {                   /* 恢复 1MHz */
        TIM_SetCounter(US_TIM, 0);
        TIM_PrescalerConfig(US_TIM, 71, TIM_PSCReloadMode_Immediate);
    }
}

/* 设置灯色 (0~255)。颜色没变就不重发 —— 省时间, 也少关中断。
 * 灯珠断电前会保持上次写入的颜色, 所以上电时要主动发一帧把状态刷成已知值。 */
static void sk_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!SK6812_ENABLE || !g_sk_ok) return;
    if (r == g_sk_r && g == g_sk_g && b == g_sk_b) return;
    g_sk_r = r; g_sk_g = g; g_sk_b = b;
    sk_show();
}

static void sk_init(void)
{
    GPIO_InitTypeDef io;

    io.GPIO_Pin   = SK_PIN;
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SK_PORT, &io);
    SK_OFF();                              /* 数据线空闲应为低电平 */

    /* 位时序要靠 US_TIM 定位, 所以必须等 us_dly_init() 自检通过才能用 */
    g_sk_ok = g_us_ok;
    g_sk_r  = g_sk_g = g_sk_b = 0;
    if (g_sk_ok) sk_show();                /* 上电先清一帧, 免得带着上次的颜色上电 */
}

#else   /* ---------------- LAMP_KIND == 1: AlarmLight 报警灯 ---------------- */

/* 报警灯初始化: PB12 推挽输出(2026-10-03 从 PA3 让位), 上电先熄灭。
 * ★ 这里**只配脚、不猜极性** —— 亮灯电平由 ALARM_ACTIVE_LOW 决定, 而它到底该是
 *   几, 由开机 alarm_probe() 两种电平各驱动 2 秒、用人眼当场定案(见十六节前)。 */
static void alarm_init(void)
{
    GPIO_InitTypeDef io;

    io.GPIO_Pin   = ALARM_PIN;
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ALARM_PORT, &io);
    ALARM_SET_OFF();                       /* 上电先灭, 别一上电就报警 */
}

#endif  /* LAMP_KIND */

/*==============================================================================
 *                          五、GPIO / 外设初始化
 *============================================================================*/

static void GPIO_AllInit(void)
{
    GPIO_InitTypeDef io;
    uint8_t i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    /* ---- SWJ(串行调试口) 释放 —— 决定哪些"调试专用脚"能被当普通 IO 用 ----
     *   SWJ_CFG = 010 (JtagDisable): 关 JTAG、留 SWD → 释放 PA15/PB3/PB4;
     *                                PA13/PA14 仍属 SWD, 下载口还在。
     *   SWJ_CFG = 100 (Disable)    : JTAG + SWD 全关   → 再释放 PA13/PA14,
     *                                但**板子的下载/调试口就没了**(见 DHT_IN_PA14 警告)。
     * 本项目: 4 路风扇固定要 PA15/PB3/PB4; 仓内 DHT11 按 DHT_IN_PA14 决定要不要 PA14。 */
#if DHT_IN_PA14
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);       /* 全关: PA13~PA15/PB3/PB4 全释放 */
#else
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);   /* 只关 JTAG, 保住 SWD 下载口 */
#endif

    /* PB6/PB7 (OLED I2C) 由 oled_i2c_init() 配置; PB10/PB11 (USART3) 由
       USART3_Init() 配置 —— 两处都在这之后调用, 互不覆盖。 */

    /* ---------- PA: 控制 + 数字量 ---------- */
    /* 诱虫灯(PA11) / 蜂鸣器(PA12) / 粉尘LED(PA8)
     * ★ PA2/PA3 **不再**出现在这里 —— 它们已归 USART2(485 风速)。若还留在这句
     *   GPIO_Init 里, 就会被设成普通推挽, 把随后 USART2_Init() 的复用配置覆盖掉。 */
#if DUST_ENABLE
    io.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_11 | GPIO_Pin_12;
#else
    io.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_12;   /* PA8 随粉尘作废而释放为空闲 */
#endif
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &io);
    LAMP_OFF(); BEEP_OFF();
#if DUST_ENABLE
    GP2Y_LED_OFF();
#endif

    /* 超声波 TRIG → **PB1** (2026-10-03 从 PA2 让位而来); 同样是普通推挽输出 */
    io.GPIO_Pin   = HC_TRIG_PIN;
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(HC_TRIG_PORT, &io);
    HC_TRIG_L();

    /* ---------- 4 路风扇 (PA15 / PB3 / PB4 / PB5) ----------
     * 先一律配成普通推挽输出并置低(风扇停), 免得一上电就满速抽风;
     * 若用硬件 PWM, fan_pwm_init() 随后会把它们改成 AF 复用推挽。 */
    io.GPIO_Pin   = fan_pin[0];                      /* PA15 */
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &io);
    io.GPIO_Pin   = (uint16_t)(fan_pin[1] | fan_pin[2] | fan_pin[3]);   /* PB3/PB4/PB5 */
    GPIO_Init(GPIOB, &io);
    for (i = 0; i < 4u; i++) fan_pin_off(i);

    /* 超声波 ECHO(PA0) 输入下拉; PA7 = **MQ-135 DO**(低电平=超阈值) 输入上拉
     * ★ PA4(光敏 AO) 与 PA6(MQ-135 AO) 由 ADC_PinsInit() 配成 AIN;
     *   PA5(补光 LED) 由 glamp_init() 配成 Out_PP。
     *   三处都**不在这里**初始化 —— 免得被这句 GPIO_Init 覆盖成别的模式。 */
    io.GPIO_Pin   = GPIO_Pin_0;
    io.GPIO_Mode  = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &io);

#if GAS_ENABLE
    io.GPIO_Pin   = GPIO_Pin_7;          /* MQ-135 DO: 板载比较器输出, 低电平=超阈值 */
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &io);
#endif

#if FLAME_ENABLE
    /* 火焰传感器 DO → PB0: 模块板上比较器的数字输出(推挽), 这里配上拉输入。
     * 模块没接时上拉把线拉高; 上电后串口会把**实际电平**打出来, 供现场判读。 */
    io.GPIO_Pin   = FLAME_PIN;
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(FLAME_PORT, &io);
#endif

    /* DHT11 #1 (舱外) 数据脚: 默认脚先置输出高; 实际用哪根由 dht_scan() 开机测定 */
    dht_pin_out(DHT_PIN_DEFAULT);
    dht_set(DHT_PIN_DEFAULT);

    /* DHT11 #2 (仓内) 数据脚: 固定 DHT_IN_PIN, 置上拉输入(总线空闲应为高) */
    dht_pin_in(DHT_IN_PIN);

    /* ---------- PC13/PC14/PC15 ----------
     * 2026-10-02 起三色状态灯取消, PC13~15 释放; ★ 它们与 RTC 晶振复用, **不要开 LSE**。
     * 2026-10-04: PC14 给雨量传感器 DO。 */
#if RAIN_ENABLE
    io.GPIO_Pin   = RAIN_PIN;                 /* PC14: 雨量 DO (模块比较器的数字输出) */
    io.GPIO_Mode  = GPIO_Mode_IPU;            /* 上拉输入: 模块没接时线上是高, 一眼可辨 */
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RAIN_PORT, &io);
#endif

#if BDS_ENABLE
    /* 北斗模块 TX → PB2: 软件串口接收脚, 上拉输入(线空闲应为高)。 */
    io.GPIO_Pin   = BDS_PIN;
    io.GPIO_Mode  = GPIO_Mode_IPU;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BDS_PORT, &io);
#endif
}

static void ADC_PinsInit(void)
{
    GPIO_InitTypeDef io;
    io.GPIO_Pin  = GPIO_Pin_4 | GPIO_Pin_6;   /* PA4 光敏电阻 AO / PA6 MQ-135 AO
                                               * ★ PA5 已**不是** ADC: 它现在是补光 LED 的
                                               *   推挽输出脚(软件 PWM), 由 glamp_init() 单独配置。
                                               *   把 PA5 留在这里配成 AIN, LED 就永远驱动不动。 */
    io.GPIO_Mode = GPIO_Mode_AIN;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &io);
}

/* ---------- 带超时的安全 ADC (替代 SYSTEM/adc 的 Adc_Init/Get_Adc) ----------
   模板 adc.c 里有三处 while 死等（复位校准 / 校准 / EOC）。仿真平台若不模拟
   ADC 状态位，程序会永久卡死在 Adc_Init() —— 而且它在 oled_init() 之前调用，
   表现就是"屏幕完全不亮"。这里全部加超时，取不到就返回 0，绝不阻塞。      */
static void adc_safe_init(void)
{
    ADC_InitTypeDef a;
    uint32_t to;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);        /* 72M/6 = 12MHz (<14M) */

    ADC_DeInit(ADC1);
    ADC_StructInit(&a);
    a.ADC_Mode               = ADC_Mode_Independent;
    a.ADC_ScanConvMode       = DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;      /* 单次转换，每次软件触发 */
    a.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign          = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &a);

    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    to = 200000;
    while (ADC_GetResetCalibrationStatus(ADC1) && to--) ;

    ADC_StartCalibration(ADC1);
    to = 200000;
    while (ADC_GetCalibrationStatus(ADC1) && to--) ;
}

static uint16_t adc_read_safe(uint8_t ch)
{
    uint32_t to = 200000;

    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_55Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET && to--) ;
    if (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) return 0;   /* 超时: 返回 0 */
    return (uint16_t)ADC_GetConversionValue(ADC1);
}

static uint16_t adc_read_avg_safe(uint8_t ch, uint8_t times)
{
    uint32_t sum = 0;
    uint8_t  i;
    if (times == 0) times = 1;
    for (i = 0; i < times; i++) {
        sum += adc_read_safe(ch);
        delay_us(200);
    }
    return (uint16_t)(sum / times);
}

#if USE_TIM_CAPTURE
static void TIM2_CaptureInit(void)
{
    TIM_TimeBaseInitTypeDef t;
    TIM_ICInitTypeDef ic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseStructInit(&t);
    t.TIM_Period    = 0xFFFF;
    t.TIM_Prescaler = 71;                 /* 72MHz/72 = 1MHz → 1µs/tick */
    TIM_TimeBaseInit(TIM2, &t);

    TIM_ICStructInit(&ic);
    ic.TIM_Channel     = TIM_Channel_1;
    ic.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter    = 0x0;
    TIM_ICInit(TIM2, &ic);
    TIM_Cmd(TIM2, ENABLE);
}
#endif

/*==============================================================================
 *              五-B、4 路 PWM 风扇 (TIM2 CH1/CH2 + TIM3 CH1/CH2, 1kHz)
 *
 *  引脚↔通道是硬件写死的(见文件头「4 路 PWM 风扇」), 这里做三件事:
 *    ┌ TIM2 **部分重映射**(TIM2_REMAP=01): CH1→PA15, CH2→PB3
 *    │   特意不用"全重映射"(=11) —— 那样 CH3/CH4 会落到 PB10/PB11,
 *    │   而那两根正是 ESP8266 的 USART3_TX/RX, 复用脚会打架。
 *    │   部分重映射把 CH3/CH4 留在 PA2/PA3(PA2 是超声波 TRIG, 只要不配置
 *    │   CH3 的输出就毫无影响)。
 *    └ TIM3 **部分重映射**: CH1→PB4, CH2→PB5  (CH3/CH4 留在 PB0/PB1)
 *
 *  频率: PSC=71 → 1MHz 计数; ARR=999 → **1kHz** PWM, 1000 级(0.1% 分辨率)。
 *  占空比: CCR = pct × (ARR+1) / 100。
 *
 *  ★ 硬件 PWM / 软件 PWM 二选一(FAN_USE_TIM_PWM):
 *    平台对"外设复用输出"的模拟不一定到位(本平台已知 delay 是桩、TIM4 未必计数)。
 *    若硬件 PWM 下风扇纹丝不动, 把 FAN_USE_TIM_PWM 改 0 —— 主循环会用
 *    fan_soft_cycle() 以 GPIO 位翻转产生同样的 1kHz PWM。
 *    GPIO 时序平台一定看得见(软件 I2C 已被验证可行), 所以那条路必然能工作。
 *============================================================================*/
static uint8_t g_fan_pwm_ok = 0;      /* 1 = 硬件 PWM 已就绪 */

/* 一次配好一个通道 (TIM2/TIM3 是通用定时器, 无需 TIM_CtrlPWMOutputs) */
static void fan_tim_ch_setup(TIM_TypeDef *T, uint8_t ch)
{
    TIM_OCInitTypeDef oc;

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    oc.TIM_Pulse       = 0;                     /* 上电占空比 0 → 风扇停 */
    if (ch == 1u) TIM_OC1Init(T, &oc);
    else          TIM_OC2Init(T, &oc);
}

static void fan_pwm_init(void)
{
#if FAN_USE_TIM_PWM
    TIM_TimeBaseInitTypeDef t;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3, ENABLE);

    GPIO_PinRemapConfig(GPIO_PartialRemap1_TIM2, ENABLE);   /* CH1→PA15 CH2→PB3 */
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3,  ENABLE);   /* CH1→PB4  CH2→PB5 */

    TIM_TimeBaseStructInit(&t);
    t.TIM_Period        = FAN_PWM_ARR;          /* 999 → 1000 级 */
    t.TIM_Prescaler     = 71;                   /* 72MHz/72 = 1MHz */
    t.TIM_ClockDivision = 0;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &t);
    TIM_TimeBaseInit(TIM3, &t);

    fan_tim_ch_setup(TIM2, 1u);   /* PA15 FAN1 */
    fan_tim_ch_setup(TIM2, 2u);   /* PB3  FAN2 */
    fan_tim_ch_setup(TIM3, 1u);   /* PB4  FAN3 */
    fan_tim_ch_setup(TIM3, 2u);   /* PB5  FAN4 */

    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    /* 4 个脚切成定时器复用推挽 (必须在重映射之后) */
    {
        GPIO_InitTypeDef io;
        io.GPIO_Pin   = fan_pin[0];
        io.GPIO_Mode  = GPIO_Mode_AF_PP;
        io.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &io);
        io.GPIO_Pin   = (uint16_t)(fan_pin[1] | fan_pin[2] | fan_pin[3]);
        GPIO_Init(GPIOB, &io);
    }

    g_fan_pwm_ok = 1;
#else
    g_fan_pwm_ok = 0;     /* 走软件 PWM, 引脚保持 GPIO 推挽输出 */
#endif
}

/* 设置第 n 台(0~3) 风扇转速 0~100; 同时记进 g.fan[] 供上报/显示 */
static void fan_set_pct(uint8_t n, uint8_t pct)
{
    if (n > 3u) return;
    if (pct > 100u) pct = 100u;
    g.fan[n] = pct;

#if FAN_USE_TIM_PWM
    if (g_fan_pwm_ok) {
        uint16_t ccr = (uint16_t)((uint32_t)pct * (FAN_PWM_ARR + 1u) / 100u);
        switch (n) {
            case 0:  TIM_SetCompare1(TIM2, ccr); break;   /* PA15 TIM2_CH1 */
            case 1:  TIM_SetCompare2(TIM2, ccr); break;   /* PB3  TIM2_CH2 */
            case 2:  TIM_SetCompare1(TIM3, ccr); break;   /* PB4  TIM3_CH1 */
            default: TIM_SetCompare2(TIM3, ccr); break;   /* PB5  TIM3_CH2 */
        }
        return;
    }
#endif
    /* 软件 PWM: 主循环的 fan_soft_cycle() 会读 g.fan[] 产生波形, 这里不动引脚 */
}

static void fan_all_off(void)
{
    uint8_t i;
    for (i = 0; i < 4u; i++) fan_set_pct(i, 0);
    g.fan_on = 0; g.fan_pct = 0;
}

/* 4 台一起设同一个转速 (手动命令 CMD_FAN_ON/OFF 用) */
static void fan_all_set(uint8_t pct)
{
    uint8_t i;
    for (i = 0; i < 4u; i++) fan_set_pct(i, pct);
    g.fan_on   = (uint8_t)(pct ? 4u : 0u);
    g.fan_pct  = pct;
    g.vent_why = (uint8_t)(pct ? 4u : 0u);     /* 4 = 手动 */
}

#if !FAN_USE_TIM_PWM
/* 软件 PWM 的一个 1ms 周期: 4 台同时起高, 按占空比从小到大先后拉低。
 * 4 段延时的总和恰好 = 1000µs, 所以整个周期就是 1ms —— 与硬件 PWM 同频。
 * 主循环每拍跑 10 个周期(10ms), 主循环节拍不受影响。 */
static void fan_soft_cycle(void)
{
    uint8_t  pend = 0x0Fu;      /* bit i = 第 i 台此刻仍为高 */
    uint16_t el   = 0;          /* 本周期已走的 µs */
    uint8_t  k, i;

    for (i = 0; i < 4u; i++) fan_pin_on(i);

    for (k = 0; k < 4u; k++) {
        uint8_t  best = 0xFF;
        uint16_t bt   = 0xFFFF;

        for (i = 0; i < 4u; i++) {
            uint16_t ct;
            if (!(pend & (1u << i))) continue;
            ct = (uint16_t)(g.fan[i] * 10u);        /* 该台高电平长度 (µs) */
            if (ct < bt) { bt = ct; best = i; }
        }
        if (best == 0xFF) break;
        if (bt > el) { us_dly((uint16_t)(bt - el)); el = bt; }
        pend = (uint8_t)(pend & ~(1u << best));
        fan_pin_off(best);
    }
    if (el < 1000u) us_dly((uint16_t)(1000u - el));
}
#endif

/* 注: TFT 改用 16 位并口后, PB 口被 D0~D15 占满, PA8 改作粉尘 LED、PB5 释放;
 * 原 RGB 双路 PWM (TIM1_CH1/TIM3_CH2) 早已不需要(RGB 用 PC13/14/15 纯开关)。
 * 2026-10-01 起 TIM2_CH1/CH2 与 TIM3_CH1/CH2 改作 4 路风扇 PWM, 见上一节。 */


/*==============================================================================
 *                          六、USART1 (自实现，不依赖 usart.c 的接收中断)
 *============================================================================*/

static void USART1_Init(uint32_t baud)
{
    GPIO_InitTypeDef io;
    USART_InitTypeDef u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    io.GPIO_Pin   = GPIO_Pin_9;            /* TX */
    io.GPIO_Mode  = GPIO_Mode_AF_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &io);

    io.GPIO_Pin   = GPIO_Pin_10;           /* RX */
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &io);

    USART_StructInit(&u);
    u.USART_BaudRate            = baud;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &u);
    USART_Cmd(USART1, ENABLE);
}

static void u1_putc(char c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint16_t)c);
}

static void u1_raw(uint8_t b) { u1_putc((char)b); }

static void u1_print(const char *s)
{
    while (*s) u1_putc(*s++);
}

static void u1_print_int(int32_t v)
{
    char buf[12];
    int  i = 10;
    int  neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    buf[11] = '\0';
    if (v == 0) buf[i--] = '0';
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) buf[i--] = '-';
    u1_print(&buf[i + 1]);
}

#if USE_WIND485
/*==============================================================================
 *      六-B、USART2 + RS485 风速传感器 (Modbus RTU 主站, 9600 8N1) [2026-10-03]
 *
 *  为什么占 USART2: USART1 是调试/协议帧口、USART3 是 ESP8266 专口, 这两路都
 *  不能动; 而 STM32F103C8(LQFP48) 只有 USART1/2/3 三路, 所以只剩 USART2 ——
 *  **它的脚是固定死的 PA2(TX) / PA3(RX)**, 只能把原来压在这两根线上的超声波
 *  TRIG 和报警灯让开: TRIG → PB1、报警灯 → PB12 (接线跟着改这两根)。
 *
 *  ★★ "平台没有 485 收发器, 怎么接?" —— 正解不是想办法加转换模块, 而是**不需要**:
 *     puliedu 器件库里确实没有 MAX485/SP3485 模型, 因此平台把这类 485 器件
 *     (ZTS_3000_FSJT / RS_FX_N01 / SN_3000_TR ...) **简化成一对串口线**:
 *         传感器 A(+) → MCU 的 RX        传感器 B(−) → MCU 的 TX
 *     差分线在仿真里等效成 TTL 串口(半双工简化成全双工), **直连即可**。
 *     **实物部署**才需要收发器, 接法: A→A、B→B, RO→PA3、DI→PA2,
 *     DE/RE 并联后接一个空闲 GPIO 做收发方向切换。
 *     这段"仿真简化 vs 实物接法"的对比写进设计报告是加分项, 不是避讳。
 *
 *  ★ 接反了是什么现象: USART2 **一个字节都收不到**。所以本驱动把每次收到的原始
 *    字节按 hex 打一份到 USART1(发 'V' 可开/关"逐次打印", 默认只在成败翻转时打),
 *    "线没接 / 接反 / 波特率不对 / 元件地址不同" 四种情况当场分得开, 不用猜。
 *
 *  数据口径: 保持寄存器 0x0000 的两个字节, 量程 0~600 对应 **0~60.0 m/s**(值÷10)。
 *  风压(动压)由风速算得, 见 wind_poll() 里的算式 —— 平台没有风压传感器, 但不需要:
 *     动压 q = ½ρv², 取 ρ = 1.2 kg/m³(常温常压空气) ⇒ **q[Pa] = 0.6 · v²**
 *  这就是"用风速测风压"的全部依据(伯努利), 不是拟合出来的经验值。
 *============================================================================*/

#define WIND_ADDR       1u          /* Modbus 从站地址 (元件默认 01) */
#define WIND_BAUD       9600u       /* 4800 也支持; 与元件属性里的波特率保持一致 */
#define WIND_REG        0x0000u     /* 风速值所在保持寄存器 */
#define WIND_MAX_RAW    6000u       /* 0~600.0 m/s 上限: 只为挡住异常的寄存器值, 防溢出 */

static uint8_t  g_wind_verbose = 0;      /* 1 = 每次轮询都把收发字节 hex 打到 USART1 */
static uint8_t  g_wind_lastok  = 0xFFu;  /* 上一次的成败(0xFF=还没跑过), 用于"只在变化时"打印 */

static void USART2_Init(uint32_t baud)
{
    GPIO_InitTypeDef  io;
    USART_InitTypeDef u;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    io.GPIO_Pin   = GPIO_Pin_2;            /* TX -> 485-B */
    io.GPIO_Mode  = GPIO_Mode_AF_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &io);

    io.GPIO_Pin   = GPIO_Pin_3;            /* RX <- 485-A */
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &io);

    USART_StructInit(&u);
    u.USART_BaudRate            = baud;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &u);
    USART_Cmd(USART2, ENABLE);
}

static void u2_putb(uint8_t b)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, (uint16_t)b);
}

/* 收一个字节, 最多等 us 微秒; 超时返回 -1。
 * ★ 不用平台的 delay_us 计时(它可能是空桩), 用 US_TIM 自己数 —— 与 DHT11 同一条教训。 */
static int16_t u2_getb(uint16_t us)
{
    uint16_t t0;

    if (!g_us_ok) {                      /* 时基不可用: 退回粗等, 绝不挂死在这里 */
        uint16_t i;
        for (i = 0; i < (uint16_t)(us / 100u + 1u); i++) {
            if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET)
                return (int16_t)(USART_ReceiveData(USART2) & 0xFFu);
            delay_us(100);
        }
        return -1;
    }
    t0 = us_now();
    for (;;) {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == SET)
            return (int16_t)(USART_ReceiveData(USART2) & 0xFFu);
        if ((uint16_t)(us_now() - t0) >= us) return -1;
    }
}

/* Modbus RTU 的 CRC16 (多项式 0xA001, 初值 0xFFFF; 帧内**低字节先发**) */
static uint16_t mb_crc16(const uint8_t *b, uint8_t n)
{
    uint16_t crc = 0xFFFFu;
    uint8_t  i;

    while (n--) {
        crc ^= (uint16_t)(*b++);
        for (i = 0; i < 8u; i++)
            crc = (uint16_t)((crc & 1u) ? ((crc >> 1) ^ 0xA001u) : (crc >> 1));
    }
    return crc;
}

static void u1_hex2(uint8_t v)
{
    const char *h = "0123456789ABCDEF";
    u1_putc(h[(v >> 4) & 0x0Fu]);
    u1_putc(h[v & 0x0Fu]);
}

/* 把一段字节按 hex 打到 USART1 —— 485 总线上到底有没有东西, 只有这一条能看见 */
static void wind_dump(const char *tag, const uint8_t *p, uint8_t n)
{
    uint8_t i;
    u1_print(tag);
    for (i = 0; i < n; i++) { u1_print(" "); u1_hex2(p[i]); }
    u1_print("\r\n");
}

/* 问一次风速: 发 `01 03 00 00 00 01 <crc>`(共 8 字节),
 *             收 `01 03 02 <hi> <lo> <crc_lo> <crc_hi>`(共 7 字节)。
 * ★ 判"有效"要**三样全对**: 地址/功能码/字节数 对得上 + 长度正好 7 + CRC 对得上。
 *   任何一样不对 ⇒ wind_ok = 0 ⇒ 屏/网页/串口三处一律 --, 绝不拿 0 m/s 冒充"无风"。 */
static void wind_poll(void)
{
    uint8_t  q[8], r[8];
    uint8_t  i, n;
    uint8_t  ok   = 0;
    uint16_t crc;
    uint16_t raw  = 0;
    uint16_t tmo  = 60000u;      /* 首字节等 60ms(给从站应答时间); 之后每字节 6ms */

    q[0] = (uint8_t)WIND_ADDR;
    q[1] = 0x03u;                            /* 功能码 03: 读保持寄存器 */
    q[2] = (uint8_t)(WIND_REG >> 8);
    q[3] = (uint8_t)(WIND_REG & 0xFFu);
    q[4] = 0x00u;  q[5] = 0x01u;             /* 读 1 个寄存器 */
    crc  = mb_crc16(q, 6);
    q[6] = (uint8_t)(crc & 0xFFu);           /* CRC 低字节在前 */
    q[7] = (uint8_t)(crc >> 8);

    for (i = 0; i < 8u; i++) u2_putb(q[i]);

    for (n = 0; n < 7u; ) {
        int16_t c = u2_getb(tmo);
        if (c < 0) break;
        r[n++] = (uint8_t)c;
        tmo = 6000u;                         /* 帧内字节间隔只有 ~1ms, 不必再等 60ms */
    }

    if (n == 7u && r[0] == (uint8_t)WIND_ADDR && r[1] == 0x03u && r[2] == 0x02u) {
        uint16_t want = (uint16_t)(((uint16_t)r[6] << 8) | (uint16_t)r[5]);
        if (mb_crc16(r, 5) == want) {
            raw = (uint16_t)(((uint16_t)r[3] << 8) | (uint16_t)r[4]);
            ok  = 1;
        }
    }

    if (ok) {
        if (raw > (uint16_t)WIND_MAX_RAW) raw = (uint16_t)WIND_MAX_RAW;
        g.wind_raw   = raw;
        g.wind_x10   = (int16_t)raw;         /* 寄存器原值本身就是 ×10 m/s */
        /* ---- 风压(动压) = 由风速算出来的, 不是另装一个传感器 ----
         *   q = ½ρv², ρ = 1.2 kg/m³  ⇒  q = 0.6·v²  [Pa]
         *   这里存 **×10 Pa**(0.1Pa 分辨率)。整数算式(w = wind_x10 = v×10):
         *     q×10 = 0.6·(w/10)² × 10 = 0.06·w²  ⇒  w*w*6/100
         *   量程校验: w=30(3.0m/s) -> 54  => 5.4 Pa  (0.6×9=5.4 ✓)
         *             w=600(60m/s) -> 21600 => 2160.0 Pa (0.6×3600=2160 ✓)
         *   w 已钳在 6000 以内 ⇒ 6000*6000*6 = 2.16e8, int32 绝不溢出。 */
        g.wind_press_x10 = (int32_t)((int32_t)raw * (int32_t)raw * 6 / 100);
        g.wind_ok    = 1;
    } else {
        g.wind_ok        = 0;
        g.wind_raw       = 0;
        g.wind_x10       = 0;
        g.wind_press_x10 = 0;
    }

    /* 只在"成败翻转"或开了 verbose 时打印, 免得每秒往串口灌两行 */
    if (g_wind_verbose || ok != g_wind_lastok) {
        wind_dump("[WND] TX", q, 8u);
        wind_dump("[WND] RX", r, n);
        u1_print("[WND] ");
        if (ok) {
            u1_print("ok raw=");  u1_print_int((int32_t)g.wind_raw);
            u1_print(" -> ");     u1_print_int(g.wind_x10 / 10);
            u1_print(".");        u1_print_int(g.wind_x10 % 10);
            u1_print(" m/s  q="); u1_print_int(g.wind_press_x10 / 10);
            u1_print(".");        u1_print_int(g.wind_press_x10 % 10);
            u1_print(" Pa\r\n");
        } else {
            u1_print("FAIL n=");  u1_print_int((int32_t)n);
            u1_print("  (n=0: 一个字节都没回 -> 查 485-A/B 是否接反、波特率是否对;"
                     " n>0 但 CRC/地址不符: 查元件从站地址与寄存器号)\r\n");
        }
        g_wind_lastok = ok;
    }
}
#endif  /* USE_WIND485 */

/*==============================================================================
 *                          七、协议帧收发
 *============================================================================*/

#define PKT_VMAP     0x01
#define PKT_AMAP     0x02
#define PKT_LEVEL    0x04
#define PKT_SUMMARY  0x05
#define PKT_ENV      0x07
#define PKT_VENT     0x13
#define PKT_SOUND    0x14
#define PKT_ACK      0x16

#define CMD_PREFIX   0xC5
#define CMD_FAN_ON   0xA1
#define CMD_FAN_OFF  0xA2
#define CMD_LED_ON   0xA5
#define CMD_LED_OFF  0xA6
#define CMD_BEEP     0xA7
#define CMD_VENT_AUTO   0xA9
#define CMD_VENT_MANUAL 0xAA
#define CMD_PEST_0   0xD0     /* 扩展: 害虫等级注入 */
#define CMD_PEST_1   0xD1
#define CMD_PEST_2   0xD2
#define CMD_CAL      0xC0     /* 扩展: 重新标定声速基线 */

/* 扩展: 补光 LED (PA5, 软件 PWM) 的手动强执。
 * 命令码取 0xB5~0xB7 —— 0xB1~0xB4 已被 pc_host 的"从板上报/采声音"占用,
 * 0xA3/0xA4 是"窗户门开/关", 都不能复用, 故顺延到 B5 之后。
 * 平时 LED **跟随光照自动调光**(见 glamp_task), 这三条只用于现场验证与强制干预
 * (ON = 强制 100% 最亮, OFF = 强制 0% 熄灭);
 * 交回自动再发一次 CMD_GLAMP_AUTO 即可。 */
#define CMD_GLAMP_ON   0xB5
#define CMD_GLAMP_OFF  0xB6
#define CMD_GLAMP_AUTO 0xB7

/* 现场排障: 光照方向 / LED 极性都只能实测, 做成运行时可翻, 免得为每次猜测重传固件。
 * 这两条命令把对应的开关改成**运行时翻转**, 一次烧写就能把四种组合试全,
 * 避免为每一种猜测都往平台重传一遍固件。试出正确组合后再改宏固化。
 * 命令码 0xB8/0xB9 —— 0xB1~B4 是 pc_host 的"从板上报", 0xA3/A4 是"窗户门", 均不冲突。 */
#define CMD_LIGHT_INV  0xB8    /* 翻转光照方向: 电压高=亮 <-> 电压高=暗 */
#define CMD_GLAMP_POL  0xB9    /* 翻转 LED 极性: 高电平点亮 <-> 低电平点亮 */
#define CMD_FLAME_POL  0xBA    /* 翻转火焰传感器 DO 极性: 低=有火 <-> 高=有火 (2026-10-03) */

#define ACK_OK          0x00
#define ACK_UNKNOWN_CMD 0x01

static void pkt_send(uint8_t type, uint8_t seq, uint8_t total,
                     const uint8_t *data, uint8_t len)
{
    uint8_t cs = 0, i;
    cs ^= type; cs ^= seq; cs ^= total; cs ^= len;
    for (i = 0; i < len; i++) cs ^= data[i];

    u1_raw(0xAA); u1_raw(0x55);
    u1_raw(type); u1_raw(seq); u1_raw(total); u1_raw(len);
    for (i = 0; i < len; i++) u1_raw(data[i]);
    u1_raw(cs);
}

/*==============================================================================
 *                八、OLED 驱动 (SSD1306 128x64，软件 I2C)
 *
 *  平台元件 i2c_oled_128x64_096 内部是 SSD1306: 128 列 x 8 页, 每页 8 行像素,
 *  一个字节 = 同一列上的 8 个垂直像素 (bit0 在上, bit7 在下)。
 *  驱动策略 = "本地显存 + 整屏刷新":
 *      oled_clear / oled_pixel / oled_str ... 只改 RAM (oled_buf[1024])
 *      oled_flush() 再按页把 1024 字节连续写进 GRAM
 *  这样一次界面刷新只产生 8 次 I2C 事务, 软件 I2C 也不会成为负担。
 *  接线: SCL->PB6, SDA->PB7, VDD->3.3V, GND->GND。
 *============================================================================*/

static uint8_t oled_buf[OLED_BUF_SIZE];        /* 显存: 8 页 x 128 列 */

/*==============================================================================
 * I2C 传输层 —— 两种实现, 用 OLED_HW_I2C 切换
 *   oled_i2c_init()                 初始化引脚/外设
 *   oled_wr(ctrl, dat, n)           一次完整事务: START + 地址 + ctrl + n 字节 + STOP
 * 上层 (oled_cmd / oled_flush) 只认这两个接口, 换实现不动上层。
 *============================================================================*/

#if OLED_HW_I2C
/* ---------------- 硬件 I2C1 (PB6=SCL / PB7=SDA, 复用开漏) ---------------- */

static void oled_i2c_init(void)
{
    GPIO_InitTypeDef io;
    I2C_InitTypeDef  ic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    io.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    io.GPIO_Mode  = GPIO_Mode_AF_OD;          /* I2C 必须复用开漏 */
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &io);

    I2C_DeInit(I2C1);
    I2C_StructInit(&ic);
    ic.I2C_Mode                = I2C_Mode_I2C;
    ic.I2C_DutyCycle           = I2C_DutyCycle_2;
    ic.I2C_OwnAddress1         = 0x00;
    ic.I2C_Ack                 = I2C_Ack_Enable;
    ic.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    ic.I2C_ClockSpeed          = 400000;
    I2C_Init(I2C1, &ic);
    I2C_Cmd(I2C1, ENABLE);
}

/* 等事件带超时: 平台若不模拟 I2C 外设也只是白等 20ms, 不会死机 */
static uint8_t oled_i2c_wait(uint32_t evt)
{
    uint32_t t0 = millis();
    while (!I2C_CheckEvent(I2C1, evt)) {
        if ((millis() - t0) > 20) return 0;
    }
    return 1;
}

static void oled_wr(uint8_t ctrl, const uint8_t *dat, uint16_t n)
{
    uint16_t i;

    I2C_GenerateSTART(I2C1, ENABLE);
    if (!oled_i2c_wait(I2C_EVENT_MASTER_MODE_SELECT)) return;
    I2C_Send7bitAddress(I2C1, OLED_I2C_ADDR7, I2C_Direction_Transmitter);
    if (!oled_i2c_wait(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return;

    I2C_SendData(I2C1, ctrl);
    if (!oled_i2c_wait(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return;

    for (i = 0; i < n; i++) {
        I2C_SendData(I2C1, dat[i]);
        if (!oled_i2c_wait(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return;
    }
    I2C_GenerateSTOP(I2C1, ENABLE);
}

#else
/* ---------------- 软件 I2C (GPIO 位翻转, 默认) ----------------
 * SCL 推挽输出; SDA 用"推挽拉低 / 上拉输入释放"的经典做法, 不依赖外部上拉。 */

static void oled_i2c_init(void)
{
    GPIO_InitTypeDef io;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    io.GPIO_Pin   = GPIO_Pin_6;               /* SCL: 推挽输出 */
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &io);
    OLED_SCL_H();

    io.GPIO_Pin   = GPIO_Pin_7;               /* SDA: 上拉输入 = 释放总线 */
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &io);
    OLED_SDA_H();
}

static void i2c_dly(void)
{
#if OLED_I2C_DELAY_US
    delay_us(OLED_I2C_DELAY_US);
#endif
}

static void i2c_start(void)
{
    OLED_SDA_H(); OLED_SCL_H(); i2c_dly();
    OLED_SDA_L(); i2c_dly();      /* SCL 高电平期间 SDA 下降沿 = START */
    OLED_SCL_L(); i2c_dly();
}

static void i2c_stop(void)
{
    OLED_SDA_L(); i2c_dly();
    OLED_SCL_H(); i2c_dly();
    OLED_SDA_H(); i2c_dly();      /* SCL 高电平期间 SDA 上升沿 = STOP */
}

static void i2c_wr_byte(uint8_t b)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (b & 0x80) OLED_SDA_H(); else OLED_SDA_L();
        i2c_dly();
        OLED_SCL_H(); i2c_dly();
        OLED_SCL_L(); i2c_dly();
        b = (uint8_t)(b << 1);
    }
    /* 第 9 拍收 ACK: 只释放总线不判应答 —— 屏不在线也不会卡死 */
    OLED_SDA_H(); i2c_dly();
    OLED_SCL_H(); i2c_dly();
    OLED_SCL_L(); i2c_dly();
}

static void oled_wr(uint8_t ctrl, const uint8_t *dat, uint16_t n)
{
    uint16_t i;

    i2c_start();
    i2c_wr_byte(OLED_I2C_ADDR);     /* 从机地址 + 写 */
    i2c_wr_byte(ctrl);              /* 控制字节: 0x00=命令 / 0x40=数据 */
    for (i = 0; i < n; i++) i2c_wr_byte(dat[i]);
    i2c_stop();
}
#endif

/* ---------------- SSD1306 命令 / 刷屏 ---------------- */

static void oled_cmd(uint8_t c)
{
    uint8_t d = c;
    oled_wr(0x00, &d, 1);
}

static void oled_flush(void)
{
    uint8_t page;

    for (page = 0; page < OLED_PAGES; page++) {
        oled_cmd((uint8_t)(0xB0 | page));   /* 页地址 0~7 */
        oled_cmd(0x00);                     /* 列地址低 4 位 = 0 */
        oled_cmd(0x10);                     /* 列地址高 4 位 = 0 */
        oled_wr(0x40, &oled_buf[(uint16_t)page * OLED_W], OLED_W);   /* 128 字节数据 */
    }
}

/* ---------------- 显存操作 ---------------- */

static void oled_clear(void)
{
    uint16_t i;
    for (i = 0; i < OLED_BUF_SIZE; i++) oled_buf[i] = 0x00;
}

static void oled_fill(void)
{
    uint16_t i;
    for (i = 0; i < OLED_BUF_SIZE; i++) oled_buf[i] = 0xFF;
}

static void oled_pixel(uint16_t x, uint16_t y, uint8_t on)
{
    uint16_t idx;
    uint8_t  mask;

    if (x >= OLED_W || y >= OLED_H) return;
    idx  = (uint16_t)((y >> 3) * OLED_W + x);
    mask = (uint8_t)(1u << (y & 7));
    if (on) oled_buf[idx] |=  mask;
    else    oled_buf[idx] &= (uint8_t)(~mask);
}

static uint8_t oled_slen(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < 200) n++;
    return n;
}

/* ---------------- 文本 (6x12, 取自 font.h 的 asc2_1206) ----------------
 * asc2_1206 是行扫描格式: 每字符 12 字节, 每字节一行 6 像素。
 * 位序必须按 **bit0 为最左** 解 —— 这一点由字库实测确定, 别再想当然:
 *   'F' 首横 0x1F = 0b011111, 'L' 顶横 0x07 = 0b000111,
 *   只有 bit0 在左时横画才起于字符左缘; 若按 bit5 在左读, 整屏文字会
 *   逐字左右镜像(字符顺序却不变), 典型的“能认出来但是反的”。
 * 这里逐点搬进页格式显存, 无需任何字库转换表。 */
#define OLED_FONT_W   6
#define OLED_FONT_H   12
#define OLED_LINES    5                     /* 64 / 12 = 5 行 */

static void oled_char(uint16_t x, uint16_t y, char ch, uint8_t inv)
{
    const unsigned char *p;
    uint8_t idx = (uint8_t)ch;
    uint8_t row, col, on;

    if (idx < ' ' || idx > '~') idx = (uint8_t)' ';
    p = gt_font_1206[idx - ' '];

    for (row = 0; row < OLED_FONT_H; row++) {
        uint8_t bits = p[row];
        for (col = 0; col < OLED_FONT_W; col++) {
            on = (bits & (uint8_t)(1u << col)) ? 1 : 0;   /* bit0 = 最左像素 */
            if (inv) on = (uint8_t)(on ? 0 : 1);
            oled_pixel((uint16_t)(x + col), (uint16_t)(y + row), on);
        }
    }
}

static void oled_str(uint16_t x, uint16_t y, const char *s, uint8_t inv)
{
    while (*s) {
        if (x + OLED_FONT_W > OLED_W) return;
        oled_char(x, y, *s++, inv);
        x = (uint16_t)(x + OLED_FONT_W);
    }
}

/* 标题栏: 整行反白 + 左标题 + 右状态 */
static void oled_header(uint16_t y, const char *title, const char *right)
{
    uint16_t i, j;
    uint8_t  n;

    for (j = 0; j < OLED_FONT_H; j++)
        for (i = 0; i < OLED_W; i++)
            oled_pixel(i, (uint16_t)(y + j), 1);

    oled_str(0, y, title, 1);

    if (right && *right) {
        n = oled_slen(right);
        i = (uint16_t)(OLED_W - (uint16_t)n * OLED_FONT_W);
        oled_str(i, y, right, 1);
    }
}

/* ---------------- SSD1306 初始化 ---------------- */

static void oled_init(void)
{
    delay_ms(100);                        /* 上电稳定 */

    oled_cmd(0xAE);                       /* Display OFF */
    oled_cmd(0xD5); oled_cmd(0x80);       /* 时钟分频 / 振荡频率 */
    oled_cmd(0xA8); oled_cmd(0x3F);       /* 多路复用比 = 64 行 */
    oled_cmd(0xD3); oled_cmd(0x00);       /* 显示偏移 = 0 */
    oled_cmd(0x40);                       /* 显示起始行 = 0 */
    oled_cmd(0x8D); oled_cmd(0x14);       /* 电荷泵使能 */
    oled_cmd(0x20); oled_cmd(0x00);       /* 寻址模式 = 页模式 */
    oled_cmd(0xA1);                       /* 段重映射: 左右不反 */
    oled_cmd(0xC8);                       /* 行扫描方向: 上下不反 */
    oled_cmd(0xDA); oled_cmd(0x12);       /* COM 引脚配置 */
    oled_cmd(0x81); oled_cmd(0xCF);       /* 对比度 */
    oled_cmd(0xD9); oled_cmd(0xF1);       /* 预充电周期 */
    oled_cmd(0xDB); oled_cmd(0x40);       /* VCOMH 去耦电平 */
    oled_cmd(0xA4);                       /* 显示内容跟随显存 */
    oled_cmd(0xA6);                       /* 正常显示 (非反色) */
    oled_cmd(0xAF);                       /* Display ON */
    delay_ms(50);

    oled_clear();
    oled_flush();
}


/*==============================================================================
 *                          九、传感器采集
 *============================================================================*/

/* 字符串拼装工具在第十三节定义, 这里先声明 (DHT 探测要用它们打诊断信息) */
static char *s_put(char *p, const char *s);
static char *s_dec(char *p, int32_t v, uint8_t dec);

/* ---------- DHT11 单总线读取 (引脚可指定, 带实测诊断) ----------
 * 返回 0=成功; 1=无响应(传感器不在线/接错脚/没供电); 2=校验和错。
 * raw5 非空时把收到的 5 字节原样带回, 便于串口诊断。
 * 时序与平台模板 HARDWARE/DHT11/dht11.c 一致 (该文件 DHT11_Check/Read_Bit):
 *   主机拉低 ≥18ms → 释放 20~40µs → 从机回 80µs 低 + 80µs 高 →
 *   每 bit: 50µs 低 + (26~28µs=0 / 70µs=1) 高。
 *   位判决用"量高电平宽度"而不是"固定 40µs 后采样" —— 后者在帧尾(最后一位
 *   之后从机就释放总线了, 线一直是高)会把 0 误判成 1, 且依赖延时精度。
 * ★ 延时全部走 us_dly()/ms_dly() (TIM3 时基), 不用平台 delay_us() —— 见四-B节。 */

/* 最近一次读取实测到的从机应答 (µs; 0xFFFF = 没等到该电平)
 *   g_dht_lo : 主机释放总线 → 从机把线拉低, 中间等了多久 (标准 20~40µs)
 *   g_dht_hi : 从机这根低电平持续了多久                (标准 ~80µs)
 * 两个都有值 = 这根脚上确实挂着传感器; 都是 0xFFFF = 没有任何器件拉低总线。 */
static uint16_t g_dht_lo = 0xFFFF;
static uint16_t g_dht_hi = 0xFFFF;

/* 等某个电平出现, 返回等待耗时(µs); 超时返回 0xFFFF */
static uint16_t dht_wait(uint8_t id, uint8_t want, uint16_t timeo_us)
{
    uint16_t c0, t = 0;

    if (g_us_ok) {
        c0 = TIM_GetCounter(US_TIM);
        while (dht_get(id) != want) {
            if ((uint16_t)(TIM_GetCounter(US_TIM) - c0) >= timeo_us) return 0xFFFF;
        }
        return (uint16_t)(TIM_GetCounter(US_TIM) - c0);
    }
    while (dht_get(id) != want) {
        if (t >= timeo_us) return 0xFFFF;
        delay_us(1);
        t++;
    }
    return t;
}

static uint8_t dht_read_pin(uint8_t id, int16_t *t_x10, int16_t *rh_x10, uint8_t *raw5)
{
    uint8_t  buf[5], i, j;
    uint16_t v;

    g_dht_lo = 0xFFFF; g_dht_hi = 0xFFFF;

    dht_pin_out(id);
    dht_clr(id);
    ms_dly(20);                   /* 起始信号 ≥18ms */
    dht_set(id);
    us_dly(30);                   /* 主机释放 20~40µs, 等从机应答 */
    dht_pin_in(id);

    v = dht_wait(id, 0, 500);     /* 等从机把线拉低 (~80µs) */
    if (v == 0xFFFF) return 1;
    g_dht_lo = v;
    v = dht_wait(id, 1, 500);     /* 等这 80µs 低结束 */
    if (v == 0xFFFF) return 1;
    g_dht_hi = v;

    for (i = 0; i < 5; i++) {
        uint8_t byte = 0;
        for (j = 0; j < 8; j++) {
            uint16_t w;
            if (dht_wait(id, 0, 500) == 0xFFFF) return 1;   /* 每个 bit 先 50µs 低 */
            if (dht_wait(id, 1, 500) == 0xFFFF) return 1;   /* 低结束 → 数据高电平开始 */

            /* 量这根高电平有多宽来判 bit: 26~28µs=0 / 70µs=1。
               为什么不用"固定延时 40µs 后采样": 那一帧的最后一位后面总线就被
               从机释放了(一直是高), 固定采样会把它误判成 1; 量宽度则天然避开
               这个坑, 而且不依赖延时精度(±10µs 都不影响判决)。 */
            w = dht_wait(id, 0, 150);
            byte = (uint8_t)(byte << 1);
            if (w != 0xFFFF && w >= 45) byte |= 1;          /* 150µs 仍不下降 = 帧尾, 按 0 */
        }
        buf[i] = byte;
    }

    if (raw5) { for (i = 0; i < 5; i++) raw5[i] = buf[i]; }

    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) return 2;

    *rh_x10 = (int16_t)(buf[0] * 10 + (buf[1] & 0x7F));   /* DHT11 小数位常为 0, 兼容 DHT22 风格 */
    *t_x10  = (int16_t)(buf[2] * 10 + (buf[3] & 0x7F));
    return 0;
}

/* 最近一次 DHT 读取收到的原始 5 字节 (诊断用: 校验错时打出来看) */
static uint8_t dht_raw[5];

/* 读**舱外** DHT11 (用开机扫描认到的脚) —— 主循环用 */
static uint8_t dht_read(int16_t *t_x10, int16_t *rh_x10)
{
    return dht_read_pin(g_dht_pin, t_x10, rh_x10, dht_raw);
}

/* 读**仓内** DHT11 (固定 DHT_IN_PIN, 默认 PA14)。
 * 两路各自独立读取、各自独立报错, 一路坏不影响另一路 ——
 * 内外温湿度差是通风决策的唯一依据, 缺任何一路都必须显式暴露,
 * 绝不允许拿另一路的值顶替(那等于编数据)。 */
static uint8_t dht_read_in(int16_t *t_x10, int16_t *rh_x10)
{
    return dht_read_pin(g_dht2_pin, t_x10, rh_x10, 0);
}

/*==============================================================================
 *                 十五-B、DHT11 引脚自动探测 (2026-10-01)
 *
 *  为什么要扫描: 接线是人接的, 固件写死一根脚经常和实际不符 —— 表现就是屏上
 *  温湿度永远 --.-, 而人还在怀疑代码。所以开机把所有**空闲脚**各做一次完整
 *  握手, 哪个脚应答就用哪个脚, 并把"每个脚的结果 + 实测应答宽度"当场显示:
 *      L---H---  没等到从机应答 → 这根脚上没有传感器 (或 VCC/GND 没接)
 *      L80 H80   应答正常       → 传感器数据线就在这根脚上
 *      有 L/H 但结果=2(校验错)  → 线对了, 是时序/干扰问题, 要查驱动
 *
 *  已占用的脚绝不拉低 (2026-10-01 更新 —— 风扇与仓内 DHT 又占掉一批):
 *    PA0 超声ECHO  PA2 TRIG  PA5/6 ADC  PA7 MQ-DO  PA8 粉尘LED
 *    PA9/10 USART1  PA11 诱虫灯  PA12 蜂鸣器
 *    PA14 = 仓内 DHT11 固定脚(不参与扫描)
 *    PA15/PB3/PB4/PB5 = FAN1~FAN4 的定时器 PWM 输出 —— **绝不能**被单总线的
 *        扫描脉冲拉来拉去(会干扰风扇, 也可能把风扇波形误当成传感器应答)
 *    PB6/PB7 OLED-I2C  PB10/PB11 USART3(ESP8266)
 *    PB0 = **火焰传感器 DO** (2026-10-03 新增; 数字输入, 同样不能进扫描表)
 *  (PA13 随 SWJ 一起释放了, 本设计不用它)
 *============================================================================*/

static const uint8_t dht_cand[] = {
    /* ★ PA2/PA3 已给 **USART2(485 风速)**、PA4 已给光敏电阻的 ADC、PA5 已给补光 LED、
     *   PA6/PA7 已给 MQ-135: 这些脚**绝不能进扫描表** —— 扫描会把它当单总线拉高拉低,
     *   轻则把 ADC 读数搅乱, 重则把 LED 闪一下、把 USART2 的接收线来回拽。 */
    DHT_PA(1),  DHT_PA(0),   /* 最可能的 2 个 PA 脚在前 */
    /* ★ PB0 已给**火焰传感器 DO**(2026-10-03): 从扫描表剔除 —— 否则单总线扫描
     *   会把这个脚拉高拉低, 既干扰 DO 读数, 也可能把模块输出误当成 DHT 应答。
     * ★ PB1 已给**超声波 TRIG**、PB12 已给**报警灯**(同日让位后的新脚): 同样剔除 ——
     *   扫描会把 TRIG 拉出一串假脉冲(超声波乱读数), 把报警灯闪成乱码。 */
    /* ★ PB2 已给**北斗软串口 RX**(2026-10-04): **剔除** —— 单总线扫描会把这根线
     *   拉高拉低, 把 NMEA 数据流搅乱。它现在只接北斗模块, 不参与 DHT 扫描。 */
    DHT_PB(8),  DHT_PB(9),   /* PB12 已给报警灯、PB3/4/5 已给风扇, 见上 */
    DHT_PB(13), DHT_PB(14), DHT_PB(15)
};
#define DHT_CAND_N  (sizeof(dht_cand) / sizeof(dht_cand[0]))

static uint8_t  dht_res[DHT_CAND_N];      /* 每脚结果: 0=成功 1=无响应 2=校验错 */
static uint16_t dht_plo[DHT_CAND_N];      /* 每脚实测应答低电平宽度 (µs) */
static uint16_t dht_phi[DHT_CAND_N];      /* 每脚实测应答高电平宽度 (µs) */
static uint8_t  g_scan_i = 0;             /* 主循环轮询扫描下标 */

/* 追加 "PA3" / "PB5" */
static char *s_dhtpin(char *p, uint8_t id)
{
    p = s_put(p, (id & 0x10u) ? "PB" : "PA");
    return s_dec(p, dht_num(id), 0);
}

/* 扫一遍全部候选脚; 返回第一个成功的脚 id (0xFF = 一个都没应答),
 * 并把该脚读到的温湿度带走 (避免紧接着重读一次 —— DHT11 两次采样要求间隔) */
static uint8_t dht_scan(int16_t *t_out, int16_t *h_out)
{
    uint8_t i, found = 0xFF;
    char    b[64];
    char   *p;

    for (i = 0; i < DHT_CAND_N; i++) {
        int16_t t = 0, h = 0;
        uint8_t st;

        if (dht_cand[i] == (uint8_t)DHT_IN_PIN) {   /* 仓内的固定脚, 不当舱外候选 */
            dht_res[i] = 1; dht_plo[i] = 0xFFFF; dht_phi[i] = 0xFFFF;
            continue;
        }

        st = dht_read_pin(dht_cand[i], &t, &h, 0);

        dht_res[i] = st;
        dht_plo[i] = g_dht_lo;
        dht_phi[i] = g_dht_hi;

        p = b;
        p = s_put(p, "[GT] dht scan ");
        p = s_dhtpin(p, dht_cand[i]);
        p = s_put(p, " -> ");
        p = s_dec(p, st, 0);
        p = s_put(p, " ack L=");
        if (g_dht_lo != 0xFFFF) p = s_dec(p, g_dht_lo, 0); else p = s_put(p, "--");
        p = s_put(p, "us H=");
        if (g_dht_hi != 0xFFFF) p = s_dec(p, g_dht_hi, 0); else p = s_put(p, "--");
        p = s_put(p, "us\r\n");
        u1_print(b);

        if (st == 0 && found == 0xFF) {
            found = dht_cand[i];
            *t_out = t;
            *h_out = h;
        }
        if (st != 1) ms_dly(60);          /* 有应答才缓一下 (DHT11 采样间隔要求) */
    }
    return found;
}

/* 开机探测: 舱外自动扫脚 + 仓内固定脚, 两路都探 */
static void dht_autodetect(void)
{
    uint8_t id;
    int16_t t = 0, h = 0;
    char    b[80];
    char   *p;

    /* ---------------- 舱外 DHT11: 扫遍所有空闲脚, 哪根应答就用哪根 ---------------- */
    id = dht_scan(&t, &h);

    if (id != 0xFF) {
        g_dht_pin  = id;
        g.temp_x10 = t; g.rh_x10 = h;
        g.dht_ok   = 1; g.dht_ever = 1;
        p = b;
        p = s_put(p, "[GT] DHT11 OUT OK on ");
        p = s_dhtpin(p, id);
        p = s_put(p, "  T=");
        p = s_dec(p, t, 1);
        p = s_put(p, "C RH=");
        p = s_dec(p, h, 1);
        p = s_put(p, "%\r\n");
        u1_print(b);
    } else {
        g_dht_pin = DHT_PIN_DEFAULT;
        g.dht_ok  = 0;
        u1_print("[GT] DHT11 OUT NOT FOUND on any free pin\r\n");
        u1_print("[GT]   -> 舱外温湿度显示 --.- ; 先查 DHT11 的 VCC(3.3V)/GND/DATA\r\n");
        u1_print("[GT]   -> DATA 别接在已占用脚: 风扇 PA15/PB3/PB4/PB5, 仓内 PA14,\r\n");
        u1_print("[GT]      PA2/PA3(485) PA5/PA8/PA11/PB0/PB1/PB6/PB7/PB10/PB11/PB12\r\n");
    }

    /* DHT11 两次采样间隔要求 ≥1s: 扫完舱外要歇一下再读仓内 */
    ms_dly(1200);

    /* ---------------- 仓内 DHT11: 固定脚(DHT_IN_PIN), 不扫描 ---------------- */
    {
        uint8_t st = dht_read_in(&t, &h);

        p = b;
        p = s_put(p, "[GT] DHT11 IN  on ");
        p = s_dhtpin(p, g_dht2_pin);

        if (st == 0) {
            g.in_temp_x10 = t; g.in_rh_x10 = h;
            g.dht_in_ok = 1; g.dht_in_ever = 1;
            p = s_put(p, " OK  T=");
            p = s_dec(p, t, 1);
            p = s_put(p, "C RH=");
            p = s_dec(p, h, 1);
            p = s_put(p, "%\r\n");
        } else {
            g.dht_in_ok = 0;
            p = s_put(p, (st == 2) ? " checksum error\r\n" : " no response\r\n");
        }
        u1_print(b);

#if DHT_IN_PA14
        if (st != 0)
            u1_print("[GT]   -> 仓内 DHT11 接 PA14; 它是 SWD 的 SWCLK 脚, "
                     "已全关 SWJ 释放; 查 VCC/GND/DATA 三线\r\n");
#endif
    }

    ms_dly(1200);    /* 再歇一下, 让紧接着的主循环首读也满足间隔要求 */
}

#if DHT_DIAG_MODE
/* 开机两屏诊断 —— 不接串口也能一眼看清 */
static void dht_diag_screen(void)
{
    char    l[26];
    char   *p;
    uint8_t i;

    /* ============ 第 1 屏「结论屏」: 两路 DHT11 + 4 路风扇 + SWJ 状态 ============ */
    oled_clear();
    oled_header(UI_L0, "DHT DIAG",
                !g_us_ok ? "CLK:?" : (g_sys_ok ? "SYS:OK" : "SYS:STUB"));

    /* 舱外温湿度 (脚由扫描认到) */
    p = l;
    p = s_put(p, "OUT ");
    p = s_dhtpin(p, g.dht_ok ? g_dht_pin : DHT_PIN_DEFAULT);
    p = s_put(p, " ");
    if (g.dht_ok) {
        p = s_dec(p, g.temp_x10, 1); p = s_put(p, "C ");
        p = s_dec(p, g.rh_x10, 1);   p = s_put(p, "%");
    } else {
        p = s_put(p, "NO DATA");
    }
    oled_str(0, UI_L1, l, 0);

    /* 仓内温湿度 (固定 DHT_IN_PIN) */
    p = l;
    p = s_put(p, "IN  ");
    p = s_dhtpin(p, g_dht2_pin);
    p = s_put(p, " ");
    if (g.dht_in_ok) {
        p = s_dec(p, g.in_temp_x10, 1); p = s_put(p, "C ");
        p = s_dec(p, g.in_rh_x10, 1);   p = s_put(p, "%");
    } else {
        p = s_put(p, "NO DATA");
    }
    oled_str(0, UI_L2, l, 0);

    /* 4 路风扇各自占空比: "FAN 88/88/00/00" */
    p = l;
    p = s_put(p, "FAN ");
    for (i = 0; i < 4u; i++) {
        if (i) p = s_put(p, "/");
        if (g.fan[i] < 10u) p = s_put(p, "0");
        p = s_dec(p, g.fan[i], 0);
    }
    oled_str(0, UI_L3, l, 0);

    /* SWJ 状态: 为了这 4 路风扇和仓内 DHT11, 调试脚让出了哪些 */
#if DHT_IN_PA14
    s_put(l, "SWJ:OFF (PA14=DHT2)");
#else
    /* ★ 别把脚号写死 —— DHT_IN_ALT 已经改过三轮(PA4 -> PB2 -> PA8),
     *   写死的字符串不会跟着变, 屏幕就会**报一个错的脚号**让人照着接错线。
     *   用 s_dhtpin() 按 pin id 输出, 以后改宏屏幕自动跟着对。 */
    p = l;
    p = s_put(p, "SWJ:SWD on DHT2=");
    p = s_dhtpin(p, (uint8_t)DHT_IN_ALT);
#endif
    oled_str(0, UI_L4, l, 0);
    oled_flush();
    ms_dly((uint16_t)DHT_DIAG_HOLD_MS);

    /* ============ 第 2 屏「明细屏」: 12 个候选脚各自的握手结果 ============ */
    oled_clear();
    p = l;
    if (g.dht_ok) { p = s_put(p, "USE "); p = s_dhtpin(p, g_dht_pin); }
    else          { p = s_put(p, "NONE"); }
    oled_header(UI_L0, "DHT SCAN", l);
    /* 候选脚按 "A0:1 B12:1 ..." 排, 放不下就自动换行 (21 字符/行) */
    {
        uint8_t idx = 0u, ln = 0u;
        while (idx < DHT_CAND_N && ln < 4u) {
            p = l;
            while (idx < DHT_CAND_N) {
                char    tmp[8];
                char   *q = tmp;
                uint8_t w;

                q = s_put(q, (dht_cand[idx] & 0x10u) ? "B" : "A");
                q = s_dec(q, dht_num(dht_cand[idx]), 0);
                q = s_put(q, ":");
                q = s_dec(q, dht_res[idx], 0);
                w = (uint8_t)(oled_slen(tmp) + ((p == l) ? 0u : 1u));
                if ((uint8_t)((uint16_t)(p - l) + w) > 21u) break;   /* 本行放不下 */
                if (p != l) p = s_put(p, " ");
                p = s_put(p, tmp);
                idx++;
            }
            *p = '\0';
            oled_str(0, (uint16_t)(UI_L1 + ln * 12u), l, 0);
            ln++;
        }
    }
    oled_flush();
    ms_dly((uint16_t)DHT_DIAG_HOLD_MS);
}
#endif


/* ---------- HC-SR04 (Trig=PB1, Echo=PA0/TIM2_CH1) ----------
 * ★ 2026-10-03: Trig 由 PA2 让位给 USART2_TX(485-B)。 */
static int32_t hc_read_us(void)
{
#if USE_TIM_CAPTURE
    uint16_t c1, c2;
    uint16_t wait;

    TIM_SetCounter(TIM2, 0);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC1);
    TIM_OC1PolarityConfig(TIM2, TIM_ICPolarity_Rising);

    HC_TRIG_L(); delay_us(2);
    HC_TRIG_H(); delay_us(12);
    HC_TRIG_L();

    wait = 0;
    while (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1) == RESET) {
        delay_us(10);
        if (++wait > 3000) return -1;        /* 30ms 超时 */
    }
    c1 = TIM_GetCapture1(TIM2);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC1);
    TIM_OC1PolarityConfig(TIM2, TIM_ICPolarity_Falling);

    wait = 0;
    while (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1) == RESET) {
        delay_us(10);
        if (++wait > 3000) return -1;
    }
    c2 = TIM_GetCapture1(TIM2);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC1);

    if (c2 >= c1) return (int32_t)(c2 - c1);
    return (int32_t)(65536u + c2 - c1);
#else
    /* 电平轮询 (USE_TIM_CAPTURE=0, 本版默认):
     * 用 US_TIM(1MHz) 量 ECHO 高电平宽度, 完全不依赖平台 delay_us 的精度 ——
     * 拉高 TRIG 12µs, 等 ECHO 上升沿(最长 30ms), 再量到下降沿为止。 */
    uint16_t c0, c1;
    uint32_t to = 0;

    HC_TRIG_L(); us_dly(2);
    HC_TRIG_H(); us_dly(12);
    HC_TRIG_L();

    if (!g_us_ok) {                       /* 独立时基不可用: 退回平台 delay_us 老办法 */
        uint32_t us   = 0;
        uint16_t wait = 0;
        while (!HC_ECHO_IN() && wait < 3000) { wait++; delay_us(10); }
        if (wait >= 3000) return -1;
        while (HC_ECHO_IN() && us < 40000u) { delay_us(1); us++; }
        return (int32_t)us;
    }

    while (!HC_ECHO_IN()) {               /* 等回波上升沿 */
        if (++to > 3000000u) return -1;   /* 约 30ms 无回波 → 超时 */
    }
    c0 = TIM_GetCounter(US_TIM);
    to = 0;
    while (HC_ECHO_IN()) {                /* 量高电平宽度 = 往返飞行时间 */
        if (++to > 3000000u) return -1;
    }
    c1 = TIM_GetCounter(US_TIM);
    return (int32_t)(uint16_t)(c1 - c0);
#endif
}

/* ---------- GP2Y1014AU 粉尘 (LED=PA8 低电平点亮, VO=PA6/ADC1_IN6) ----------
 * ★ 2026-10-02 起**整路作废**: PA6 让给 MQ-135 的 AO, 粉尘不再采样。
 *   与"气体作废"同一条规矩: 不是"读成 0 再拿去比阈值", 而是
 *   ① 不采样(连 PA8 的 LED 驱动都不碰); ② 判据里摘掉; ③ 三处显示 -- 。 */
#if DUST_ENABLE
static uint16_t dust_read(void)
{
    uint16_t adc;
    float    volt;
    int32_t  ug;

    GP2Y_LED_ON();               /* 拉低点亮 */
    delay_us(280);               /* 等待光路稳定 */
    adc  = adc_read_safe(ADC_Channel_6);
    delay_us(40);
    GP2Y_LED_OFF();
    delay_us(9680);              /* 补足 10ms 周期 */

    volt = (float)adc * 3.3f / 4096.0f;
    ug   = (int32_t)((volt - 0.60f) * 200.0f);   /* 0.6V 基线, 200µg/m³ 每伏 */
    if (ug < 0)     ug = 0;
    if (ug > 1000)  ug = 1000;
    return (uint16_t)ug;
}
#endif

/* =============================================================================
 *          ★★ 模拟量两点定标 (2026-10-04) —— 光敏 / MQ-135 共用
 * -----------------------------------------------------------------------------
 * 为什么需要: 平台这两个元件在**前端**只发 `proportion`(滑条 0~100%), 真正的
 *   "百分比 → 电压"是**服务端模型**算的, 前端读不到。实测 MQ-135 满偏只有 1287
 *   (不是 12 位的 4095), 光敏又是另一个值 ⇒ 过去写死 /4095 必然算错。
 *
 * 办法: 两点线性定标。LO/HI = 滑条最低/最高处的**原始 ADC 码**,
 *       现场用串口命令把当时的 raw 锁进去(不用改代码、不用重传固件):
 *           光敏:    q = 锁定暗端(记作 0%)     r = 锁定亮端(记作 100%)
 *           MQ-135:  s = 锁定零端(记作 0.0‰)   t = 锁定满端(记作 100.0‰)
 *       现场步骤: 滑条拖到一端 → 按一次键 → 拖到另一端 → 再按一次键。
 *
 * ★ 端点数不出来(hi <= lo)时**判无效**(三处显示 --), 绝不拿 0 或某个
 *   "看起来合理"的百分比糊过去 —— 项目硬规矩第 1 条。
 * ========================================================================== */
static uint16_t g_light_lo = LIGHT_ADC_LO;   /* 光敏: 暗端 raw */
static uint16_t g_light_hi = LIGHT_ADC_HI;   /* 光敏: 亮端 raw */
static uint16_t g_gas_lo   = 0u;             /* MQ-135: 零端 raw (实测为 0) */
static uint16_t g_gas_hi   = 1287u;          /* MQ-135: 满端 raw (★ 用户实测 0~1287) */

/* raw → 0~span 的线性映射; hi<=lo 视为未标定, 返回 0 并由调用方置无效 */
static uint16_t cal_map(uint16_t raw, uint16_t lo, uint16_t hi, uint16_t span)
{
    uint32_t v;
    if (hi <= lo)  return 0u;
    if (raw <= lo) return 0u;
    v = (uint32_t)(raw - lo) * (uint32_t)span / (uint32_t)(hi - lo);
    if (v > (uint32_t)span) v = (uint32_t)span;
    return (uint16_t)v;
}

#if GAS_ENABLE
/* ---------- MQ-135 (2026-10-02 起: AO=PA6/ADC1_IN6, DO=PA7) ----------
 * ★ 换脚原因: PA6 原是粉尘 VO; 用户要 MQ-135 的 AO 接 PA6, 粉尘整路作废(DUST_ENABLE=0)。
 * ★ 气体这一路**已恢复**(GAS_ENABLE=1), 本函数正常参与编译。 */
static void gas_read(uint16_t *pct_x10, uint8_t *do_alarm, uint8_t *ok)
{
    uint16_t adc = adc_read_avg_safe(ADC_Channel_6, 8);
    /* ★★ 两点定标(见 cal_map): 端点没标出来就判无效, 不给编造的浓度 */
    uint16_t p   = cal_map(adc, g_gas_lo, g_gas_hi, 1000u);

    g.gas_adc = adc;                        /* 原始码打串口, 定标时就看它 */

    /* ★★ 与光敏电阻(light_read)同一个口径: **ADC 超时返回 0 → 这一路判无效**。
     *   理由: MQ-135 的 AO 是模拟电压, 没接/读失败时本来就接近 0V;
     *   若照样算出 "0.0‰" 显示出去, 就把"传感器掉了"伪装成了"空气很干净" ——
     *   正是本项目明令禁止的"看起来正常的兜底值"。
     *   无效时: pct=0 / do=0 / ok=0, 屏·网页·串口三处显示 --, 且不参与告警判据。 */
    if (adc == 0u || g_gas_hi <= g_gas_lo) {
        *pct_x10 = 0u; *do_alarm = 0u; *ok = 0u; return;
    }

    *pct_x10  = (uint16_t)p;
    *do_alarm = MQ_DO_ALARM() ? 1 : 0;
    *ok       = 1u;
}
#endif

/* ★ 2026-10-03 编译修复: 这三个变量原来被插在 light 段(在 flame_read() 之**后**),
 *   而 flame_read() 里要用 g_flame_alow ⇒ 平台报 'g_flame_alow' undeclared。
 *   C 必须先声明后使用, 所以挪到函数之前。
 *   ★ 保持**文件作用域且不加 FLAME_ENABLE 守卫** —— 因为三处引用点都在守卫之外:
 *     告警函数里的火警蜂鸣(无条件)、JSON 组包、串口状态行。
 *     加守卫会让 FLAME_ENABLE=0 变体编译不过。
 *   教训: 本机没有 arm-none-eabi-gcc, 这类"位置错"只能靠 _check12.py 静态兜住。 */
static uint8_t  g_flame_alow    = FLAME_ACTIVE_LOW;   /* 1 = DO 低电平=有火 (运行时可翻) */
static uint32_t g_flame_beep_t  = 0;                  /* 上次火警蜂鸣的起点 (ms) */
static uint8_t  g_flame_beep_on = 0;                  /* 1 = 蜂鸣器正响着(等 1s 后停) */

#if FLAME_ENABLE
/* ---------- 火焰传感器 (DO → PB0) ----------
 * 只做两件事:
 *   ① 照实读**原始电平** (排障依据, 与极性无关);
 *   ② 按**当前运行时极性**判"有没有火"。
 * 极性来源见 g_flame_alow —— 高/低=有火由服务端决定, 这里不写死。 */
static void flame_read(void)
{
    uint8_t v = FLAME_RAW();

    g.flame_raw = v;
    g.flame     = (uint8_t)(g_flame_alow ? (v == 0u) : (v != 0u));
}
#endif

/* =============================================================================
 *              九、光敏电阻模块 (PA4 / ADC1_IN4) = 舱外光照
 * -----------------------------------------------------------------------------
 * ★ 这个元件**不需要任何协议代码**: 从平台前端分块里读到它的组件实现是
 *     watch: photosensitive_resistance_adc →
 *        send({ type:"analog_type", id:`${node}.ADCx_IN${ch}`, data: 电压 })
 *   即**前端自己算出它接在哪个 ADC 通道, 再把电压直接灌进仿真**。
 *   所以固件只要读 ADC_Channel_4 就行, 一行解码都不用写。
 *
 * ★ 但"电压高 = 亮 还是 = 暗"**读不出来** —— 电压是服务端算的, 前端只转发。
 *   所以这里只做**线性归一化**(0 → 0%, 满量程 → 100%), 方向交给 LIGHT_INVERT,
 *   并由串口每次采集都打出 raw / mV, 让你拖一下元件上的滑条就能当场定方向:
 *       滑条 100% 时 raw 更大 → LIGHT_INVERT = 0 (默认)
 *       滑条 0%   时 raw 更大 → LIGHT_INVERT = 1
 *
 * ★ 有效性判据: 只判"ADC 超时/完全没电压"这一种硬故障。
 *   注意**不能**用"电压 = 0 就是没接" —— 全黑时电压本来就可能接近 0,
 *   那样会把"夜里"误判成"传感器掉了"。所以这里的口径是:
 *     - ADC 返回 0(超时)         → 无效 (light_ok = 0)
 *     - 有读数                   → 有效, 哪怕它是 0%(真黑)
 *   这与"温湿度不给兜底值"是同一条规矩: 宁可显示 --, 也不虚构一个"50%"出来。
 * ========================================================================== */

/* ★★ 2026-10-02 现场排障改造: 下面这两个"方向/极性"原来是**编译期宏**。
 *    那意味着每猜错一次就要往平台上传一遍固件重编译 —— 一轮往返很贵, 而且
 *    **"补光灯一直亮"这个现象有两种完全不同的根因, 光看现象分不出来**:
 *
 *      ① 光照方向反了: 平台实际是"光越强 → 电压越低"(光敏电阻分压的常见接法),
 *         而我们默认"电压越高越亮" ⇒ 把滑条拉到最亮, light_pct 反而很低
 *         ⇒ 判成"天黑" ⇒ 点亮。改 LIGHT_INVERT 即可。
 *      ② 灯的极性反了: 平台实际是"低电平点亮", 而我们按"高电平点亮"驱动
 *         ⇒ 与光照完全无关, 该亮的时候灭、该灭的时候亮。
 *
 *    两者表现一模一样, 只能靠数据区分 —— 判据是**屏上 L 后面的数字**:
 *        亮度拉满时 L 显示很低(如 05%) → 根因是 ①;
 *        亮度拉满时 L 显示很高(如 92%) 却还亮 → 根因是 ②。
 *
 *    所以这里把它们改成**运行时变量**(初值仍由宏给), 再配两条命令
 *    (0xB8 翻方向 / 0xB9 翻极性) —— 一次烧写就能把四种组合现场试全,
 *    试出对的那一组再把宏改掉固化, 不用为每一次猜测都重传固件。 */
static uint8_t g_light_invert = LIGHT_INVERT;      /* 0 = 电压越高越亮; 1 = 电压越高越暗 */
static uint8_t g_glamp_ahigh  = GLAMP_ACTIVE_HIGH; /* 1 = 高电平点亮; 0 = 低电平点亮 */
static uint8_t g_gled_duty    = 0;                 /* 补光 LED 当前占空比 0~100(%);
                                                    * 由 glamp_task() 按光照算出, 软件 PWM 逐片消费 */
static uint32_t g_glamp_hold  = 0;                 /* 手动强制的起始时刻(millis), 用于 5s 超时 */

static void light_read(void)
{
    uint16_t adc;
    uint32_t mv;

#if !LIGHT_ENABLE
    /* 整路关闭: 与"气体作废"同一口径 —— 标成无效, 而不是给一个 50% 之类的正常值 */
    g.light_ok = 0; g.light_pct = 0; g.light_adc = 0; g.light_mv = 0;
    return;
#endif

    adc = adc_read_avg_safe(LIGHT_ADC_CH, LIGHT_AVG_N);
    mv  = (uint32_t)adc * LIGHT_MV_FULL / LIGHT_ADC_FULL;

    g.light_adc = adc;
    g.light_mv  = (uint16_t)mv;

    /* ADC 超时返回 0 → 这一路判为无效; 屏/网页/串口三处显示 -- */
    if (adc == 0u) {
        g.light_ok  = 0;
        g.light_pct = 0;
        return;
    }
    g.light_ok = 1;

    /* 归一化到 0~100% —— ★★ 两点定标(见 cal_map), 不再拿 4095 硬除 */
    {
        uint32_t pct;
        if (g_light_hi <= g_light_lo) {          /* 端点没标出来 → 显式无效 */
            g.light_ok  = 0;
            g.light_pct = 0;
            return;
        }
        pct = cal_map(adc, g_light_lo, g_light_hi, 100u);
        /* 运行时可翻(命令 0xB8), 见上面"现场排障改造"那段注释 */
        if (g_light_invert) pct = 100u - pct;
        if (pct > 100u) pct = 100u;
        g.light_pct = (uint8_t)pct;
    }
}

/*==============================================================================
 *                          十、执行器
 *============================================================================*/

#if LAMP_KIND == 0
/* 状态灯(旧方案)。函数名保留 rgb_set(调用点不必跟着改), 但实现已从"PC13/PC14/PC15
 * 三路开关、亮度只能过阈值近似"换成 PA3 上的 SK6812 单总线灯(四-B 节) —— 现在 0~255
 * 是真正按分量给的颜色, 黄灯是真正的琥珀色而不是"红+绿各一半"。 */
static void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    sk_set(r, g, b);
}
#else
/* ================= 报警灯 (AlarmLight, PB12) =================
 * 单色灯只有"亮/灭"两态, 所以"绿/琥珀/红"三档等级改由**闪烁节奏**表达:
 *     等级 0 正常  -> 灭
 *     等级 1 预警  -> 慢闪 500ms 亮 / 500ms 灭 (1Hz, 不打扰)
 *     等级 2 报警  -> 快闪 150ms 亮 / 150ms 灭 (≈3.3Hz, 一眼看出是最高级)
 * 由 alarm_lamp_task() 在主循环 10ms 节拍里调用 —— **不阻塞、不 delay**,
 * 所以哪怕告警刷屏也和网络/采集互不影响。 */

/* 按当前等级刷一次灯状态(只算"这一刻该亮还是该灭") */
static void alarm_lamp_task(void)
{
    uint32_t t = millis();
    uint8_t  on = 0;

    if (!ALARM_ENABLE) { ALARM_SET_OFF(); return; }

    if (g.alarm == 1u) {
        on = (uint8_t)(((t / ALARM_SLOW_MS) & 1u) == 0u);
    } else if (g.alarm >= 2u) {
        on = (uint8_t)(((t / ALARM_FAST_MS) & 1u) == 0u);
    } else {
        on = 0;                                  /* 正常: 灭 */
    }

    if (on) ALARM_SET_ON(); else ALARM_SET_OFF();
}
#endif

/* =============================================================================
 *          补光 LED (PA5, 软件 PWM) —— 占空比由舱外光照连续调节
 * -----------------------------------------------------------------------------
 * ★ 与报警灯**完全同构**的被动两态元件:
 *     前端组件 methods:{} 是**空的、没有任何 send**;
 *     渲染只有一句 `plant_light[名字].data === 1 ? <image href="lightUp-*.png"/> : 空`
 *   ⇒ 亮不亮由**服务端按 netlist 算出的电流**决定, 极性别猜 → 开机 glamp_probe() 实测。
 *
 * ★ 控制律 (你要的"感知舱外光照强弱来**调节 LED 亮度**"): 现在是**连续调光**, 不是开关:
 *     光照有效 → 占空比 duty = 100 − light_pct (%)
 *                即环境越暗、补光越强: 全黑 → 100%(最亮), 全亮 → 0%(熄灭)。
 *     光照**无效**(light_ok = 0) → duty = 0, 并且**不猜** ——
 *          读不到就是读不到, 绝不当成"天黑了"把灯打开, 也绝不当成"天亮了"关掉。
 *
 * ★ 手动优先: 上位机/终端发 CMD_GLAMP_ON / CMD_GLAMP_OFF 可强制 100% / 0%,
 *   强制期间自动逻辑让位; 想交回自动, 再发一次 CMD_GLAMP_AUTO。
 * ========================================================================== */
static uint8_t g_glamp_mode  = 0;    /* 0 = 自动(跟随光照); 1 = 手动强制 */
static uint8_t g_glamp_force = 0;    /* 手动模式下的目标状态 */

/* 补光灯驱动: 读**运行时**极性变量 g_glamp_ahigh, 而不是编译期宏 GLAMP_ACTIVE_HIGH。
 * 直写 BSRR/BRR, 避免"读-改-写"被别的位操作打断。 */
static void glamp_drive(uint8_t on)
{
    uint8_t hi = g_glamp_ahigh;
    if (on) {
        if (hi) GLAMP_PORT->BSRR = GLAMP_PIN;      /* 高电平点亮 (默认) */
        else    GLAMP_PORT->BRR  = GLAMP_PIN;      /* 低电平点亮 */
    } else {
        if (hi) GLAMP_PORT->BRR  = GLAMP_PIN;      /* 灭 = 拉低 */
        else    GLAMP_PORT->BSRR = GLAMP_PIN;      /* 灭 = 拉高 */
    }
}

static void glamp_init(void)
{
    GPIO_InitTypeDef io;

    io.GPIO_Pin   = GLAMP_PIN;
    io.GPIO_Mode  = GPIO_Mode_Out_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GLAMP_PORT, &io);
    glamp_drive(0);                        /* 上电先灭 */
    g.glamp_on = 0;
    g_gled_duty = 0;
}

/* ★ 软件 PWM 的**每一片**: idx = 0..GLED_PWM_SLICES-1, 每片 1ms。
 *    前 n 片输出"亮"、其余输出"灭", n = round(duty / 100 * SLICES)。
 *    ⇒ 一个节拍(10ms)走完 = 一个完整 PWM 周期 = 100Hz。
 *    由主循环调用, 与风扇共用同一个节拍, 不额外占时间。 */
static void gled_pwm_slice(uint8_t idx)
{
    uint8_t n;

    if (!GLAMP_ENABLE) { glamp_drive(0); return; }
    if (idx >= GLED_PWM_SLICES) idx = (uint8_t)(GLED_PWM_SLICES - 1u);

    /* n = duty * SLICES / 100 (四舍五入, 保证 duty=100 → 全亮、duty=0 → 全灭) */
    n = (uint8_t)(((uint16_t)g_gled_duty * (uint16_t)GLED_PWM_SLICES + 50u) / 100u);
    glamp_drive((uint8_t)(idx < n));
}

/* 每个 10ms 节拍算一次**占空比**(不是直接开关):
 *     手动强制 ON  → 100%   (带 5 秒超时, 见下)
 *     手动强制 OFF → 0%
 *     光照有效     → duty = 100 − light_pct  (环境越暗, 补光越强; 连续调节)
 *     光照无效     → **慢闪** (500ms 亮 / 500ms 灭) —— 见下面那段说明
 * 真正的亮/灭由 gled_pwm_slice() 在本节拍的 10 片里逐片输出。 */
static void glamp_task(void)
{
    if (!GLAMP_ENABLE) { g_gled_duty = 0; g.glamp_on = 0; return; }

    /* ★ 手动强制加 5 秒超时: 现场按一下字符键('L'/'K')就能肉眼看灯,
     *   不用记得再按一次交回自动 —— 忘了按就一直是强制态, 会让人误判"灯坏了"。 */
    if (g_glamp_mode && (int32_t)(millis() - g_glamp_hold) >= GLAMP_FORCE_MS) {
        g_glamp_mode = 0;
        u1_print("[GLP] force timeout -> back to auto\r\n");
    }

    if (g_glamp_mode) {
        g_gled_duty = g_glamp_force ? GLED_DUTY_MAX : 0u;   /* 手动强制 */
    } else if (!g.light_ok) {
        /* ★★ 2026-10-02 现场: 光照读不到时原来一律 duty=0(**全灭**)。
         *    可"全灭"跟"灯坏了 / 10K 太大 / 元件类型不对"长得**一模一样**,
         *    现场根本分不清是哪一层, 只能干瞪眼。
         *    改成**慢闪**(500ms 亮 / 500ms 灭)当"数据缺失"的显式指示:
         *      看到慢闪 = 程序在跑、灯和驱动都是好的, 只是光照没数据(去查 PA4);
         *      连慢闪都没有 = 问题在灯这一侧(电阻/元件/极性), 与光照无关。
         *    ★ 这不是"猜光照值": 这个占空比不参与任何告警/通风判断,
         *      只是把"noData"这个事实**显示出来**, 与"温湿度读不到显示 --.-"同一口径。 */
        g_gled_duty = ((millis() / GLAMP_BLINK_MS) & 1u) ? GLED_DUTY_MAX : 0u;
    } else {
        uint16_t d = (uint16_t)(GLED_DUTY_MAX - g.light_pct);
        if (d > GLED_DUTY_MAX) d = GLED_DUTY_MAX;
        g_gled_duty = (uint8_t)d;
    }

    g.glamp_on = (uint8_t)(g_gled_duty > 0u);   /* 给网页/串口用: "亮" = 占空比 > 0 */
}

static void beep_n(uint8_t n, uint16_t ms)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        BEEP_ON();  delay_ms(ms);
        BEEP_OFF(); delay_ms(ms);
    }
}

/*==============================================================================
 *                          十一、声学层析 (ART-lite)
 *============================================================================*/

/* 单射线偏差 + 环境场 → 16×16 声速图 / 异常图 */
static void tomo_update(void)
{
    int      i, j;
    float    cx, cy, s2;
    float    base;            /* 全场基线 (归一化) */
    float    amp;             /* 异常强度 */
    float    hot, damp;       /* 温湿度修正 */
    uint8_t  cnt[7] = {0,0,0,0,0,0,0};
    float    v_ref;                          /* 参考声速 (m/s) */

    /* 参考声速 (空气, 温湿度修正)。
       注意: 一次都没读到温湿度时**不能**拿 0℃/0%RH 硬算 —— 那会把基线拉偏到 331.3,
       让 r_dev(相对偏差) 整体失真, 进而污染异常图和告警。没有实测值就退到
       20℃ 干空气标准值 343.4 m/s, 并明确它只是标称值, 不冒充测量结果。 */
    if (g.dht_ever) {
        v_ref = 331.3f + 0.606f * (g.temp_x10 / 10.0f)
                       + 0.0124f * (g.rh_x10 / 10.0f);
    } else {
        v_ref = 343.4f;                      /* 20℃ 干空气标称声速 */
    }
    if (v_ref < 300.0f) v_ref = 300.0f;

    if (g.tof_us > 0) {
        g.v_meas = 2000.0f * PATH_MM / (float)g.tof_us;   /* m/s */
        g.dist_mm = (uint16_t)((float)g.tof_us * v_ref / 2000.0f);
    }
    if (g.v_base <= 0.0f) g.v_base = (g.v_meas > 0.0f) ? g.v_meas : v_ref;
    if (g.v_base > 0.0f && g.v_meas > 0.0f) g.r_dev = g.v_meas / g.v_base - 1.0f;
    else                                    g.r_dev = 0.0f;

    /* 异常强度限幅 */
    amp = g.r_dev * 2.5f;
    if (amp >  1.0f) amp =  1.0f;
    if (amp < -1.0f) amp = -1.0f;

    hot  = (g.temp_x10 > 300) ? ((g.temp_x10 - 300) / 200.0f) : 0.0f;   /* >30℃ */
    damp = (g.rh_x10   > 700) ? ((g.rh_x10   - 700) / 250.0f) : 0.0f;   /* >70%RH */
    if (hot  > 1.0f) hot  = 1.0f;
    if (damp > 1.0f) damp = 1.0f;

    g.phase += 0.25f;
    if (g.phase > 6.28318f) g.phase -= 6.28318f;
    cx = 7.5f + 4.5f * (float)cosf(g.phase);
    cy = 7.5f + 4.5f * (float)sinf(g.phase);
    s2 = 2.0f * TOMO_SIGMA * TOMO_SIGMA;

    base = 0.30f;
    for (j = 0; j < GRID_N; j++) {
        for (i = 0; i < GRID_N; i++) {
            float dx = (float)i - cx, dy = (float)j - cy;
            float shape = (float)expf(-(dx * dx + dy * dy) / s2);
#if DUST_ENABLE
            float dust  = g.dust_ug / 2000.0f;
#else
            float dust  = 0.0f;      /* 粉尘整路作废 -> 不参与层析融合 */
#endif
            float gasv  = (g.gas_ok ? g.gas_pct_x10 : 0u) / 5000.0f;   /* 无效 → 不贡献 */
            float f = base + amp * shape * 0.75f + damp * 0.10f * (1.0f - shape)
                          + hot * 0.08f * shape + dust * 0.05f + gasv * 0.05f;
            int   vi;
            uint8_t code;

            if (f < 0.0f)  f = 0.0f;
            if (f > 1.0f)  f = 1.0f;
            vi = (int)(f * 255.0f);
            g.vmap[j * GRID_N + i] = (uint8_t)vi;

            /* 异常分类 (优先级: 虫害 > 发霉 > 发热 > 受潮 > 空洞 > 压实) */
            if (g.pest >= 1 && shape > 0.55f)      code = 5;   /* 虫害 */
            else if (damp > 0.5f && hot > 0.3f)    code = 6;   /* 发霉 */
            else if (hot  > 0.4f)                  code = 3;   /* 发热 */
            else if (damp > 0.4f)                  code = 2;   /* 受潮 */
            else if (f > 0.62f)                    code = 1;   /* 空洞 */
            else if (f < 0.12f)                    code = 4;   /* 压实 */
            else                                   code = 0;   /* 正常 */

            g.amap[j * GRID_N + i] = code;
            cnt[code]++;
        }
    }

    /* 主异常 = 除"正常"外占比最高的类型 */
    {
        uint8_t best = 0, bi = 0;
        for (i = 1; i < 7; i++) { if (cnt[i] > best) { best = cnt[i]; bi = (uint8_t)i; } }
        g.main_anom = (best > 12) ? bi : 0;
    }

    /* 置信度 = 异常占比 × 偏差幅度 */
    {
        float c = ((float)(GRID_PIX - cnt[0]) / (float)GRID_PIX);
        float d = (g.r_dev < 0) ? -g.r_dev : g.r_dev;
        float cf = c * (0.4f + 3.0f * d);
        if (cf > 1.0f) cf = 1.0f;
        g.conf_x1000 = (uint16_t)(cf * 1000.0f);
    }
}

/*==============================================================================
 *        十一-B、雨量传感器 (下雨停风机) + 北斗软串口 (2026-10-04 新增)
 *============================================================================*/

/* ---- 雨量: 只读 DO(PC14) 的**原始电平**, 极性现场翻 ------------------------- */
#if RAIN_ENABLE
static void rain_read(void)
{
    uint8_t v = RAIN_RAW();

    g_rain_raw = v;
    g_rain_on  = (uint8_t)(g_rain_alow ? (v == 0u) : (v != 0u));
}
#endif

#if BDS_ENABLE
/* ---- 北斗: 软件串口(bit-bang) 收 NMEA --------------------------------------
 *  为什么是"轮询 + 阻塞收整行"而不是中断:
 *    本项目一路采用**轮询**架构(超声波电平轮询、DHT 位循环), 没有用任何中断 ——
 *    仿真平台对 NVIC/EXTI 的支持不明, 不拿它赌。做法是把主循环本来就空等的
 *    那 10ms(切 10 片做补光软件 PWM)借来高频轮询: 线空闲时 O(1) 返回,
 *    见到起始位就一头扎进去把这一行收完(9600 下约 73ms)。
 *  什么时候会漏字节: 两次调用间隔 > 1 个位时长(9600 下 104µs)。所以调用点放在
 *    "1ms 切片内部的忙等循环"里 —— 间隔只有 1~2µs, 远小于一位, 不会漏。 */

/* 收 1 字节 8N1。**调用前必须已确认线路处于起始位(无效电平=低)**; 每位中心采一次。
 * ★ 一律用 BDS_LEV()(已按极性解释) —— 反相模块不用改硬件。 */
static uint8_t bds_recv_byte(uint8_t *out)
{
    uint32_t bit;
    uint8_t  i, v = 0u;

    bit = 1000000ul / (uint32_t)g_bds_baud;
    if (bit == 0u) bit = 1u;

    us_dly((uint16_t)(bit / 2u));            /* 走到起始位中心 */
    if (BDS_LEV() != 0u) return 0u;          /* 半位后已变高 => 毛刺, 不是起始位 */
    for (i = 0u; i < 8u; i++) {              /* 8 个数据位, 每位中心采一次 */
        us_dly((uint16_t)bit);
        v = (uint8_t)(v >> 1);
        if (BDS_LEV() != 0u) v = (uint8_t)(v | 0x80u);
    }
    us_dly((uint16_t)bit);                   /* 走到停止位, 与下一字节对齐 */
    *out = v;
    return 1u;
}

/* 取第 n 个逗号分隔字段(0 起); 无此字段返回 NULL */
static const char *bds_field(const char *line, uint8_t n, uint8_t *len)
{
    uint8_t     i = 0u;
    const char *p = line;

    while (*p != 0) {
        if (i == n) break;
        if (*p == ',') i++;
        p++;
    }
    if (i != n) { *len = 0u; return (const char *)0; }
    {
        const char *q = p;
        uint8_t     k = 0u;
        while (*q != 0 && *q != ',' && *q != '*') { q++; k++; }
        *len = k;
        return p;
    }
}

/* "ddmm.mmmm" / "dddmm.mmmm" -> 度 ×1e6 (纯整数, 不用浮点)。
 * 分组做, 避免 ip*1e6 溢出: deg = ip/100, mm6 = (ip%100)*1e6 + frac。 */
static int32_t bds_dm_x1e6(const char *p, uint8_t *ok)
{
    int32_t ip = 0, frac = 0;
    uint8_t fd = 0u;

    *ok = 0u;
    while (*p >= '0' && *p <= '9') { ip = ip * 10 + (int32_t)(*p - '0'); p++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9' && fd < 6u) {
            frac = frac * 10 + (int32_t)(*p - '0'); p++; fd++;
        }
        while (*p >= '0' && *p <= '9') p++;        /* 多余小数位丢掉 */
    }
    while (fd < 6u) { frac = frac * 10; fd++; }    /* 补足 6 位 */
    if (ip <= 0) return 0;
    {
        int32_t deg = ip / 100;                                /* 度 */
        int32_t mm6 = (ip % 100) * 1000000 + frac;             /* 分 ×1e6 */
        *ok = 1u;
        return deg * 1000000 + (mm6 + 30) / 60;                /* +30: 四舍五入到 1e-6 度 */
    }
}

/* $...*HH 异或校验 (平台若不带校验和, 这一项只会让 badsum 计数不准, 不影响解析) */
static uint8_t bds_sum_ok(const char *line)
{
    uint8_t     cs = 0u, h = 0u, i;
    const char *p = line;

    if (*p != '$') return 0u;
    p++;
    while (*p != 0 && *p != '*') { cs = (uint8_t)(cs ^ (uint8_t)(*p)); p++; }
    if (*p != '*' || p == line + 1) return 0u;
    p++;
    for (i = 0u; i < 2u; i++) {
        char c = p[i];
        h = (uint8_t)(h << 4);
        if (c >= '0' && c <= '9')      h = (uint8_t)(h | (uint8_t)(c - '0'));
        else if (c >= 'A' && c <= 'F') h = (uint8_t)(h | (uint8_t)(c - 'A' + 10));
        else if (c >= 'a' && c <= 'f') h = (uint8_t)(h | (uint8_t)(c - 'a' + 10));
        else return 0u;
    }
    return (uint8_t)(h == cs);
}

/* 存"最近一行原文"给串口 'N' 和**网页**看。
 * ★ 必须洗成安全字符: 这一行会原样进 JSON —— 一个半角双引号或反斜杠就能把 JSON
 *   字符串截断(整页取数全废), 不可打印字符则会让页面显示成乱码。 */
static void bds_keep(const char *line)
{
    uint8_t i = 0u;
    char    c;

    while (line[i] != 0 && i + 1u < (uint8_t)BDS_LINE_MAX) {
        c = line[i];
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') c = '.';
        g_bds_last[i] = c;
        i++;
    }
    g_bds_last[i] = 0;
}

/* 解析一行 NMEA: 只认 RMC / GGA 取坐标; 其它语句只计入"收到了 NMEA" */
static void bds_parse(const char *line)
{
    const char *p;
    uint8_t     type = 0u, guard;
    uint8_t     len, ok1, ok2;
    const char *fst, *flat, *fns, *flon, *few;
    int32_t     la, lo;

    /* (原文已由 bds_task 经 bds_keep 记好, 这里不再重复) */
    if (line[0] != '$') return;

    /* 头 10 个字符里找句子类型名 —— 兼容 $GNRMC/$GPRMC/$BDRMC/$GNGGA/$GPGGA/$BDGGA */
    for (p = line, guard = 0u; *p != 0 && guard < 10u; p++, guard++) {
        if (p[0] == 'R' && p[1] == 'M' && p[2] == 'C') { type = 1u; break; }
        if (p[0] == 'G' && p[1] == 'G' && p[2] == 'A') { type = 2u; break; }
    }
    if (type == 0u) { if (g_bds_state < 1u) g_bds_state = 1u; return; }

    if (type == 1u) {           /* $xxRMC: [2]状态(A=有效) [3]纬 [4]N/S [5]经 [6]E/W */
        fst  = bds_field(line, 2u, &len);
        flat = bds_field(line, 3u, &len);
        fns  = bds_field(line, 4u, &len);
        flon = bds_field(line, 5u, &len);
        few  = bds_field(line, 6u, &len);
    } else {                    /* $xxGGA: [2]纬 [3]N/S [4]经 [5]E/W [6]定位质量(>0=已定位) */
        fst  = bds_field(line, 6u, &len);
        flat = bds_field(line, 2u, &len);
        fns  = bds_field(line, 3u, &len);
        flon = bds_field(line, 4u, &len);
        few  = bds_field(line, 5u, &len);
    }

    if ((flat == (const char *)0) || (flon == (const char *)0)) {
        if (g_bds_state < 1u) g_bds_state = 1u;
        return;
    }
    ok1 = 0u; ok2 = 0u;
    la = bds_dm_x1e6(flat, &ok1);
    lo = bds_dm_x1e6(flon, &ok2);
    if (ok1 && ok2) {
        g_bds_lat = la;
        g_bds_lon = lo;
        if (fns != (const char *)0) g_bds_ns = (uint8_t)((fns[0] == 'S') ? 1u : 0u);
        if (few != (const char *)0) g_bds_ew = (uint8_t)((few[0] == 'W') ? 1u : 0u);
        if ((type == 1u) && (fst != (const char *)0) && fst[0] == 'A')      g_bds_state = 2u;
        else if ((type == 2u) && (fst != (const char *)0) &&
                 fst[0] != '0' && fst[0] != ',')                            g_bds_state = 2u;
        else if (g_bds_state < 2u)                                          g_bds_state = 1u;
    } else if (g_bds_state < 1u) {
        g_bds_state = 1u;
    }
}

/* ---- 线路体检: 在一个 1ms 片里采样线路, 看它到底动不动 ---------------------
 * ★ 只从"本片内第 3 次跳变之后"才开始记宽度: 片是硬切进来的, 头一个脉宽可能
 *   只有半截(比如 2µs), 记进去就把"最短脉宽"污染成 2µs, 反推的波特率全废。
 * ★ 本片内的间隔 ≤1000µs, 16 位计数器(65.5ms 回绕)不可能绕一圈 ⇒ 差值可信。 */
static void bds_probe(void)
{
    uint32_t t0    = (uint32_t)us_now();
    uint8_t  last  = BDS_RAW();
    uint16_t tl    = us_now();
    uint8_t  seen  = 0u;
    uint32_t hi_n  = 0u, all_n = 0u;
    uint8_t  v;
    uint16_t w;

    while ((uint16_t)(us_now() - (uint16_t)t0) < 1000u) {
        v = BDS_RAW();
        all_n++;
        if (v != 0u) hi_n++;
        if (v == last) continue;

        seen++;
        w = (uint16_t)(us_now() - tl);
        tl = us_now();
        if (seen > 2u) {                     /* 头两次跳变之后才是完整脉宽 */
            if (last == 0u) { if (g_bds_lo_min == 0u || w < g_bds_lo_min) g_bds_lo_min = w; }
            else            { if (g_bds_hi_min == 0u || w < g_bds_hi_min) g_bds_hi_min = w; }
        }
        g_bds_edges++;
        last = v;
    }
    g_bds_hi_n  += hi_n;
    g_bds_all_n += all_n;
}

/* 用体检结果**自动定**极性与波特率(只在没人手动定过、且样本够多时才下结论)。 */
static void bds_autocfg(void)
{
    static const uint32_t cand[4] = {4800u, 9600u, 19200u, 115200u};
    uint16_t bit;
    uint32_t i, b, best = 0u, bestd = 0xFFFFFFFFu, c, d;

    if (g_bds_manual || g_bds_edges < 32u) return;     /* 样本太少, 不下结论 */
    if (g_bds_lo_min == 0u || g_bds_hi_min == 0u) return;

    /* 极性: 采样里高电平占比不到一半 ⇒ 空闲电平是低 ⇒ 线路反相 */
    g_bds_inv = (uint8_t)((g_bds_hi_n * 2u) < g_bds_all_n);

    /* 一位时长 = 最短的那一侧电平宽度(正常取低, 反相取高) */
    bit = (g_bds_lo_min < g_bds_hi_min) ? g_bds_lo_min : g_bds_hi_min;
    if (bit < 4u) return;
    b = 1000000ul / (uint32_t)bit;
    g_bds_baud_m = b;
    for (i = 0u; i < 4u; i++) {
        c = cand[i];
        d = (b > c) ? (b - c) : (c - b);
        if (d < bestd) { bestd = d; best = c; }
    }
    /* 误差 <25% 才采纳; 差得远就只上报、不改档(免得自动跳到另一个更错的档) */
    if (best != 0u && (bestd * 4u) < best && best != g_bds_baud) {
        g_bds_baud = best;
        u1_print("[BDS] auto: baud=");
        u1_print_int((int32_t)best);
        u1_print(" (one-bit=");
        u1_print_int((int32_t)bit);
        u1_print("us inv=");
        u1_print_int((int32_t)g_bds_inv);
        u1_print(")\r\n");
    }
}

/* 主循环 / 空等里**高频**调用。
 * ★ 核心是"**见到起始位就一路收到底**": 一行 NMEA 在 9600 下要约 73ms, 中途只要把
 *   控制权交回主循环一次(哪怕只有 200µs), 剩下的字节就全丢 —— 旧版每片只收 1 个字节,
 *   结果永远凑不成一行, 网页就一直"未收到"。所以进来就不撒手, 直到收满这一行。
 * guard 是"线被拉死"的兜底: 最多连收 140 字节就交回主循环。 */
static void bds_task(void)
{
    static char     line[BDS_LINE_MAX];
    static uint8_t  n        = 0u;
    static uint8_t  zero_run = 0u;
    static uint32_t quiet    = 0u;       /* 线被拉死时的静默截止时刻(millis) */
    uint8_t         c;
    uint16_t        guard, w, lim;

    if (quiet != 0u) {                   /* 线一直被拉在低电平: 歇 1s 再来, 别霸着主循环 */
        if ((int32_t)(millis() - quiet) < 0) return;
        quiet = 0u;
    }

    if (BDS_LEV() != 0u) {               /* 线空闲 —— O(1) 返回; 顺手把半截行丢掉 */
        n = 0u;
        return;
    }

    lim = (uint16_t)((1000000ul / (uint32_t)g_bds_baud) * 6u);
    for (guard = 0u; guard < 140u; guard++) {
        if (guard > 0u) {
            /* 字节之间**没有空隙**: stop 位一过就是下一字节的起始位。所以每次都要
             * 等线被拉低才动手 —— 这样 bds_recv_byte 才是"从起始位起点"开始量的
             * (不等的话第 2 个字节必然采偏)。等太久 = 这一段突发结束了, 半截行丢掉。 */
            w = us_now();
            while (BDS_LEV() != 0u) {
                if ((uint16_t)(us_now() - w) > lim) { n = 0u; return; }
            }
        }
        if (!bds_recv_byte(&c)) { n = 0u; return; }   /* 采不到起始位 ⇒ 收摊 */
        g_bds_bytes++;

        if (c == 0x00u) {                    /* 连续 0x00 = 线被拉死(没接/接反/没上电) */
            if (++zero_run > 24u) { zero_run = 0u; n = 0u; quiet = millis() + 1000u; return; }
            continue;
        }
        zero_run = 0u;

        if (c == '\r' || c == '\n') {       /* 两种行尾都认(有的模块只发 CR) */
            line[n] = 0;
            if (n > 0u) {
                bds_keep(line);              /* 原文进"最近一行"(已洗成安全字符) */
                if (line[0] == '$') {        /* 只把 $ 开头的当 NMEA 记账/解析 */
                    g_bds_lines++;
                    if (!bds_sum_ok(line)) g_bds_badsum++;
                    bds_parse(line);
                }
            }
            n = 0u;
            return;                          /* 一行收完 ⇒ 交回主循环 */
        }
        if (n + 1u < (uint8_t)BDS_LINE_MAX) line[n++] = (char)c;
        else n = 0u;                         /* 行太长: 丢弃重来 */
    }
    n = 0u;                                  /* 收满 140 字节还不见行尾: 丢掉重来 */
}
#endif /* BDS_ENABLE */

/*==============================================================================
 *                          十二、报警决策
 *============================================================================*/

static void alarm_update(void)
{
    uint8_t lv = 0;
    float   d  = (g.r_dev < 0) ? -g.r_dev : g.r_dev;
    uint8_t t_hi, h_hi, t_md, h_md, dt_hi, dh_hi, dt_md, dh_md;
    uint8_t g2 = 0, g1 = 0;             /* 气体判据; GAS_ENABLE=0 时恒 0(整路作废) */
    uint8_t fl_fire = 0;                /* 火焰判据; FLAME_ENABLE=0 时恒 0(整路作废) */
    int16_t dT = 0, dH = 0;

    /* 温湿度阈值看**仓内** —— 仓内才是被监测的对象, 舱外只是通风的参照。
     * 而且只在仓内 DHT 有效时才参与判定: 读不到时 in_temp_x10 = 0, 若照样拿去
     * 比阈值, 就会把"根本没数据"当成"温度很低"从而漏报 —— 无效数据不得参与
     * 告警与决策, 这与"传感器不给兜底值"是同一条规矩。 */
    t_hi = (uint8_t)(g.dht_in_ok && g.in_temp_x10 > 350);
    h_hi = (uint8_t)(g.dht_in_ok && g.in_rh_x10   > 800);
    t_md = (uint8_t)(g.dht_in_ok && g.in_temp_x10 > 300);
    h_md = (uint8_t)(g.dht_in_ok && g.in_rh_x10   > 700);

    /* 温湿度**内外差**(仓内 − 舱外) —— 这是"温湿度比"这一路的判据:
     *   仓内比舱外高 5.0℃ / 潮 10.0%RH  → 预警
     *   高了 8.0℃      / 潮 20.0%RH      → 报警
     * 差值越大说明仓内湿热积得越厉害、通风不足(与十二节的通风控制律同源)。
     * ★ 必须**两路 DHT 都有效**才算: 缺一路差值就是编的, 这正是"拿缺失数据硬算"
     *   的典型场景 —— 所以 dht_ok && dht_in_ok 一起把关, 任一无效则 dT/dH 保持 0,
     *   不会凭空触发告警。 */
    if (g.dht_ok && g.dht_in_ok) {
        dT = (int16_t)(g.in_temp_x10 - g.temp_x10);
        dH = (int16_t)(g.in_rh_x10   - g.rh_x10);
    }
    dt_hi = (uint8_t)(dT >= 80);
    dt_md = (uint8_t)(dT >= 50);
    dh_hi = (uint8_t)(dH >= 200);
    dh_md = (uint8_t)(dH >= 100);

    /* 三组判据任一组越限即报警:
     *   ① 温湿度: 仓内绝对值(t_/h_) + 内外差(dt_/dh_)
     *   ② 气体: MQ-135 浓度(gas_pct_x10) 与硬件阈值报警(gas_do)
     *      ★ 2026-10-02: 气体这一路**已恢复**(AO 从 PA5 改到 **PA6**, GAS_ENABLE=1)。
     *        (它同时是"气压异常"的替代量 —— 平台 173 个器件里没有任何气压/风压传感器,
     *         所以只能用气体/粉尘这类"仓内空气质量"量代替, **不编造假的气压值**。
     *         粉尘于同日作废, 这条替代量现在只剩气体一路。)
     *   ③ 其它: 声速偏差(层析)、虫害等级 */
    g2 = 0; g1 = 0;
#if FLAME_ENABLE
    /* ④ 火焰传感器 (PB0): **一票否决**级 —— 检出火源直接进最高档(灯快闪 + 蜂鸣器)。
     *   它不与温湿度的"分级"混算: 明火是安全事件, 不存在"轻微着火"这一档。
     *   ★ 只有 FLAME_ENABLE 时进编译; 关掉则 fl_fire 恒 0, 判据整条摘除。 */
    fl_fire = g.flame;
#endif
#if GAS_ENABLE
    /* ★ 两条判据都先过 gas_ok: **无效数据不得参与告警决策**(与 light_ok 同规矩),
     *   否则 MQ-135 一掉线, DO 悬空/浓度算成 0 会各自触发一个假结论。 */
    g2 = (uint8_t)(g.gas_ok && g.gas_do != 0u);
    g1 = (uint8_t)(g.gas_ok && g.gas_pct_x10 > 500u);
#endif

    /* 粉尘判据: DUST_ENABLE=0 时**整路摘除**(不是"读成 0 再拿去比阈值") */
    {
        uint8_t d2 = 0, d1 = 0;
#if DUST_ENABLE
        d2 = (uint8_t)(g.dust_ug > 500u);
        d1 = (uint8_t)(g.dust_ug > 250u);
#endif
        if (fl_fire || g.pest >= 2 || g2 || d2 || d > 0.25f ||
            t_hi || h_hi || dt_hi || dh_hi) {
            lv = 2;
        } else if (g.pest == 1 || d1 || d > 0.10f ||
                   g1 || t_md || h_md || dt_md || dh_md) {
            lv = 1;
        }
    }
    g.alarm = lv;

    /* ★ 火警蜂鸣 —— 全系统唯一"会响"的自动告警。
     *   为什么单独给火焰开一条声音通道: 其他判据(温差/气体/虫害)都是**趋势类**,
     *   报警灯闪得再急也只在视线内可见; 明火需要一条**听得见**的独立通道。
     *   节奏由时间决定(1s 响 / 1s 停), 不用 delay 阻塞主循环:
     *   本函数每 1s 被调一次, 所以用两个阈值做"开-关"两态。 */
    if (fl_fire) {
        uint32_t bn = millis();
        if (g_flame_beep_on) {
            if ((uint32_t)(bn - g_flame_beep_t) >= 80u) { BEEP_OFF(); g_flame_beep_on = 0; }
        } else if ((uint32_t)(bn - g_flame_beep_t) >= 1000u) {
            BEEP_ON(); g_flame_beep_on = 1; g_flame_beep_t = bn;
        }
    } else if (g_flame_beep_on) {
        BEEP_OFF(); g_flame_beep_on = 0;      /* 火源消失立刻静音 */
    }

    /* 诱虫灯: 检出虫害即开启 (可与上位机命令联动) */
    if (g.pest >= 1) { LAMP_ON();  g.lamp_on = 1; }
    else if (!g.lamp_on) { LAMP_OFF(); }

#if LAMP_KIND == 0
    /* 旧方案(三色灯): 绿 / 黄 / 红 三档。
     * 重度告警做成 500ms 红-灭交替 —— 换灯前那套是"红常亮 + 蓝闪", 现在只有一颗
     * 灯, 就用闪烁本身表达"最高级", 比常亮更抓眼。 */
    if (lv == 0) {
        rgb_set(0, 180, 0);                       /* 绿: 正常 */
    } else if (lv == 1) {
        rgb_set(200, 160, 0);                     /* 琥珀: 轻度 */
    } else if ((millis() / 500u) & 1u) {
        rgb_set(0, 0, 0);                         /* 重度: 灭半拍 */
    } else {
        rgb_set(255, 0, 0);                       /* 重度: 亮红 */
    }
#else
    /* 报警灯: 等级已经存进 g.alarm, 亮/灭节拍由 alarm_lamp_task() 刷 ——
     * 这里顺手再刷一次, 让等级变化"当场"反映到灯上(主循环另有 10ms 节拍在刷)。 */
    alarm_lamp_task();
#endif

    /* ================= 通风决策: 内外温湿度差 → 4 路风扇 =================
     * 这是本设计的核心控制律。通风的本质是"拿舱外的空气换仓内的空气", 所以
     * **只有舱外比仓内更凉/更干时, 开风扇才有意义**; 反过来开风扇等于帮倒忙。
     * 因此用「仓内 − 舱外」的**差值**分级, 而不是只看仓内绝对值。
     *
     *   dT = 仓内温度 − 舱外温度  (×10 ℃)
     *   dH = 仓内湿度 − 舱外湿度  (×10 %RH)
     *
     *   need = 需求点数(0~4), 既决定"开几台风机", 也决定"每台多快":
     *     仓内比舱外热 ≥ 2.0℃            → +1   (有换气价值)
     *     仓内比舱外热 ≥ 5.0℃            → +1   (差距大, 加力抽)
     *     仓内比舱外潮 ≥ 5.0%            → +1   (排湿)
     *     仓内绝对超标(>28℃ 或 >65%)     → +1   (兜底: 外面也热, 但仓内已到警戒线)
     *     粉尘 > 300 µg/m³               → +1   (排尘, 与内外差无关)
     *
     *   need → 4 个通风口从 1 号起依次启用, 每台转速随 need 递增: 40/55/70/85 %。
     *   例: 舱外 40℃/60%、仓内 40℃/60% → dT=0 dH=0 need=0 → 全停;
     *       把仓内 DHT 的 Temp 属性改成 26℃(比外面凉) → 仍然 need=0, 不该开;
     *       改成 45℃(高 5℃) → need=2 → FAN1/FAN2 各 55%; 改成 50℃ → need=3 → 70%;
     *       再让湿度也高 5% → need=4 → 四台全开 85%。改属性就能现场演示控制律。
     *
     * ★ 两路 DHT 只要有一路没读到, 就**不做决策**: 风扇停并把 vent_why 置 0。
     *   绝不拿缺失数据硬算 —— 缺一路, 差值就是假的, 风扇会照着假差值瞎转,
     *   那正是"编数据"的一种形式。 */
    if (g.vent_auto) {
        uint8_t i;

        if (g.dht_ok && g.dht_in_ok) {
            int16_t dT   = (int16_t)(g.in_temp_x10 - g.temp_x10);
            int16_t dH   = (int16_t)(g.in_rh_x10   - g.rh_x10);
            uint8_t need = 0;
            uint8_t base;

            if (dT >= 20)                                need++;   /* 仓内高 2.0℃ ↑ */
            if (dT >= 50)                                need++;   /* 高 5.0℃ ↑ → 强抽 */
            if (dH >= 50)                                need++;   /* 仓内潮 5.0% ↑ → 排湿 */
            if (g.in_temp_x10 > 280 || g.in_rh_x10 > 650) need++;  /* 仓内绝对超标 */
#if DUST_ENABLE
            if (g.dust_ug > 300u)                        need++;   /* 粉尘换气 */
#endif
            if (need > 4u) need = 4u;

            base = (uint8_t)(40u + (need ? (uint8_t)((need - 1u) * 15u) : 0u));
            for (i = 0; i < 4u; i++)
                fan_set_pct(i, (uint8_t)((i < need) ? base : 0u));

            g.fan_on   = need;
            g.fan_pct  = (uint8_t)(need ? base : 0u);
            g.vent_why = (uint8_t)(need ? ((dT >= 50) ? 1u : ((dH >= 50) ? 2u : 3u)) : 0u);
        } else {
            for (i = 0; i < 4u; i++) fan_set_pct(i, 0);
            g.fan_on   = 0;
            g.fan_pct  = 0;
            g.vent_why = 0;
        }
    }

    /* ★ 雨量联锁 (2026-10-04): 检出下雨 → **强制停掉全部风机**, 压过自动/手动两种模式。
     *   道理: 通风是拿舱外空气换仓内空气, 下雨时舱外是高湿空气, 开风机等于往仓里灌潮。
     *   放在通风决策**之后**再压一次 —— 这样连手动命令(CMD_FAN_ON)也拦得住。 */
#if RAIN_ENABLE
    if (g_rain_on) {
        uint8_t i;
        for (i = 0; i < 4u; i++) fan_set_pct(i, 0);
        g.fan_on   = 0;
        g.fan_pct  = 0;
        g.vent_why = 5u;        /* 5 = 下雨联锁 (页面 WY 表跟着加了一项) */
    }
#endif
}

/*==============================================================================
 *                          十三、OLED 界面
 *
 *  屏幕只有 128x64 单色、5 行 x 12px 字 (21 字符/行)，所以本机只做"关键信息牌":
 *  标题栏 + 温湿度 + 声学 + 粉尘/气体 + 虫害/告警。
 *  16x16 声速热力图、异常图、趋势曲线、回波波形与通风控制全部交给网页端 —— 
 *  ESP8266 上传数据，PC/手机浏览器打开 http://<ip>/ 查看 (更完整界面见 web_host/)。
 *============================================================================*/

/* UI_L0~UI_L4 (5 行文本的 y 坐标) 定义在文件头「OLED 配置」一节,
   因为开机诊断屏 (十五-B 节) 要用到, 必须在那里就可见。 */

/* 行缓冲拼装: 不用 sprintf —— 省 flash 且行为完全可控 */
static char *s_put(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = '\0';
    return p;
}

/* 追加定点数: v = 真值 x 10^dec */
static char *s_dec(char *p, int32_t v, uint8_t dec)
{
    char    t[12];
    uint8_t n = 0, i;

    if (v < 0) { *p++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (n < (uint8_t)(dec + 1)) t[n++] = '0';      /* 至少 1 位整数 */
    for (i = n; i > 0; i--) {
        if (dec && i == dec) *p++ = '.';              /* 小数点位置 */
        *p++ = t[i - 1];
    }
    *p = '\0';
    return p;
}

/* ★ 原来的 anom_name() 已删 (2026-10-02 网页改成"数据驱动"后):
 *   旧版是 C 侧把页面字符串一行行拼出来, 所以要有一张中文名表;
 *   现在异常类型名在页面的 ANM 数组里, C 只上报编号(amap 直方图 + main_anom),
 *   这张表就成了没人调用的死代码 —— 平台编译会报 'defined but not used'。
 *   7 个分类必须与页面 ANM 数组同序, 由 _check7.py 核验。 */

static void ui_draw(void)
{
    char    l[26];
    char   *p;
    uint8_t i;

    /* 先整屏清零: 否则开机诊断/自检残留的长字符串会在行尾留 "DAT"/"ATA"
       这类幽灵字 (1s 后刷新的 UI 行比它们短, 覆盖不到)。 */
    oled_clear();

    /* 标题栏 (反白): 左边标题, 右边联网状态 (三态实测, 见 wifi_str) */
    oled_header(UI_L0, "GRAIN TOMO", wifi_str());

    /* 行1: **舱外**温湿度 (DHT11 #1, 脚由开机扫描认到) —— 读不到显式 --.- */
    p = l;
    p = s_put(p, "OUT ");
    if (g.dht_ok) {
        p = s_dec(p, g.temp_x10, 1); p = s_put(p, "C ");
        p = s_dec(p, g.rh_x10, 1);   p = s_put(p, "%");
    } else {
        p = s_put(p, "--.-C --.-% !");
    }
    oled_str(0, UI_L1, l, 0);

    /* 行2: **仓内**温湿度 (DHT11 #2, 固定 DHT_IN_PIN) */
    p = l;
    p = s_put(p, "IN  ");
    if (g.dht_in_ok) {
        p = s_dec(p, g.in_temp_x10, 1); p = s_put(p, "C ");
        p = s_dec(p, g.in_rh_x10, 1);   p = s_put(p, "%");
    } else {
        p = s_put(p, "--.-C --.-% !");
    }
    oled_str(0, UI_L2, l, 0);

    /* 行3: 4 路风扇转速 + **舱外光照** —— 每台 2 位, "/" 分隔, 一眼看出哪几台在转。
       (内外温湿度差越大, 亮的位越多、数字越大, 见十二节控制律)
       光照放在这行是因为它是**补光 LED 的输入**: 屏上 L 后面的数字与 PA5 的
       PWM 占空比恒为互补(duty = 100 − L), 数字和灯的亮度能对上, 才是"控制"而不是"演示"。
       ★ 光照读不到时显示 L-- (与温湿度的 --.- 同一口径), 且**占空比归 0、灯不动作**。 */
    p = l;
    p = s_put(p, "FAN ");
    for (i = 0; i < 4u; i++) {
        if (i) p = s_put(p, "/");
        if (g.fan[i] < 10u) p = s_put(p, "0");
        p = s_dec(p, g.fan[i], 0);
    }
    /* 光照/占空比**并排**给出(如 L62/38): 自动档下二者恒等于 100,
       一眼就能现场验算"灯是不是真按光照在调", 而不是屏上一个数、灯另演一套。 */
    p = s_put(p, " L");
    if (g.light_ok) p = s_dec(p, g.light_pct, 0);
    else            p = s_put(p, "--");
    p = s_put(p, "/");
    p = s_dec(p, g_gled_duty, 0);
    oled_str(0, UI_L3, l, 0);

    /* 行4: 告警等级 / 等效声速 / 害虫等级
       (TOF、粉尘、气体浓度以及 16x16 热力图都交给网页端 —— 屏幕只有 5 行,
        本机只留"一眼要看"的; 全部数据仍会从串口/网页完整给出) */
    p = l;
    p = s_put(p, "ALM:");
    /* 火源优先显示: 明火是最紧急的一条, 而且它只看 PB0 一根线 ——
     * 现场拿火源一靠近, 屏上立刻从 NORMAL 变 FIRE!, "传感器→屏"这条链一眼可证。 */
#if FLAME_ENABLE
    if (g.flame) p = s_put(p, "FIRE!");
    else
#endif
    p = s_put(p, (g.alarm == 0) ? "NORMAL" :
                  ((g.alarm == 1) ? "WARN" : "ALERT"));
    p = s_put(p, " V:");
    p = s_dec(p, (int32_t)g.v_meas, 0);
    p = s_put(p, " P:");
    p = s_dec(p, (int32_t)g.pest, 0);
    oled_str(0, UI_L4, l, 0);
}


/*==============================================================================
 *              十三-B、ESP8266 网页服务 (USART3) —— 完整信息看这里
 *
 *  两台串口分工明确:
 *    USART1 → 虚拟终端 / 上位机 (0xAA55 协议帧 + ASCII 日志, 可导出给 sim_bridge)
 *    USART3 → ESP8266 专口, 走标准 AT 指令建 TCP 服务器:
 *               AT                     握手, 判断模块在不在
 *               AT+CWMODE=1            站点模式
 *               AT+CWJAP="ssid","pwd"  接入路由器 (两组 SSID 依次尝试, 命中即停)
 *               AT+CIPMUX=1            允许多连接
 *               AT+CIPSERVER=1,80      在 80 端口开服务器
 *  手机/PC 浏览器访问 http://<模块IP>/ 时, 分两路:
 *    GET /          → 整页 HTML (**LZSS 压缩常量存 Flash, 发送时边解压边流式发**;
 *                     这一改把页面从 38733 压到 22599 字节, 见十五-B 节),
 *                     页面为左侧栏分页看板: 总览 9 张 KPI 卡 + 两套 3D 场景(粮仓模型 / 声学层析立体场)
 *                     + "全部数据"页保留全量表; 页面自带手写 3D 渲染器, 不依赖任何 CDN。
 *    GET /d?t=...   → 一小段 JSON (实测最长 556 字节), 页面每 1 秒取一次原地刷新。
 *  本机 OLED 只有 5 行摘要, 完整信息就在这一页 —— 这是"屏小信息大"的分工。
 *
 *  注: 仿真平台的 ESP8266_01S 元件**没有配网入口**, 所以 SSID 只能写死在上面的宏里;
 *  流程"只发不等死", 每条 AT 之后短收一下回复并**把回复存下来判别 OK/FAIL**,
 *  全程没有任何应答时不置联网标志 (屏上就是 WIFI:--), 绝不编造"已联网"。
 *============================================================================*/

#define ESP_ASSUME_OK   0                /* 1 = 模块无应答也硬置联网成功(仅演示用, 默认关) */
#define ESP_TXBUF_SIZE  1024             /* **只剩数据端点用**(页面已改走压缩常量流式解压发送)。
                                          * ★ 2026-10-04 起解压另占 2KB 环形窗(pg_hist)+1KB 分片
                                          *   (pg_chunk), 共 3KB —— 换掉 16134 字节 Flash。
                                          * JSON 实测最长 556 字节(normal/demo/worst 三组样本),
                                          * 留到 1024 余量 468 字节。
                                          * ★ 这一改把 RAM 从原来 5632 的占用降到 1KB —— 
                                          *   C8T6 只有 20KB SRAM, 省下的 4.6KB 全归栈和以后的功能。 */
#define ESP_CHUNK       1024             /* 每片 AT+CIPSEND 的字节数 (AT 固件单次上限 2048) */
#define ESP_REQ_CAP     96               /* 只在请求头前 96 字节里找 " /d" 判路径 —— 够且省 RAM */

/* 仿真平台里没有能渲染网页的浏览器(平台前端无此元件), 也没真 TCP/IP 栈,
 * 所以 http://<IP>/ 打不开。为了让"网页"在仿真里也**看得见**, 本固件提供两条等价通路:
 *   1) 在 uart_monitor 面板的发送框里发一个字符 'W' → 固件当成一次网页访问;
 *   2) 数据端点(含 " /d" 的访问)应答时, 把那段 **JSON 同时回吐到 USART1**,
 *      直接出现在 uart_monitor 的接收列表里 —— 各传感器的值在跳, 一眼就看到。
 * ★ 2026-10-02 起**不再回吐整页 HTML**: 页面现在有两万五千字节, 倒进串口监视器没意义,
 *   看整页请用 _page_tool.py --preview 生成本地文件。
 * 接真实 ESP8266 上现场时把本宏置 0, 就不往调试口倒 JSON 了。 */
#define ESP_HTML_ECHO   1                /* 1: 数据端点应答时把 JSON 回吐到 USART1 (仿真验证用) */

/* ★ 自动上报 —— 仿真里"真实数据上网页"的通路 (2026-10-02 新增)。
 * 每 JSON_AUTO_MS 毫秒把数据 JSON 打到 USART1 一次。
 * ★ 这段**完全不经过 ESP8266**: 平台 uart_monitor 收到这段文本 → 页面里注入的
 *   一小段 JS 把它推给本机 _live.py → 本机浏览器渲染 3D 大屏。
 *   于是"仿真里的真实数据"能上网页, 与平台是否模拟网络无关。
 *   0    = 关 (只在收到 /d 请求或按 'W' 时打一次)
 *   3000 = 每 3 秒一次 (115200 下每帧约 556 字节, 带宽占用不到 2%, 不挤上行协议帧) */
#define JSON_AUTO_MS    3000

/* g_wifi_ok 的定义在文件前部(供 OLED 界面引用) */
static uint8_t  g_http_req  = 0;         /* 1 = 收到一次够判别的请求, 该应答了 */
static uint8_t  g_req_data  = 0;         /* 本次请求是数据端点(/d) 还是整页 */
static uint32_t g_http_t    = 0;         /* 上次应答时刻 (去抖用) */
static uint32_t g_req_ms    = 0;         /* 最后一次收到请求字节的时刻(静默判据) */
static uint8_t  g_req_n     = 0;         /* 已收到的请求字节数 */
static char     g_req[ESP_REQ_CAP];      /* 请求头前若干字节, 用来认路径和 Accept-Encoding */
static char     g_hdr[192];              /* 数据端点的 HTTP 头 */
static char     g_txbuf[ESP_TXBUF_SIZE]; /* 数据端点的 JSON 组包缓冲 (页面不走这里) */

static void u3_putc(char c)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
    USART_SendData(USART3, (uint16_t)c);
}

static void u3_print(const char *s)
{
    while (*s) u3_putc(*s++);
}

static void u3_int(int32_t v)
{
    char buf[12];
    int  i = 10, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    buf[11] = '\0';
    if (v == 0) buf[i--] = '0';
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    if (neg) buf[i--] = '-';
    u3_print(&buf[i + 1]);
}

/* 收 wait_ms 毫秒; 返回期间是否收到过字节
 * 顺便把回复的前 63 字节存进 g_esp_resp, 供 esp_resp_has() 判 OK / FAIL。
 * 多一层 guard: 万一平台时基(TIM4)没在走, 也绝不会死等在这里。 */
static char    g_esp_resp[64];           /* 最近一次 AT 回复 (截断保存) */
static uint8_t g_esp_rlen = 0;

static uint8_t esp_wait(uint16_t wait_ms)
{
    uint8_t  got   = 0;
    uint32_t t0    = millis();
    uint32_t guard = 0;

    g_esp_rlen    = 0;
    g_esp_resp[0] = '\0';

    while ((millis() - t0) < wait_ms) {
        if (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET) {
            char c = (char)USART_ReceiveData(USART3);
            if (g_esp_rlen < (uint8_t)(sizeof(g_esp_resp) - 1)) {
                g_esp_resp[g_esp_rlen++] = c;
                g_esp_resp[g_esp_rlen]   = '\0';
            }
            got = 1;
        }
        delay_ms(1);
        if (++guard > (uint32_t)wait_ms * 3u + 50u) break;
    }
    return got;
}

/* 最近一次回复里是否含某子串 (AT 模块回复固定大写, 故大小写敏感) */
static uint8_t esp_resp_has(const char *pat)
{
    uint8_t i, j;

    if (!pat || !pat[0]) return 0;
    for (i = 0; i < g_esp_rlen; i++) {
        for (j = 0; pat[j]; j++)
            if (g_esp_resp[i + j] != pat[j]) break;
        if (pat[j] == '\0') return 1;
    }
    return 0;
}

/* CWJAP 专用等待: 一旦回复里出现 OK / FAIL / ERROR 就立刻返回, 不用干等满 3s。
 * (模块有应答但连不上时, 每组 SSID 都干等 3s, 开机白等好几秒, 体验很差) */
static void esp_wait_join(uint16_t wait_ms)
{
    uint32_t t0    = millis();
    uint32_t guard = 0;

    g_esp_rlen    = 0;
    g_esp_resp[0] = '\0';

    while ((millis() - t0) < wait_ms) {
        while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET) {
            char c = (char)USART_ReceiveData(USART3);
            if (g_esp_rlen < (uint8_t)(sizeof(g_esp_resp) - 1)) {
                g_esp_resp[g_esp_rlen++] = c;
                g_esp_resp[g_esp_rlen]   = '\0';
            }
        }
        if (esp_resp_has("OK") || esp_resp_has("FAIL") || esp_resp_has("ERROR")) return;
        delay_ms(1);
        if (++guard > (uint32_t)wait_ms * 3u + 50u) break;
    }
}

static void USART3_Init(uint32_t baud)
{
    GPIO_InitTypeDef  io;
    USART_InitTypeDef u;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    io.GPIO_Pin   = GPIO_Pin_10;             /* TX */
    io.GPIO_Mode  = GPIO_Mode_AF_PP;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &io);

    io.GPIO_Pin   = GPIO_Pin_11;             /* RX */
    io.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &io);

    USART_StructInit(&u);
    u.USART_BaudRate            = baud;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &u);
    USART_Cmd(USART3, ENABLE);
}

/* 候选 SSID 表 (顺序 = 先平台默认热点, 后现场路由器; 增删一组只改表, 组数自动算) */
typedef struct { const char *ssid; const char *pwd; } EspAp;
static const EspAp esp_ap[] = {
    { WIFI_SSID,  WIFI_PASS  },
    { WIFI_SSID2, WIFI_PASS2 }
};
#define ESP_AP_NUM  ((uint8_t)(sizeof(esp_ap) / sizeof(esp_ap[0])))

/* 开机建网 + 开 TCP 服务器
 * 每步都带超时(模块不在也不卡死); 状态位只按实测应答置, 无应答就是 WIFI:-- */
static void esp_start(void)
{
    uint8_t i;
    uint8_t at_ok;

    /* 1) 握手: 模块到底在不在 */
    u3_print("AT\r\n");
    at_ok    = esp_wait(400);
    g_esp_at = at_ok;

    if (!at_ok) {
        /* 模块一个字都没回: 后面的 AT 照发(接真模块时仍然需要), 但不假装已联网 */
        u3_print("AT+CWMODE=1\r\n");                     (void)esp_wait(120);
        u3_print("AT+CIPMUX=1\r\n");                     (void)esp_wait(120);
        u3_print("AT+CIPSERVER=1,"); u3_int(WIFI_HTTP_PORT);
        u3_print("\r\n");                                (void)esp_wait(120);
        u3_print("AT+CIFSR\r\n");                        (void)esp_wait(200);

        g_wifi_ok  = (uint8_t)(ESP_ASSUME_OK ? 1 : 0);
        g_wifi_idx = 0xFF;

        u1_print("[GT] esp: no AT response on USART3 (PB10/PB11) -> state ");
        u1_print(wifi_str());
        u1_print("\r\n");
        return;
    }

    u3_print("AT+CWMODE=1\r\n");                         (void)esp_wait(300);

    /* 2) 各组 SSID 依次尝试, 命中即停 */
    g_wifi_idx = 0xFF;
    for (i = 0; i < ESP_AP_NUM; i++) {
        u3_print("AT+CWJAP=\""); u3_print(esp_ap[i].ssid);
        u3_print("\",\"");       u3_print(esp_ap[i].pwd);
        u3_print("\"\r\n");
        esp_wait_join(3000);                 /* 见到 OK / FAIL 立刻走, 不干等 */
        if (esp_resp_has("OK") && !esp_resp_has("FAIL")) { g_wifi_idx = i; break; }
    }
    g_wifi_ok = (uint8_t)((g_wifi_idx != 0xFF) ? 1 : (ESP_ASSUME_OK ? 1 : 0));

    /* 3) 开 80 端口 TCP 服务器 */
    u3_print("AT+CIPMUX=1\r\n");                         (void)esp_wait(300);
    u3_print("AT+CIPSERVER=1,"); u3_int(WIFI_HTTP_PORT);
    u3_print("\r\n");                                    (void)esp_wait(500);
    u3_print("AT+CIFSR\r\n");                            (void)esp_wait(500);

    /* 4) 串口日志: 模块有应答 / 连上了哪组 SSID */
    u1_print("[GT] esp: AT ok");
    if (g_wifi_idx != 0xFF) {
        u1_print(", joined \""); u1_print(esp_ap[g_wifi_idx].ssid); u1_print("\"");
    } else {
        u1_print(", no AP joined");
    }
    u1_print(" -> "); u1_print(wifi_str()); u1_print("\r\n");
}

/* 轮询 USART3: 把请求头前 ESP_REQ_CAP 个字节**收进缓冲**, 交给 esp_serve 认路径。
 * ★ 为什么不"见到一个字节就应答": 现在要区分 GET / 和 GET /d,
 *   而 "+IPD,0,412:GET /d?t=..." 这行是分几次到的 —— 收够 24 字节再判, 路径就稳了;
 *   万一某个请求特别短(或一直没凑够), 主循环里 120ms 静默后会按"整页"处理(见主循环)。 */
static void esp_poll(void)
{
    while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET) {
        char c = (char)USART_ReceiveData(USART3);
        if (g_req_n < (uint8_t)(ESP_REQ_CAP - 1u)) g_req[g_req_n++] = c;
        g_req[g_req_n] = '\0';
        g_req_ms = millis();
    }
    /* 收够 " +IPD,0,nnn:GET /xxx" 这么多就足以判路径了; 模块没应答时不认, 免得
     * 开机时 USART3 上的杂波被当成一次访问, 白等两秒多发一遍整页。 */
    if (g_req_n >= 24u && g_esp_at) g_http_req = 1;
}

/* 一个十六进制字符 = 4 bit 量化 (256 点声速图压成 256 字符, 16 级色阶足够看) */
static char *s_hex1(char *p, uint8_t v)
{
    const char *hx = "0123456789abcdef";
    *p++ = hx[v & 0x0F];
    *p   = '\0';
    return p;
}

/*==============================================================================
 *        网页: 页面固化进 Flash + 数据走 /d 端点 (2026-10-02 第 9 次改动)
 *============================================================================*/
/* ★★ 下面 PAGE_GEN_BEGIN/END 之间那一整段(页面数组 + esp_build_data) 是
 *    `python _gen_page_c.py` 生成的, **不要手改**:
 *      改页面版式 → 改 `_shell.py`;  改字段 → 改 `_page_tool.py` 的 FIELDS;
 *      然后跑 _gen_page_c.py 重新生成。
 *
 * 为什么这么设计:
 *   1. C8T6 只有 20KB SRAM。原先整页 HTML 要先在 RAM 里拼好再发, 页面一大就只能
 *      砍内容。现在页面是**常量字符串躺在 Flash 里**, 发送时 1024 字节一片直接
 *      从 Flash 读出去 —— 一个字节的 RAM 都不占, 所以页面可以做到 2 万字节那种
 *      体量(左侧栏分页: 9 张 KPI + 两套 3D 场景 + 逐指标明细)而 MCU 毫无压力。
 *   2. 页面里的 JS 每 1 秒 fetch 一次 /d, 只拿一小段 JSON(实测最长 556 字节)原地刷新;
 *      整页只在浏览器首次打开时传一次, 主循环不再被整页回吐拖住。
 *   3. 请求路径靠 esp_poll 收到的请求字节判别: 含 " /d" 就是数据请求, 否则给页面。
 *      收不全时**默认给页面** —— 失手也只是"多传一次整页", 页面照常能开。
 *   4. 3D 场景是页面上**手写的渲染器**(透视投影 + 画家算法 + 平面光照),
 *      不引用任何 CDN —— 电脑连的是 ESP 自己的 AP, 外网多半不通。
 */
/* ==== PAGE_GEN_BEGIN ==== */
/* ★ 本区由 `python _gen_page_c.py` 生成, **不要手改**。
 *   改版式 -> `_shell.py`; 改字段 -> `_page_tool.py` 的 FIELDS; 再重跑生成器。
 *
 * ★★ 整页是**压缩存放**的 (LZSS; 格式见 `_lzss.py`, 解码器是 main.c 的 pg_decode)。
 *    为什么压: C8T6 只有 64KB Flash, 页面裸存 47396 字节(约占 59%)时,
 *      再加一个功能就顶爆 .rodata —— 2026-10-04 实测加 485 风速页后
 *      `region FLASH overflowed by 5060 bytes`。
 *    压缩后: 27875 字节 (省 19521, 41.2%), 净腾出约 18821 字节。
 *    代价: 解压要一个 2048u 字节环形窗 (RAM) + 开机一次自检 + 这几行代码。
 *
 *    ★ 压缩流**含 0x00 字节** => 长度只能用 PAGE_LZ_LEN,
 *      **禁用 sizeof()-1 / strlen / 字符串函数** (会提前截断)。
 *    ★ Content-Length 仍是**未压缩**长度 47396 (浏览器收到的是解压后的字节)。
 *    ★ 改窗口要同步三处: `_lzss.py` 的 WINDOW、下面的 PAGE_LZ_WIN、`_check15.py`。 */

#define PAGE_LZ_WIN  2048u
#define PAGE_LZ_LEN  27875u
#define PAGE_LZ_RAW  47396u
#define PAGE_LZ_XOR  0x000fu

static const char PAGE_LZ[] =
    "\x00<!doctyp\x00"
    "e html><\x00meta cha\x00rset=utf\x10-8>\n\x04\x00\x15nam\x00"
    "e='viewp\x00ort' con\x00tent='wi\x00"
    "dth=devi\x08"
    "ce-\x03\x00\r,ini\x00tial-sca le=1'\x01\x00"
    "Dti\x00tle>\xe4\xbb\x93\xe7\x00\x9c\xb8\xe6\x8e\xa2\xe5\xbe\xae\x80 \xc2\xb7 \xe7\xb2\xae\x01\x00\x13\x00\xe5\xa3\xb0\xe5\xad\xa6\xe5\xb1\x00\x82\xe6\x9e\x90\xe7\x9b\x91\xe6\x00\xb5\x8b\xe7\xb3\xbb\xe7\xbb\x9f\x04</\x04\x00"
    "6\n<sty\x01\x02\x00\x08*{box-s\x00izing:bo rder-\x01\x00\x12;m\x00"
    "argin:0;\x10padd\x02\x00\x1c"
    "0}\n\x00"
    "body{bac\x00kground:\x00#05080e;\x00"
    "color:#d\x00"
    "be7f7;mi\x00n-height\x00:100vh;\n\x02"
    "f\x01\x00\xd8:13px/\x00"
    "1.5 syst\x00"
    "em-ui,-a pple-\x04\x00\x11,'\x00Segoe UI\x00','Micro\x00soft YaH\x00"
    "ei',sans\x00-serif;o\x00verflow-\x00x:hidden\x01\x04\x00\x99:before\x02{\x05\x01"
    "E:'';po\x02s\x01\x01"
    "5on:fix`ed;in\x01\x01\x82\x01\x00\xd2z@-index\x02\x00\xdcoBi\x01\x00-r-ev\x01\x00"
    "2s@:none;\t\x00\xe0\nhrad\x02\x01tg\x02\x00\x08\x01\x00\"(\x00"
    "780px 43\x01\x02\x00\x06"
    "at 10% \x00-8%,rgba\x00(56,225,\x00"
    "255,.17)\x88,tr\x01\x00\xb3par\x01\x00"
    "8@ 60%),\x0f\x00N6\x05\x04\x00N2\x05\x00N93% 0\x01\x04\x00L139,92,(246\x01\x00L6\"\x00L56)\x02\x00"
    "F38\x05\x00L5\x01\x00\x9a"
    "10\x06"
    "6\x05\x00\x9b\x01\x00G230,1\x9c"
    "68\x01\x00O\x10\x00N\x05\x01@af\x01\x01\x0b\x05?\x01?\n\x08\x01@-imag\x00"
    "e:linearG\x08\x00\xab\x03\x00\x93\x01\x00\xde"
    "150\x01\x01"
    "20\x00,.065) 1<px\x0b\x00\x99\x01\x00\x10\x01\x00\xe7\x0e\x00<90pdeg,(\x00"
    "B\t\x01\xce\x02\x02\xe4"
    "e\x18:44\x02\x01s\x01\x00\x05}\n.\x00hd,.kpi,\x10.gri\x01\x00\x0b"
    "fot\x02{\x07\x00\xf3relatiTve\x07\x00\xee"
    "2\x01\x03\x12x\x04\x03\x8c:\x0c"
    "12\x02\x01h\x07\x03# aut\x02o\x03\x00P{displ\x00"
    "ay:flex;\x80"
    "align-i\x01\x02\xdc\x10s:ce\x02\x01*;ga\x8ep\x01\x00>\x01\x00<\x02\x00!-wr\x01\x00\x0f\x07\x02\x00\x05\x05\x00K\x01\x03yttom:b1\x03\x00\xa0h1{\x02\x03"
    "9\x04\x00\xb8"
    "1\x16"
    "7\x02\x00"
    "5\x02\x00\x0fw\x04\x03[650\xb0;let\x02\x01\x7f\x01\x00\xf1"
    "c\x02\x03\xa2"
    "F.\x01\x00"
    "7\x06\x03\x8c"
    "fff\x02\x00"
    "B \x04"
    "b{\x05\x00\x11"
    "38e1f\xaa"
    "f\x08\x00\xb4"
    "3\x03\x00"
    "c2\n\x00"
    "c3\x0e\x00"
    "c\x84"
    "00\x06\x00Oeaf3\x02\x00RP.sub\n\x00"
    "31\x08\x00r6 d84a3\x06\x04/-l\xe0"
    "eft:9\x01\x00\x1f\x05\x04T\x03\x00\x10\x01\x01\x00/ solid \x80#1d2b45\x02\x00R\xc8pg{\x02\x01\x0e:1\x01\x00\r\x02\x05\x17"
    "A\x1c\x00`text-\x03\x01Z:\xdar\x02\x00\xa6;\x02\x02#\x07\x04p.\x03\x00S\x03\x05]\x81\x07\x00\xfa"
    "9fb8d8\x0f\x00\xda"
    "A\x03\x00\x10varia\x01\x00\x08n\x04um\x01\x04\\c:tab,ul\x01\x02q\x01\x00\x10s\x01\x00Ipidll\x07\x01\xd4in\x03\x00k\x1a\x01\xdb"
    "6?\x01\x00\xaf\x06\x05"
    "0\x02\x02i\x03\x01S\x05\x01\x01\x02\x02\xc3usT:9\x02\x01\x14\n\t\x00\xe3"
    "2\r\x01v7\xef\x01\x00\x9c\x04\x00"
    "3\x08\x01/\x0f\x01\xea"
    "5\x03\x02\xc1\x02\x00\x9f\x11\x04\xf7w\x04\x02\xa5\x02\x00\x8c\x05\x01\x17"
    "6\x0f\x00\x86\x01\x04 \n\x04\xe9"
    "ctur\x02\x03"
    "3C\x02\x01"
    "1\x01\x03\xc2\x02\x06\x11h\x90"
    "adow\x01\x02(0 \x01\x00\xb1& \n\x00!\x01\x00yok\x06\x01]2e0e6a8\n\x00L\x0f\x04r09N)\x06\x00~\x04\x00"
    "5\x0f\x00\"35\x01\x04\x84.\x08wn{\x07\x02\xbd"
    "c247\x03\x0f\x00U\x02\x05[194,71'\x16\x00U\n\x00\"\x04\x00Ubd\x08\x00U5d\x04"
    "7a\x13\x00U93,12\xee"
    "2\x02\x05\x1c\x15\x00T\x06\x00!4\x02\x00S\x01\x04"
    "3\x07\x02\x14"
    "5\x02\x04:;\x02\x00\x05-\x01\x02\x0b\x01\x00\x12te!\x02\x00;umns\x01\x04"
    "Bpe\x08"
    "at(\x02\x04\x1c-fit\x86,\x01\x07\x17\x01\x04"
    "B(148\x01\x04\xa8\xe0"
    "1fr))\x04\x04\x0e\x08\x04J\x07\x03\xff\xf9\x03\x03\x9c.k\x11\x04\x8e\x06\x06\xfc\x05\x06\xfa\r\x01\xd6\x03\x02o\xf7\x06\x02\x80\x02\x00Y\x03\x02\x81 \x03\x03"
    "F\t\x05\x98\x0f\x05\x92\x01\x01\x85\x01\x07\x05W23,36,5\xf1\x01\x01\x96"
    "95)\x05\x05j\x01\x05i\x01\x06\xd8\x01\x00\x12\x18"
    "6))\x0f\x02\x9c\x01\x03\xcb"
    "a27L40\x02\x00\xb2\x1a\x07\x84"
    "ab\x01\x00/u\x08te;\x03\x04\n0;to\xdep\x01\x00\x06\x05\x02\xa2\x02\x03\x05\x05\x05\\0\x01\x02\x9b\x1a\x00\xad'\x04\x06\x03\x05\x04\xd0\x0f\x07{0)\x03\x01\xa3.w\xb7\x06\x00\x8b \x00J\x04\x02n,\x0e\x02"
    "A\x06\x00Je*\x00J-\x02\x02"
    "c,\x0e\x02"
    "7\x05\x00J \x08\x02"
    "7bl\x08ock\x04\x03\xf6styl\x82"
    "e\x01\x07;rmal;\t\x04\x17\xb6"
    "0\x02\x03\xe4\x0f\x03\xf8"
    "9\x0e\x05\x10\x03\x00Yu\x18\x06x\xf0"
    "base\x02\x00\xae\x03\x02l\x02\x02\x1f\x03\x05@\xc0"
    "decora\x03\x01\x97\x03\x07\xb5\xa9\x08\x00x25\x11\x06\x05\n\r\x00\x87-\x03\x06j\x05\x1f\x05"
    "A;\x01\x00"
    "Dbkit-\xbd\x03\x00tf\x01\x04\xad\x05\x03X\t\x07\xa6\x1c\x02\x98"
    "8\x06\x01X\x00"
    "f,#a6c6e\x0c"
    "a)\x07\x00U\t\x07\xda"
    "clip\x9a:\x02\x00"
    "e;\x12\x00\x15\x03\x01#em\x0b\x06W>.\x07\x00\xe7\x0b\x01\x80\x0c\x06k\x16\x00\xbf\t\x01{.wC\x01\x01}#\x00\xc2"
    "e4b0\x02\x00\x08"
    "aH63d5\x00\xc5.e&\x00md\x18"
    "2db\x06\x02\x8f"
    "6\x00mbig\xd6{\x07\x04L\x03\x06\xcd"
    "8\x02\x00\x05"
    "6\x17\x04\xaf\x02\x00"
    "2\x93\x01\x00\xa1\x08\x01g40\x10\x02\xc2-1\t\x00-\x1es\r\x03\x11\x1d\x02\x9c\x05\x01\xb7\x05\x01\x85"
    "7b9\xd8"
    "0ac\x06\x00\x8c\x02\x04N4\x03\x00[\x02\x05\x8e\x8f\r\x05\xa0\x02\x05g\x02\x02\xfc+\x05\xa9"
    "430\x07\x05\xa9"
    "A\x13\x00\xf1"
    "card{\r\x05\x81"
    "5U\n\x05\x81"
    "2\x02\x01"
    "04\x05\x00\x05;\x01\x00W-]\x04\x04\xe3"
    "0\x17\x05"
    "D\"\x03\x0e\x03\x04^1\x01\x04\xf2"
    "6(,43\x01\x05\x94"
    "3\x05\x05\xa6"
    "8,\xd8"
    "13,\x02\x06\xa5\x01\x05\xb8)\r\x07\xc5\x03\x01\xbb\x11\x02\x01\x8f -2\x02\x00\x0b#00\xa9\x02\x05\xb0"
    "ch%\x04"
    "99\x0f\x01\xf5"
    "9\x04\x01\x03Ph .n\n\x03Y0\r\x04>7\xe3\x01\x04>\r\x02\x02"
    "1.2\x08\x01\xc5\x04\x05\xc6\x07\x01"
    "3\xfd\x02\x01"
    "27\x03\x06\xad\x0b\x01W\x02\x01V\x0e\x07\xb8\t\x05\xf4\x08\x07\xb8\x13\t\x01R\x0f\x00%26\x02\x05\x86hin|t{\x05\x00\xcb\x03\x06\x89\x02\x01\xf7\x10\x05x\x05\x00\x9f"
    "5\x00"
    "b7597}\nc\x00"
    "anvas.cn~v\r\x02\xb2\t\x06\xb4\x05\x06\xca\x03\x00P\r\x02\x16\x0e\x07\x80#@060a11\x12\x02\x07"
    "6\x00"
    "233c;cur\x16s\x01\x00\x80\x01\x01\xfb"
    "b\n\x00}:ac\xd8tiv\x01\x07_\x08\x00\x1f"
    "b\x01\x01"
    "7\x01\x00\xd7(lgd\x18\x01\xcb"
    "c\x01\x02>er\xed\x03\x01\xc9"
    "7\x08\x01\xc9\x02\x03"
    "29\x07\x05\x06\x10\x00\xfc\x07\x04\xe5\xe7\x01\x00\\\x01\x06\xbb\x02\x00V:1\x06\x00\xe6\x02\x00"
    "9\x11\x01\xa4\x01\x08\x02tinset 0\x0f\x03\x00\x02\x02\x00\xe0\x07\x07\x14\x06\x00\x04.09)\x81\"\x07\xe8"
    "123e8c\x01\x00\x08\x10"
    "496d\x01\x00\x08"
    "26d\x08"
    "2b2\x01\x04\xdd"
    "ac83\x00"
    "a,#ec443da)\x05\x00\xa8u{\x13\x04"
    "D\x05\x00\xd3"
    "9`fb8d8 \x06\xb9\x01\x00Ms\xbc"
    "ec\x1f\x01Q\x0f\x01:\x1f\x02\xfa\x02\x04\x85\n\x0c\x02\xfb"
    "c\x04\x01\x8e\x04\x04-0 4\x08\x04@\x06\x03j4\x0f\x08\x01k\x06\x04\x80\x0e\x02"
    "0\x04\x00\xb0:fir\x00st-childz{\t\x01\xe5"
    "0\x04\x00\x1f\x16\x01\x1c\x0f\x03\r\r\x00\xb6"
    "0\xe3\x0e\x03\r\x01\x01*le{\t\x02\xfa\x05\x00\x9e\x01\x00'\xc0lapse:\x06\x00\t\"\x01iltd\x07\x06"
    "e\x02\x05*0\x17\x00\xeb\x04\x02"
    "A2\x00,35,60,.\xe0"
    "8);wh\x01\x01\x8e\x03\x00\xa7\x02\x07\xc0\x10wrap\x01\x00Or:l\xb2"
    "a\x06\x01\x05 t\x07\x05\xa0\x05\x00O0\x02\x00q\xd7\x0b\x01(\x0c\x06M\x06\x01{r\x03\x01\xc3"
    "1\x03\x06Q\x01\x00"
    "17\x08\x00S\x04\x01"
    "C\x03\x02\x0c:\x03\x00%\x0b\x01\xf4"
    "65#\x07\x01-\x01\x07\x84"
    "75e\x02\x00>.b.a\x07\x00g\x04\x07\x92\x03\x00\x16g\x06\x00\x14"
    "2e\x10"
    "e6a8\x01\x01\xa4"
    "fot\x02{\x16\x03\x8d"
    "4d6482\x8a;\x02\x03$-\x05\x03\x83"
    "1.7\x07\x00\xb0M\x02\x01\xe7"
    "6\x03\x05\x92\x01\x00"
    "E b\x0c\x00\xd6}\x00\n#off{po\x04si\x03\x01\xf6"
    "fixed:;\x03\x01\xf0"
    "5\x01\x01\xbe\x02\x00<\x02\x02~;t\x00ransformF:\x03\x00\n\x02\x07$X(-\x01\x00#)@;z-ind\x01\x04\x00"
    "9T;\n\x06\x01\xae"
    "7\x02\x06\xd8"
    "6\x0f\x04\x07"
    "9%\r\x04S2\r\x03\x17"
    "60\t\x01#c2\x0c"
    "47\x0b\x04\x04\x04\x01\xe5"
    "4,18\x1c,4\x03\x06\xc9\x15\x05\xd8\x02\x04"
    "A194\x80,71,.4)\x04\x00\xe1\x90.sm{\x07\x03\xe8"
    "d0\x01\x03[)\x0e\x00Y6,\x01\x07"
    "73\x01\x02=96N)\t\x02\xb5\x01\x00"
    "2\x0f\x06-45\x02\x00Vs~d\x0e\x01"
    "6\x03\x04\xd7\x01\x07Q\x02\x03)\x02\x04\xdc\x06\x06\x01"
    "9\x03\x02\x06\xf9\x06\x01&6;over\xc0"
    "flow-y\x04\x03S\x07\x01"
    "6\xfc"
    "13\x02\x01"
    "7\x02\x07\x89\x02\x07\x93\x07\x01<\x04\x01\xd1\x0f\x06\x07"
    "9\x1a\x05\x1f"
    "18\x03\x05 \x03\x00\xba\x02\x07\xf9"
    "1,D36\x01\x00\xda"
    "7),\x06\x00\xec"
    "0\xa1\x02\x01G.99)\x04\x00\xcb \x01\x05!\x7f\x0c\x04\xd3\x06\x07\xda\x11\x04\xdb\x0c\x07\xed\x03\x07\xd7\r\x04\x97\x03\x00\x14\nC\x1f\x04\x99\x05\x00y svg\x05\x04"
    "33\xbf\x02\x02\x13\x05\x02\xc4\x03\x00\x0c\x03\x06Z\x02\x04\x8b\x08\x00.b\r\x07g\r\t\x02Q1\x0f\x05j\t\x03veaf3+\x01\x01\xf9\x0c\x03'3\x08\x00Xi\x05\x03"
    "atyBl\x02\x04"
    "4rmal\t\x00\\9M\x10\x07\xfb;\x0b\x00\x88\x01\x00Inv\x19\x00\x9d"
    "9\xd5$\x06\x06"
    "1\x0f\x00"
    "f\n\x06\x06\x06"
    "0\x0f\x06\x06\x03\x03\xa8"
    "67\x02\x03\xdc\x02\x00|a\r\x00|\x07\x02\x9fre\xe1\x01\x03\xb0ive;\n\x03\xa0\x10\x07\xa6\x0e\x03\x9e\x83\x03\x00\xb1\x06\x06\x92"
    "adc3e\x01\x01L\x88urs\x01\x00\x0fpoi\x02\x02\x0bi\x05\x00x:h\x02\x02\xd4{\x0e\x03S\n\x03"
    "40\x0c"
    "7)\x0c\x01\x88\x05\x00;.on{\x13\x1f\x07\xe8\x0f\x00N2)\x10\x00\x14"
    "02):)\x07\x00\xae"
    "f\x01\x01\xe9\r\x02\x07\x08\x00q:b\x1a"
    "e\x01\x04\xca"
    "e\x01\x04\x11\x01\x00\xc4nt:$''\x08\x01+ab\x01\x02\xa4ut\xea"
    "e\x04\x05\x03"
    "0\x04\x05\x01"
    "8\x02\x06\xc7\x04\x02\xc5\x01\x00\x0b}\x04\x02\xaa"
    "2\t\x02\xe2\x05\x01@\x02\x00\x13\t\x00\xcc\x06\x07\xc5"
    "b\x00ox-shadojw\x01\x04\x1c"
    "0\x02\x01x \x05\x00\x1b\x01\x00\x93m\\n{\n\x07"
    "d\x04\x04,\x07\x04\x11"
    "5\x05\x04\x0c \xae"
    "2\x02\x05M\x10\x01\xd0\x06\x04U2\x01\x05\xddp\x08\x03\xc1\x8f\x04\x03"
    "0\x01\x00\x12\x02\x01^\x0e\x02\xa5pv{\r\x03\xc1\x02"
    "1\x03\x02"
    "Apv:emp\x0cty\r\x00"
    "F\x01\x00\x18 .ch\x15\r\x00"
    "47\x04\x06Pw\x07\x00-gri\xd4"
    "d;\x02\x00\x05-\x01\x04*p\x02\x06\x1f\x02\x05>\x10umns\x01\x00\xaepea\xc4t(\x01\x05\xb9"
    "fr)\x07\x04X\x05\x00P\x05\x02\x01`9\x05\x00M div{7\x0c\x01S\x02\x00\x9c\x07\x01\x0f"
    "8\x05\x05\x1f\x10\x05\xf8#1 c2a44\x0f\x05\xd5"
    "11\xbb\x02\x04\xe7\x02\x05\xd6"
    "7\x02\x05\xb4\x02\x00"
    "d\x19\x04>03\x03\xa0\x05\x04\x00Wu\r\x00Wtext-\xc0"
    "decora\x03\x01\xb3\x02\x01MY\t\x00l24\x11\x04\xa8\r\x00l-\x08\x04"
    "1v\x10"
    "aria\x01\x00\x08num\x00"
    "eric:tab,ul\x01\x03(\x01\x00\x10s\x07\x00\x8e"
    "f7H695\r\x04\xdf"
    "22\x05\x00\x9f xem{\x12\x07z\x0b\x04\xee\x0c\x04x\n\x02\x98"
    "2e\x06\x01\xa3s,\x00\xe8"
    "10\x11\x05"
    "7\t\x01\xfb"
    "1\xd3\t\x01\xfb\x01\x02\xad"
    "2{\x10\x07z4\x01\x03\xe4\x01\x01\xc2\xc4"
    "16\x01\x06\xb0"
    "42)\x0f\x01\xe4\n\x00 \x9f\x02\x04"
    "f\t\x03o\x03\x07\x91\x02\x07\x8a\x03\x03@ -\x02\x03{\xfd\x0f\x00"
    "45\x06\x02\x1a\x05\x00\x83\x01\x02\"\x05\x00\xaf\x06\x03\xa5\n\x00\x1d\x02u\x06\x00\x1d"
    "2ee6a8\x05\x0c\x00\x1ds\x06\x00\x1d"
    "7bd9b\xca"
    "6\x01\x00\x1dh\x01\x05'2{\x0c\x02\xec\x1e\x01\x1f\x03\x0c\x01\xc2\x01\x00"
    "F@media (max-\x04\x04|88)\x01\x02\xab){\x01\x06\xb9{\x07\x04\x14st\x85\x01\x01\x81"
    "c\x05\x04\x9d"
    "auto\x07\x05\xdd"
    "9\x03\x00I0}\x0e\x04k\x01\x00\x12\x0b\x06"
    "4inC\x03\x00y\x04\x04'}\n</\x03\x02\x1b>\x04\n<\x01\x00\xde id=o\x00"
    "ff class\x00=sm>\xe6\xa0\xb7\xe6\x00\x9c\xac\xe6\x95\xb0\xe6\x8d\xae\x00 \xc2\xb7 \xe6\x9c\xaa\xe6\x00\x8e\xa5\xe5\x85\xa5\xe9\x87\x87\x00\xe9\x9b\x86\xe6\x9d\xbf \xe2\x14\x80\x94\x01\x00\x03 \x01\x00$\xe5\x80\xbc\x00\xe5\x8f\xaa\xe5\x8f\x8d\xe6\x98\x00\xa0\xe7\x89\x88\xe5\xbc\x8f\xef\x10\xbc\x8c\xe4\xb8\x01\x00\x0f\xaf\xe5\xae\x90\x9e\xe6\x97\xb6\x04\x00H</\x01\x00jA\x01\x00paside\x02\x00rs&d\x05\x00~\x04\x00wlg\x01\x00\x0fsv\x00g viewBoHx='\x02\x01\xda"
    "32\x01\x00\x03'\x00><path d\x00='M16 2 \x08"
    "29 \x01\x07pv13L%\x01\x00\x0f"
    "3\x01\x00& 2\x01\x06\xcfv-\x00"
    "13z' fil\x08l='\x02\x02\xaa' st\xc0roke='\x05\x01\xed\x06\x00\x11"
    "A\x04\x01i='2'/\t\x00[9\x00 21.5 13\x00 14l5 4.P5L20\x02\x00\x11"
    "1\x15\x00U7\x1c"
    "ef\x13\x00U\x06\x00\x11\x02\x01rcap\xec='\x03\x02\xd0\x0b\x00\x17j\x01\x07"
    "b\x06\x00\x18\x01\x00\x84\x06/\x01\x00\xf9\x02\x00\xffpan><\x00"
    "b>\xe5\x85\xa8\xe5\x9b\xbd\x00\xe4\xbb\xbf\xe7\x9c\x9f\xe5\x88\x00\x9b\xe6\x96\xb0\xe5\xba\x94\xe7\x00\x94\xa8\xe5\xa4\xa7\xe8\xb5\x9b\x80</b><i>\n\x00\x1f@\xe8\xae\xbe\xe8\xae\xa1\x01\x00\x1c\xe9\x04\x81\x93\x02\x01\xb1\xe8\x81\x8c\xe6\x95\x80\x99\xe7\xbb\x84</i\x02\x00^\x07\x03\x00X\x05\x01\x81&\x01srect a\x01\x00\x14"
    "6' y\x03\x00\x06\x06\x00\xe0"
    "0\xac' \x04\x02\xc0\x04\x00\x0cr\x01\x00$5\x15\x01\"\xe7\x04\x03G\x13\x01w\x06\x00_13\x03\x00`\x02\x00\x07\x05\x00\x1f\xfa"
    "6\x08\x00`6\x04\x00_\x01\x01\x97\x06\x00"
    "a\x06\x00S\x11\x01"
    "5<\xb5\x8c\x01\x02\xa2\x01\x02\x7f\x04\x01\x19\x01\x01\x00\xe5\xad\x80\xa6\xe5\xb9\xb3\xe5\x8f\xb0\x05\x01"
    "2\x80puliedu\x02\x01!\x00\xe5\x9c\xa8\xe7\xba\xbf\xe7\x94\x80\xb5\xe8\xb7\xaf\xe4\xb8\x8e\r\x00<)\x11\x01"
    "6na\x03\x03'n\x02\x00\x0c"
    "b>\xc0\xe4\xb8\xbb\xe9\xa1\xb5\x02\x00W\x01\x02\xd1\x00 data-p=\x02o\x06\x01Xon>\xe6\x80\xbb\x88\xe8\xa7\x88\x02\x00j3D \x01\x01\xb7@\xe5\xb1\x8f</a\x03\x00<\xe7`\x8e\xaf\xe5\xa2\x83\x06\x03\x15\x0c\x00"
    "Bt\x00>\xe8\x88\xb1\xe5\xa4\x96\xe6`\xb8\xa9\xe5\xba\xa6\x04\x00"
    "2\x07\x00\x1eiBt\x01\x01\xf4\x93\xe5\x86\x85\x13\x00\x1ehI\x01\x03\xb7\xb9\xbf\x10\x00\x18li\x05\x00T\xe5 \x85\x89\xe7\x85\xa7\r\x00\x1egs\x00>\xe6\xb0\x94\xe4\xbd\x93\xe6\x04\xb5\x93\x10\x00<wd>\xe9\xa3"
    "0\x8e\xe9\x80\x9f\x01\x01#\x01\x00\t\xe5\x8e"
    "b\x8b\x06\x00\xc5\xe5\xae\x89\x01\x02\xa5\x01\x00\x17\xe6 \x8a\xa5\xe8\xad\xa6\r\x00\xc8"
    "al\x06>\x04\x00\x18\x02\x00!\x89\xa7\xe8\xa1\x8c\x08\xe5\x99\xa8\r\x00\\fl>\xe7\x00\x81\xab\xe6\xba\x90\xe7\x9b\x91\x08\xe6\xb5\x8b\r\x00\x1ern>\xe9 \x9b\xa8\xe9\x87\x8f\x13\x00\x1epe\x80>\xe8\x99\xab\xe5\xae\xb3\x02\x01\xbb \xbc\x82\xe5\xb8\xb8\x07\x00\x98\xa3\xb0"
    "A\x02\x01\xf6\xb1\x82\xe6\x9e\x90\x0e\x00\x95"
    "c#\x02\x00\x1e\x01\x00\xcd\xe7\xab\x8b\x01\x00\xf1\xe5\x9c\x82\xba\x07\x01\x92\xb3\xbb\xe7\xbb\x9f\r\x00/\x01\x01\x05)>\xe5\x8c\x97\xe6\x96\x97@\xe5\xae\x9a\xe4\xbd\x8d\r\x00\x83"
    "a\x02u\x05\x00"
    "7\xe8\x87\xaa\xe6\xa3\x80\x89\r\x00\x1e"
    "da\x02\x03\xc4\xe9\x83\xa8\x06\x01\xe7=\x02\x00\x1e/\x01\x02"
    "D\x02\x00\x07\x03\x05\x02\x01\x00\tma$in\x02\x02Tmn\x01\x00\rhe\x16"
    "a\x01\x05\xcf\x05\x02"
    "Ah\x05\x05\x1b><h\x02"
    "1\x02\x01\xf4\xe7\x9c\xb8\xe6\x8e\xa2H\xe5\xbe\xae\x01\x00\xad\xc2\xb7\x02\x00\xa9\xe7\xfc\xb2\xae\x02\x02\x0c\t\x00\xee\x04\x01)\x06\x00\xcb\x01\x00"
    "8\n\x03\xea\x0csu\t\x03 \n\x00"
    "6\xe6\x88\x90\xe5$\x83\x8f\x02\x01G\xae\xb3\x01\x01S\xe6\x99\x00\xba\xe8\x83\xbd\xe8\xaf\x86\xe5\x04\x88\xab\x02\x02\xbd"
    "ESP82<66\x06\x03/\x05\x01\x8c\x03\x00\x9a\x0f\x04Nsp\x82g\x11\x00\x16meta>\x03\x00\xcc\x00\xe8\x81\x94\xe7\xbd\x91 <\x02"
    "b\x03\x00\xf4-net>-\x86-\x03\x03\x9a\x08\x00"
    "5>\xe8\xbf\x90\x01\x02.\x99\x07\x00%up\x05\x00$\x03\x03\xb4\x88\xb7\x01\x04\xeby\x07\x00\x1cts\x0b\x00@\x06\x00"
    "F\x02\x03\xb6\x05\x00|p\xff\x01\x04"
    "2\x04\x00,\x02\x00\n\x03\x00.\x03\x03\xd4\x01\x01\x84\x04\x01o\x02\x00/'\x01\x04\x87\x01\x07"
    "a\x02\x00'pg\x07\x03\xc2'p`g on'\t\x00 \x04\x00\x18k\xe4pi\x02\x00*kp\x02\x04\x1f\x04\x00\x1a\x0f\x00$\x88gri\x0c\x06\xda"
    "car\r\x00\x11"
    "dh>\n\x00\xa7n>\x01\x04$\x01\x00\xe0"
    "0\x12"
    "1\x06\x04th2\x05\x01\xe1\xe4\xb8\x89\x00\xe7\xbb\xb4\xe6\xa8\xa1\xe5\x9e\x07\x01\x01x\x01\x00\x17\x0b\x00\xe0hint>\x00\xe6\x8b\x96\xe6\x8b\xbd\xe6\x97\x10\x8b\xe8\xbd\xac\x03\x07\xac\xbb\x9a\xe8\x00\xbd\xae\xe7\xbc\xa9\xe6\x94\xbe\x89\x03\x01"
    "6\x86\x85\x01\x02\x88\xe4\xb8\xba\n\x01\xf7@\xe5\x88\x87\xe7\x89\x87\r\x04\xe3"
    "c\xa0"
    "anvas\x03\x07\x9b"
    "1\x06\x00\xaa"
    "Dnv\x05\x05\x96"
    "620\x06\x05\x96"
    "3\\72\x01\x00"
    "4\x04\x00.\r\x06Pd\x04\x00\xd9>\xb8\xe4\xbd\x8e\x01\x04\xa5\x06\x00`\x02\x05Mi\x05\x00\x1a\x18\xe9\xab\x98\x08\x00\x1a\x05\x00\xcesty\x02l\x01\x06.margin\x00-left:au\x08to'\x03\x04\xef\xa3\x81\xe6\x8c\"\x89\n\x04\xf8\xe5\x88\x86\x01\x00\xc5\xe7\x9dp\x80\xe8\x89\xb2\r\x00\xc2\x0f\x02~'\x01{2\x0f\t\x01{\x0b\x03V\x08\x04,\x13\x01~16\xc3\x97\xfe"
    "1\x01\x03 \x03\x04Y\x02\x05G\x03\x03\xf0\x02\x00S\x01\x00\xe9\x01\x00\xad"
    "6=\x04\x00\x1a\x18\x01j2=\x01j\x04\x00`\xe6\x85\xc6\xa2\x12\x01m\x04\x00\x1d\xe5\xbf\xab$\x01p\x04\x03,\xf0"
    "conf\x08\x03,\r\x01[\t\x02\xf6\x11\x03\x01)\x03\x03"
    "Edt\x06\x03\x7fg\x0b\x00\xb4'k\x90 big\x03\x00"
    "ehw\x01\x00\xa8W\x03\x00\t\x04\x03\x96\x01\x00\xb0u\x03\x00\x0fv\x03\x00\x0fu\xfb\x01\x00\xbf\x03\x00\x0fs\x02\x03g\x08\x03\xda\x10\x03g\x14\x01\xeb\x02\x00\x87\xc3\x12\x04\xa2\x0f\x01\xfd"
    "DATA\x08\x01\xf9\x03\x00s\x7f\x04\x04\x95\x01\x01\xec\x0e\x01\xeb\x05\x01\x1d\x01\x01\xf4\r\x01\x1a\x05\x00rd\xd6t\x0b\x04\x89\x15\x01*p\x05\x00Rf\x01\x07\xeb\x01\x05\xcd\x81\x04\x02/\xe5\x8f\xa3\xe5\xbe\x84\x02\x04\xcb\x00\xef\xbc\x9a\xe6\x89\x80\xe6\x9c\x02\x89\x01\x00\x19\xe5\x80\xbc\xe5\x9d\x87\x08\xe6\x9d\xa5\x02\x06"
    "b\x9d\xbf\xe4\xb8\x10\x8a\xe5\xae\x9e\x01\x05\x80\xef\xbc\x8c@\xe4\xbc\xa0\xe6\x84\x9f\x01\x07T\xe8\x00\xaf\xbb\xe4\xb8\x8d\xe5\x88\xb0\x00\xe5\x8d\xb3\xe6\x98\xbe\xe7\xa4\x06\xba\x01\x05\x1f\x06\x05"
    "3\xe5\xb9\xb6\xe6\xa0p\x87\xe7\xba\xa2\x01\x00"
    "2\x02\x06\xe7\x02\x00*\xa1\x00\xab\xe4\xbb\xbb\xe4\xbd\x95\xe5`\x85\x9c\xe5\xba\x95\x01\x00"
    "c\x04\x00v\x9b\x03\x01\x03\x07\x01\x04#\xe6\x9c\xac\xe5\x9c\xb0\x00\xe6\x89\x8b\xe5\x86\x99\xe6\xb8\x00\xb2\xe6\x9f\x93\xef\xbc\x88\xe6 \x97\xa0\xe5\xa4\x96\x02\x04"
    "D\xbe\x9d@\xe8\xb5\x96\xef\xbc\x89\x01\x00P\xe5\x04\x8f\xaf\x01\x04y\xe5\x8a\xa8\xe8\xa7\x00\x82\xe5\xaf\x9f\xe3\x80\x82<\x1c/p\x02\x00\xeb\x02\x06\xd5\x02\x01\xac"
    "cri\x00pt>var D\xe0={};\n\x02\x01\x06\x04\x00\x14\x07\x00\x1d\x02\n\x02\x00\x1eV=[],G\x00=null,QX =0,QY\x01\x00\x05RW!\x02\x00\x17ROWS\x01\x00\rIN\x02V\x01\x00\x06"
    "CUR='o\x08v',\x01\x00\x0e"
    "docu\x00ment.get\x08"
    "Ele\x02\x00\x0b"
    "ById\x00('nv');\n\x00/* PT: \xe9\x00\xa1\xb5 -> \xe6\x98\x10\x8e\xe7\xbb\x86\x01\x06V\xe4\xb8\x8b\x07\x01\x01\x10\x01\x00\xd2\x01\x01- R()/\x00SH() \xe7\x9a\x84\x00\xe5\x87\xba\xe7\x8e\xb0\xe9\xa1p\xba\xe5\xba\x8f\x01\x00\xdf\x01\x00\xcd\x04\x00\x0c\xe6\x00\x94\xb9\xe4\xba\x86\xe8\xa6\x81\x00\xe8\xb7\x91 _che\x00"
    "ck13.py \x00\xe5\xa4\x8d\xe6\xa0\xb8 *\x02/\x03\x00\xc3PT={ot\x00:[1,2,5,@6,7],i\x01\x00\x0f"
    "3\x04,4\x01\x00\x0f"
    "8],hm\x08:[4\x01\x00\x1c"
    "6],l\x02i\x01\x00'4,15,1H6,1\x01\x00)gs\x01\x00\x11"
    "0\x00,11,12,1@3],\nfl\x01\x00\x12"
    "9\x00,20,21],\x04pe\x01\x00I0,31,\x00"
    "32],ac:[\x10"
    "23,2\x01\x00N5,2@6,27,2\x01\x00"
    "a\n\x02"
    "a\x01\x00"
    "434,35,\x00"
    "36,37,38\x00,39,40,4\x19\x01\x00"
    "Cau\x01\x00\x7f\x01\x00\x8d"
    "4,4\x81\x01\x00"
    "51,46,4\x01\x00z\x08\nwd\x01\x00\x18"
    "9,50`,51,5\x01\x00\xbb\x01\x00~r\x00n:[55,56 ,57,5\x01\x00[lo\x01\x01\x00s60,61,6\x81\x01\x00\xc2"
    "3,64,6\x01\x00\xe5\x00"
    "6,67,68,@69,70]\x01\x01\xe0N\x00V.onclic k=fun\x03\x02\xef(e\x04){\x02\x01 t=e.t\x01\x01\x04let;whil@e(t&&t\x01\x00\x12g\x00Name!=='\x00"
    "A')t=t.p\x04"
    "ar\x01\x01\xc9Node;\x00\nif(t){g\x04o(\x03\x01\xe4"
    "Attri\x00"
    "bute('da\x00ta-p'))}\x00return f\xb0"
    "alse\x01\x00\x81\x06\x00v \x01\x00"
    "5Hk){\x02\x02/k;\x02\x00\x7f"
    "a\x0cs=\x01\x00\x9d\x08\x02*sByT%\x04\x00|(\x02\x00z,i\x01\x00<or\x00(i=0;i<a\x00s.length\x00;i++){as0[i].\x03\x03\xb3\x02\x00"
    "1=(\x03\x04\x00\x11\x14\x00\x93===k?'\x00on':'')}\xd2\n\x17\x02\xa5pg\x01\x02\xc7)\t\x00V\x01\x00\x12"
    "0'+(k\x01\x00@\x02\x02\xe1?'\xca \x05\x00"
    "D;\x1a\x00"
    "Ddt\x1b\x00"
    "D\x01\x00"
    "A\x83\x02\x00G\x01\x00"
    "Dpv();\x01\x01P\x00"
    "D&&D.o){@dr()}}\x08\x01"
    "0S\x00"
    "3(id,yw, pt,sp\x04\x01\xb3s= this;\x02\x00\x05.c\x91\x17\x03oid)\x04\x00#x=\x04\x00*\x01\x02\x00!Context\xb0('2d\x02\x00~\x03\x00\x19w\x06\x00 ;\x03\x06\xe2\x04\x00"
    "4h\x06\x00\x14\x04\x06\xec\x04\x00\x15ya\x10w=yw\x04\x00\x0cpit\x04=p\x05\x00\x18sp=spA\x04\x00\x0b"
    "f=3.6\x05\x00Xq\xab\x01\x04&\x04\x00\x16l\x07\x00\nm\x01\x01\xc8\n\x05\x00[\x00onmoused\x08own\x0b\x02\x96G=s;\x95\x01\x04me\x01\x01@i\x01\x00\xcdX;\x01\x04u\x01\x06\x00\rY;e.pre\x02v\x01\x00\x0b"
    "Defaul\x06t\x01\x01/\t\x00Rwheel\x0b\x0b\x00N\x10\x00"
    "0;\x02\x00\xa7Math\x80.max(2,\x04\x00\x0b in(7,\x01\x00\x1a*(\x00"
    "e.deltaY\x00>0?.93:1\xe0.07))\x02\x00"
    "f\x01\x01\xa7\x01\x03r\x82"
    "a\x02\x00Z.hash\x05\x02\x8d\xa4>4\x04\x01\x97h=\x0c\x00\x1es\x01\x03p\x90"
    "e(1)\x01\x01 li\x01\x01s\x0e,\x02\x01r\x01\x00"
    "D\x07\x00"
    "81&&+\xc0h[0]){\x07\x01_\x03\x00\x10"
    "c\x08\x01"
    "b\x01\x00\x0f"
    "1]}\x01\x02\x10\x05\x01,m\x18ove\x0b\x00\xde\x01\x00O!G)\x89\x04\x03m;G\x02\x00"
    "C+=(\x07\x01:\x00-QX)*.0009;\nG\x03\x00P\x07\x00\xeb"
    "1.\xb4"
    "3,\x07\x01\x03-\x02\x00\x0e\x03\x00!+\x07\x00<\x90Y-QY\x04\x00<))\x18\x01\x8e"
    "3\x01\x01\x15\x05\x00\x9aup\x08\x00\x98\x03\x00\x97G)\n{\x0b\x01\x0f=\x03\x00\x9e.toF\x00ixed(2)+\xad\x01\x01\x11+\x03\x00v\t\x00\x15;\x04\x06m}\x01\x00Y\x02S\x01\x05\xcdrototy\x80pe={\nQ:\x07\x00`\x00"
    "a,b,c,d,\x10k,al\x05\x01<q.p`ush([\n\x00\x1b\x01\x03nu\x00ndefined\x00?1:al])}x,\nT\x0e\x00H\x17\x00"
    "F\x03\x06\xf6\x18\x00IL\xd3\x0c\x00I\x02\x00),w\x05\x00Il\t\x00I\x13\x00"
    "B\x84,w\x0c\x00\x13.2:w\x03\x00V\xdc"
    "F:\t\x01G\x1a\x03G\x01\x00,R\n\x00,\x08\x04\x19H,x=\x01\x03\xf5,W\x01\x00\x06w\x04,H\x01\x00\x06h,cx=\x00W/2,cy=HE\x01\x00\x07k\x08\x02\x08W,H\x01\x01\xe6"
    "4\x08"
    "1,f\x01\x00(f,i,\x10t,q;\x03\x07"
    "8cyw!\x04\x00'cos(\x03\x02\x9d),\x9as\x06\x00\x14s\x01\x00;\x05\x00\x14"
    "cp\n\x00';\x01\x01\xce\x01\x00'p\n\x00&\x02\x00\x13\t\x06\x01tfD(v\x04\x00\xaeX=v\x01\x02\xed,6Y\x01\x00\x07\x01\x07\tZ\x01\x00\x07\x02\x07"
    "E=X\"*\x01\x00{+Z*\x01\x00m,b8=-X\x02\x00\t\x01\x00\x0f\x01\x00\x15;\n\x03\x05\x06V\x01\x01XY*cp-b\x08*sp\x01\x00\nsp+bE\x01\x00\x0f]\t\x05"
    "7pj(\x05\x05-d0=f-p\x01\x00Z\x02\x05"
    "ed<\x80.5)d=.5\x03\x06{@g=f/d;\x06\x00Sc\x08x+p\x01\x00\x8f*k*g[\x01\x01(\x01\x00"
    "31\x03\x00\x0c\x01\x00Ux\x01\x02\xee"
    "e\x00"
    "arRect(0\x18,0,\x02\x01"
    "4\x04\x01$rg=\x01\x01\x00\x1dreateRa\xc0"
    "dialGr\x01\x00\x07\x01\x03\x19`(cx,H\x01\x01X\x01\x07\x95,'\x07\x00\x0b\x07\x03p\x04\x01u66\x01\x00"
    "Frg\x00.addColo rStop\x01\x00`'r@gba(26\x02\x07\xe7"
    "1\x80"
    "00,.45)\x01\x04"
    "9\x05\x0e\x00)1\x05\x00)4,7,1\x18"
    "3,0\x02\x00$\x01\x00\xaa"
    "fil\x00lStyle=r\x0cg;\x04\x00\x0f\r\x00\xb8x.st0roke\x04\x00&\x04\x00"
    "E80\x00,165,235@,.17)'\x01\x00:l%\x01\x02\x98W\x02\x06\x10=1\x06\x07\x82-5\xcd\x01\x07\x83"
    "6\x04\x07{\x03\x01@1=\x01\x01g\x01\x01\xd0\x80[i*.55,\x01\x04G\x00"
    "8,-2.8])0),g2\x11\x00\x1d\x05\x00\x1c\ng\xbe"
    "3\x06\x00\x1d\x02\x00/\x04\x00\x1c\x03\x00'\x03\x00:4\x06\x00\x1d\x03\x0f\x00\x1c\x02\x00\xbe"
    "beginP\x13\x01\x01K\x01\x05\xc9x.\x02\x05\x11To(\x8cg1\x02\x02J\x01\x00\x06"
    "1])\x05\x00\xbd\xb5\x02\x00\x16"
    "2\x03\x00\x16"
    "2\x05\x00\x16\x04\x00\xf8(\x19\x00"
    "F\xaa"
    "3\x03\x00"
    "03\r\x00"
    "F4\x03\x00\x16"
    "4\r\x00"
    "F\x8a}\x03\x02\x02o\x01\x03\x93,n=\x02\x04;\xab\x04\x05\xd5\x06\x01-0\x01\x01,n\x04\x01,q\x02\x00\x1fH[i]\x03\x02uA=\x01\x00\xdaq\x99\x02\x05\xf5,B\x04\x00\x0b\x01\x00W,C\x04\x00\x0b\x02"
    "2\x01\x00\x0b"
    "D4=q[3\x84]?\x03\x00\x11"
    "3]):\x02\x04\x87!\x04\x02ou0=B\x01\x00:-Ai\x02\x00\x91u1\x01\x00\r1\x02\x00\r\x01\x03=u\x1a"
    "2\x01\x00\r2\x02\x00\r\x01\x03"
    "Cv0=2C\x07\x00'v1\x01\x00\r\x06\x00'v2\x07\x01\x00\r\x05\x00'\x04\x00Snx=u1\x00*v2-u2*v 1,ny=\x02\x00\t0-\x84u0\x01\x00\x15,nz=\x02\x00\t\x84"
    "1-\x02\x00$0,nl\x05\x03\xd8\x00qrt(nx*n\x00x+ny*ny+\x00nz*nz)||\x06"
    "1\x04\x00U\x01\x05\xed.4+.6\x84"
    "8*\x03\x00/abs(\x02\x00/f-\x01\x00\x15\x01\x00"
    "0.8\x02\x00"
    "0\x02\x02\xd7/\x14nl\x01\x04@c\x01\x00\xf0"
    "4],\x80\nz=D4?(\x02\x00\x9b\x8a+\x02\x00\xcc+\x02\x00\xaa+D4\x02\x01\x15 *.25:\r\x00\x1b)/&3\x01\x06}\x05\x05hz,\x01\x02"
    "3A)U\x02\x00\x06"
    "B\x03\x00\x06"
    "C\x02\x01I?\x01\x00\tD\xa6"
    "4\x04\x01"
    "B\x05\x03/'+\x07\x04\xfb"
    "2\x01\x02\x96\xa4"
    "cc\x02\x03\xf3sh\x03\x06\x8d\n\x0f\x00\x1c\x16"
    "1\x07\x00\x1c\x0f\x00\x1b"
    "2\x08\x00\x1bq[5`]+')'\x01\x05\xc0\x01\x00\x91s\xa6o\x01\x01\x1d\n\x06\x1a){\x05\x04"
    "ca\x02\x01\x95\xd2"
    "b\x01\x00\x05})\n\x02&o\x06\x02:\x03\x02-\xd8t=o\x02\x02+\x15\x02\xa2t\x01\x00\x93\x02\x01\xd6k\x03\x00\x08\x02\x02\x90\n\x07\x02\xa7t\x01\x00\x93\x04\x00\x1b"
    "2\xda]\x0c\x02\xc1t\x01\x02G\x04\x00\x1a"
    "3\x05\x00"
    "5\x01\x05\x0b\xaat\x01\x01t)\t\x00#4\x05\x00#4\x06\x00=`close\x07\x00\x80\x08\x04Xt\x9b\x01\x00\xee\x05\x00\x11(\x04\x00K\x01\x06\x8f){\x12\x04W\x00"
    "4,9,18,.\xa4"
    "32\r\x04S.7\n\x03I}\t\x01\r\xd7\x02\x07;\n\x01\x0f\x02\x03<l\x06\x03<p\x06\x04q\x03\x03@\x15\x01\x01\xe8"
    "2\x07\x00\x10"
    "1\x05\x04\x1eglo\x00"
    "balAlpha}\x03\x03G;\x0c\x00\x98\x02\x03"
    "c\x0b\x00\x8a\x02\x02{\x17\x04\x10p\xd5\x03\x04Vp\r\x04Vp\x03\x04Vp\x12\x04V\x08\x01&c\x05\x00i\x0c\x00Warc\n\x00T\x01\x05\x0f"
    "6\xe1\x01\x05\x97"
    "6.3)\t\x01Q\x13\x00"
    "5\x08\x00sE\x12\x00"
    "5}\r\x00\xf9"
    "1}}\t\x07qp\x04"
    "al\x03\x07r=v<0?\x00"
    "0:v>1?1:Bv\x03\x01Qs=[[\x01\x01\xa1"
    "6\x90"
    "2,14\x01\x00"
    "a[2\x01\x06\x08\xa0"
    "50,22\x02\x00\r3\x01\x05\xb0r1\x01\x00\x11"
    "78\x02\x00\x1a\x02\x00\x17\x01\x06\x8e"
    "5\xc1\x03\x00\r36,68\x02\x00\x0c\x01\x03\xb7\x00t=v*4,i=\x08t|0\x02\x07"
    "ci>3)\x08i=3\x03\x00_f=t-:i\x01\x07\xd2s\x01\x01\xc1\x01\x07\xcb\x01\x00\x07+1\xa6]\t\x07\xc5\x01\x00\xd5+(\x02\x03\x11-\x02\x00\x0b`)*f,a\x01\x00\xe2\x02\x00\x13"
    "1\xd5\x02\x00\x13"
    "1\x05\x00\x13"
    "2\x03\x00\x13"
    "2\x02\x00\x13\x02\x04\x0c\x02"
    "f\n\x07\xe8"
    "dk(c,g}\x07\x03"
    "c[\x03\x03\xd0\x01\x07\xc1\x02\x03\xbb\x02\x00\x07\x01\x03\xa7g\x81\n\x00"
    "0gv(i,j\x04\x06\xc0\x00"
    "a=V[j*16\x06+\x01\x02_\x06\x03\xa3===un\x08"
    "def\x01\x01\xeb"
    "d?.5 :a/15\t\x00?c1(6(p\x07\x00np\x03\x01Qp>\x00"
    "15?15:p|\x02"
    "0\t\x00*sam(rxx,rz\x07\x00.\x01\x00{\x02\x00?\x03\x04Ir\no\x01\x00"
    "f(\x01\x00!/.88\x01\x01\x06\xdd+.5)*15+\x01\x02\xd4\x0f\x00#z\x0e\x00#)\t\x00"
    "fmd\x80Sil(s){\x01\x01z\xe0!D.o)\x04\x00l\x03\x01~\x01\x00\xe8\x00,N=26,R2@=.9,T=\x01\x07K1\x86"
    "5\x01\x06v\x01\x00\rSG=7\x01\x01\xbb\x00t=(D.i&&\x0b\x01\x00\x05\x02\x01[?\x02\x00\x08"
    "0]/1^0\x05\x06u\x01\x03\xda\x08\x07\xff\x02\x03\x9fN\x08\x07\xfe"
    "a\x00"
    "0=i/N*6.\x00"
    "2832,a1=\x8a(\x01\x01\xde)\x08\x00\x12\nx0\x04\x06.\x00"
    "cos(a0)*\xd0R2,z\x05\x00\x13s\x01\x05H\x05\x00\x13\xd4x1\t\x00&1\x04\x00&1\t\x00&\x03\x00\x13\x05\x04\x00\x83j\x01\x00\x83j<SG;\x02j\x06\x00\x84y0=T+(\x00"
    "B-T)*j/S\x90G,y1\x07\x00\x10(j\x02\x00\x8e\x01\x02\x00\x14m=(y0+y\t\x01\x00I.5\x02\x00\xd3Q([x00,y0\x01\x00\x8e\x01\x02\xccx1U\x03\x00\x0b"
    "1\x05\x00\x0b"
    "1\x05\x00\x0b"
    "0\x03\x00\x0b"
    "0\x12]\x03\x01%==\x02\x01\x0f?[7C\x01\x04\xd1\x01\x03+16]:\x02\x03U(\x00tt+(ym-.\x82"
    "8\x02\x01\x9a"
    "1-4)/\x01\x06\xd5"
    "0,.42\x01\x01\xa3\x02\x03\x0b%2A\x01\x00;0)s.L\x03\x00sT1\x05\x00r0,B\x03\x00\n\x04\x05%13\t\x02\x03Z5,\x02\x06X.26)@',1,.7\x04\x05Zj\xef\x0b\x00@\x10\x00\xb3\x05\x00"
    "B\x02\x03\x9c"
    "1\x05\x00"
    "B\x01\x02\"\x05\x00"
    "B\x82}\x03\x07\x8c"
    "AY=B+\x02\x00\xf9"
    "5\x13\x01\xc5"
    "b\x0b\x01\xc5"
    "b\x0e\x01\xc5\x02\x01/T(\xca[\x07\x01\xa1"
    "b\x04\x01\xb4"
    "B,\x07\x01\xa0\x04\x00\x12+\x01\x00\x95\x08\x00$1\x0f\x00$1\x05\x00$0,\x18"
    "AY,\x02\x04\xc9\x01\x01=.72\xa0),1)}\x14\x00\x98"
    "d\x0b\x00\x98\xaa"
    "d\x1e\x00\x98"
    "d\x04\x00\x98T\x08\x00td\x0f\x00\x98\xec"
    "d1\x0f\x00$\x08\x00\x98T\x02\x00\x97\x01\x04L\x03\x00\x9a\x1a"
    "1\x01\x01\xc4"
    "5\x04\x00\x9d\x02\x00\x8cSL=\x00.02,SR=.`84,NR\x01\x04\xdd\x14\x00\xb5"
    "e\x15\x0b\x00\xb5"
    "e\x0f\x03\x12"
    "a\x01\x02\x8a"
    "e0+&e\x05\x02\x8a\x08\x02\xd3NR\x08\x02\xd3r0\x08=SR\x01\x02\xceNR,r\x1e"
    "1\x02\x00\x0b\x04\x02\xc9\x02\x00\x0f\x01\x00?r0+\x82r\x03\x00?,\ncl=\x02\x00\xae\x83\x02\x04h\x08\x03"
    "Am)*rm\x08\x00\xe2/\x04\x00\x10\x02\x07\x1a\x03\x02\xf7\x07\x00(e\x01\x01\x1er0\xf8,SL\x08\x00+\x04\x00\x13\n\x01"
    "1\x02\x00\xa8\x0e\x00%\xaa"
    "1\x04\x00%\n\r\x00&1\x10\x00&1\x0b\x00K\n0\x10\x00%0\x04\x00%cl,.V9\x01\x02\xad\n\x02\t8\t\x04"
    "fn\x01\x01T8\x00*3.1416,\x04"
    "ca\t\x04-n)*SRx,sa\t\x04-\x03\x00\x13\x02\x00\xed\x01\x03"
    "1cRa\x02\x00psa\x01\x00\x88-\x04\x00\x0c-\xa7\x02\x00\r\t\x03"
    "3\x01\x03/23\x01\x03"
    "38\x01\x03"
    "2`.75,1\x01\x07T\x15\x02\x90"
    "f\x95\x0b\x01\xdb"
    "f\x12\x02\x90L\t\x01`f0\x03\x00\x9c\xa7\n\x00\xf0\x04\x00\x13\n\x01\x15"
    "f1\x10\x00%1\x04\x00%\xaa\n\x05\x00\xa6"
    "9\x05\x00\xa6"
    "4\x01\x00\xa6"
    "9\x02\x03\xd9Q\x01\x05\xbb"
    "1.6\x0b\x00\xa6"
    "4\t\x01-g\x00=i*.62-.\xa0"
    "93,dx\t\x01,g\x01\x02"
    "4\r\x02\x00\x14z\t\x01-\x06\x00\x14"
    "c=D.\x16"
    "f\x01\x07\xa2\x01\x05\xf7"
    "0\x01\x04\xefx=-A\x0c\x00$13,tz\r\x00L1\x82"
    "3\x05\x02Sdx-tx\x01\x03/\x11\x01\x00[-tz\x01\x00\xdf"
    "dx+\xa5\x06\x00\x11+\n\x00\x11"
    "72\t\x00\x12-\x07\x00\x12"
    "A\x03\x00"
    "5\n[40+\x01\x04\xc2*\x80"
    "dc,220-\x01\x01\x9e"
    "A\x02\x00\x0b"
    "110-6\x02\x00\n]#\x02\x03\x8c\x0b\x06\xf6Top\x19\x06\xf6Y=D-.\x01\x01"
    "7HH=\x03\x06\xf2"
    "E\x19\x01\x00\x07"
    "88\x08\x06\xce\x06\x00GP(a\x90,b,v\x07\x07\xa1[(\x02\x07\xee\x02-\x02\x07hE,Y+v*\xa0HH,(b\x07\x00\x13]\n\x01v<15\x03\x01w\x08\x03\xc4\x01\x00\x11\x07\x03\xc4"
    "a=\xc3\x01\x07\xec\x01\x00\x89),b2\x05\x00\x0b\x01\x02.\xa4,c\x04\x00\r+1\x04\x00\x0f"
    "d\x08\x00\x0f\x05\x05\x03\xa1P\x02\x00*,a),Po\x04\x00\x17\x01\x00#\x07\x00\x0c\x01\x00\x04"
    "c\x04\x00\x0e\x03\x00\x0c"
    "b\x03\x01\x00\x0c\x03\x06\x84"
    "a+b2+c\xe4"
    "2+\x01\x00+/4\x0e\x05R\x05\x00\xa6\x03\x00\x95\x06u\x01\x02\xc2\x01\x01"
    "cE-E/2\x9c,u\x06\x02\xc2\x06\x00\x12\x08\x00\x81"
    "0,\x03\x00\xb2<0)\x06\x00{\x04\x00\x11\x02\x00\t\x01\x00\x13[u\x80"
    "1,Y-.1,\x02\x00"
    "5\xfd\x01\x01\xccu\n\x00\x0e\x05\x05"
    "A\t\x00/\x02\x07\x0c\x01\x00\x94\x06\x00^\xfd\x01\x01\x8di\x02\x00N\x02\x00\x08\x03\x00`\x02\x00\t\x01\x00+\x05\x00\x13\xef\x01\x00\x91\x01\x00"
    "b\x02\x00\xa0\x03\x00Uu\x02\x04"
    "D\x08\x00\x0e\n\x00`\xf3\x07\x00.\r\x00"
    "ai,\x01\x00\x1a\x03\x00\xc0\x01\x07\x08\x06\x00\xc1\xdf\x05\x00\x13\x03\x00\n\x03\x00\xc3\x04\x00Q\x05\x00\xc1"
    "1\t\x00\x0e\t\x00\xc1\xab\x04\x00,\r\x00_0\x04\x00\xbf"
    "0\x05\x00\xbe"
    "0\x06\x00\xbd\xea"
    "0\x06\x00\xbc-\x0c\x00\xbd-\x14\x00\xbe\x04\x00@\x05\x00\\\xbe}\x04\x03I\x08\x00+\x05\x01Z\x17\x00\x10\x04\x00\x0f-\r\x00\x10\x00"
    "26,44,72N]\x03\x00\xaf\x08\x02\x03\t\x02\x02w=\x02\x00"
    "5+\xa0i*E/4\x05\x04\xb2w\x03\x00"
    "C>2\x06\x00"
    "d\x06\x00\x0f\x03\x00\x0e\x04\x04\x84\x01\x04\x83"
    "17A\x01\x04\x87"
    "40,.2\x01\x04\x82"
    "1\x8d\x01\x04\x84)\x05\x00>\x07\x00\x84"
    "2,w\t\x00\xa2\x0b\x03\x00\x0e\x19\x00>}\x04\x07Xs1,s\x00"
    "2,s3=nulPl,s4\x05\x00\x08"
    "5\x05\x00\x08"
    "6\x15\x05\x00\x08"
    "7\x05\x00\x08"
    "8\x04\x00\x08las t=0,p\x02\x00\x05"
    "FA\x86=\x01\x07\xbc\x03\x00\x02],WA\x01\x00\x12\x0cWP\x01\x00\xf5\x08\x03\xe2"
    "fr(t\x01\x05\x04's1){s1=\x00new S3('(s1'\x03\x04\x82-\x01\x01\x86,.D00\x01\x01\x8c;s2\x08\x00\x1f"
    "2\x1a'\x01\x00\x1b"
    "6\x02\x00 \x03\x00%003\x1c"
    "4)\x04\x00\xb7\x01\x05"
    "D\x01\x00U-pt\x12)\x02\x05"
    "10;\x01\x00\x8ats;\x01\x02\x00"
    "c(dz>0)|\xe2|\x01\x00\x07.4)\x01\x00(\x01\x00\x8b\x01\x00\x18\x83\x01\x00-\x02\x00\xb5>28){\x03\x00\xbe\x05\x05\x00/G\x02\x00\x91.yaw+\xb2=\x01\x00\x08sp\x01\x00\x80\x05\x00\x0e"
    "2\x01\x00\x0e\x02}\x02\x00;CUR===\x10'ov'\x03\x00,F()\xa0;mdSi\x01\x07\xf6"
    "1\x02\x00\xaf\x87\x05\x00\x11\x03\x05\r\x02\x00\xc0"
    "1.R(\x03\x00\x18S\x01\x00\x07\x03\x00@s3\x01\x00"
    "83\x05\x00'F\\an\x01\x00\x11\x01\x05t\x01\x00#3\x08\x00#4\x10)fl2\x01\x00\x07,ts\x05\x03\x00\xd1)\x03\x00\xb5s5)gs\r\x01\x00\x17"
    "5\x08\x00\x17\x02\x00\x16"
    "6)wd\r\x01\x00\x16"
    "6\x03\x00"
    "F\x02\x00\x11"
    "7)rnE\x01\x00\x11"
    "7\x0c\x00'8)b\x02\x00'8\x01\x07\x00\x16}\nreque\x80stAnima\x02\x01\x9c\x00"
    "Frame(fr\x82)\t\x05\xd8rr(x,\x02\x05\x94\x00w,h,r){x\x80.beginP\x01\x06{\x01\x01\x00\xc2x.moveT\"o\x01\x04\xf3r,b\x02\x00\x10"
    "arZc\x03\x00\x0fw\x01\x00"
    "3\x03\x00\x06+\x02\x00"
    "7;\xf6\n\x0b\x00\x1a\x01\x00\x14"
    "a\x06\x00\x1a\x07\x00\x19\x06\x00\x17\r\x00\x15\x83\x04\x00"
    "F\x04\x00\x15"
    "close\x04\x00{\x11\t\x00\xa3"
    "fw(\x04\x03Mf=(8D&&\x01\x07:\x01\x01\xf1\x08\x02\x85h=\xa4''\x01\x04$v;\x11\x07\xa1=\x02\x07"
    "e\x02|\x01\x02\x14h+='<d\x10iv c\x01\x02\x0fs='\x00+(v>0?'o\x00n2':'')+\x00'><b>FAN\x06 \x01\x00\x1a\x02\x04X+'</b\x80><u>'+v\x01\x00\x0c@em>%</\x01\x00\x06<@/u><s>\x06\x00@\xe8\x00\xbf\x90\xe8\xbd\xac\xe4\xb8\xad\x01\x01\x00"
    "F\xe5\xb7\xb2\xe5\x81\x9c\xe6h\xad\xa2'\x03\x00>s\x01\x00,\x01\x00q>\x00'}\ntg('f\x90w',h\n\x00\xd6pv\x05\x00\xd6\x05\x02\x00\xc0;\t\x02zal')h\x89\x0b\x00\xae"
    "ch\x01\x00tpan\x05\x00\x0f\x00n>3D \xc2\xb7 \x0c"
    "03\x01\x00"
    "f\x01\x00\x17><h2\x00>\xe5\x9b\x9b\xe8\xb7\xaf\xe9\x80\xa3\x8e\xe6\x9c\xba</\x01\x00\x11\x01\n\x00"
    "2hint>\xe5\x8f\x80\xb6\xe7\x89\x87\xe5\x9c\xa8\x01\x00\xb2@ = \xe8\xaf\xa5\x01\x00"
    "1\xe6<\xad\xa3\x02\x00\x12\x03\x00\xc7\x06\x00N\x03\x00\xb7<c\x00"
    "anvas id\x08=s3\x06\x00\x88nv w\x00idth=120\x00"
    "0 height\xb0=300\x01\x00"
    "4\x04\x00/>\t\x00\xb6\xe4"
    "fw\x02\x00=fw\x06\x00Q\x08\x00\x1a\x02\x00\x8f\x04"
    "2>\x01\x00r\xe9\x80\x9f\xe4\xbb\x00\xa5 PWM \xe5\x8d\x00\xa0\xe7\xa9\xba\xe6\xaf\x94\xe8\x00\xa1\xa8\xe7\xa4\xba \xe2\x80\x02\x94\x01\x00\x03 \xe6\x9d\xbf\xe4\xb8\x00\x8a\xe6\xb2\xa1\xe6\x9c\x89\xe6\x04\xb5\x8b\x01\x00"
    "1\xe5\x8f\x8d\xe9\xa6\x80\x88\xef\xbc\x8c\xe6\x89\x80\x01\x00=\x00\xe4\xb8\x8d\xe7\xbb\x99 R\x00PM \xe6\x95\xb0\xe5\xad\x00\x97\xef\xbc\x9b"
    "1 \xe5\x8f\x00\xb7\xe8\xb5\xb7\xe9\x80\x90\xe5\x00\x8f\xb0\xe5\x90\xaf\xe7\x94\xa8\x01\x02\x00"
    "1\xa1\xa3\xe4\xbd\x8d 4@0 / 55\x01\x00\x05"
    "7\x81\x02\x00\n85%\xe3\x80\x82\x05\x01\xac\x80;\nelse \x08\x01\x8d"
    "Bf \x01\x8dVIEW\t\x01\x89\xe7\x00\x81\xab\xe6\xba\x90\xe8\xa7\x86\x08\xe7\xaa\x97\x14\x01\x89\xe6\xa3\x80\xe5\x04\x87\xba\x01\x00(\xe7\x84\xb0\xe5\x8d\x80\xb3\xe7\x82\xb9\xe4\xba\xae\x02\x00\xa0\x04\x97\xa0\x04\x00=\xe5\x88\x99\xe7\x86P\x84\xe7\x81\xad\x17\x01\x92"
    "4\x1c\x01\x92"
    "4\x13\t\x01\x92\x10\x00\xd4gs.\x00\xd4\xe6\xb0\x94\x00\xe5\xae\xa4\xe6\xb5\x93\xe5\xba"
    "B\xa6\x1b\x00\xda\x9f\xb1\xe9\x95\x01\x01\xb1\x8e\x01\x01\x00"
    "1\xe6\xb3\xa1\xe9\x87\x8f\xe9`\x9a\x8f\xe5\xae\x9e\x01\x01\xba\x04\x00=\xe5 \x8f\x98\xe5\x8c\x96\x02\x02\xbd\xe8\x99\x00\x9a\xe7\xba\xbf=\xe6\x8a\xa5\x00\xe8\xad\xa6\xe9\x98\x88\xe5\x80P\xbc 50\x17\x00\xf1"
    "5:\x00\xf1w\xc6"
    "d.\x00\xf1\x01\x03H\xe9\x81\x93\x02\x00\x06\x01\x02\xa2\x00\xb8\x8e\xe5\x8a\xa8\xe5\x8e\x8b"
    "1\x17\x03W\xe8\xbd\xae\x05\x02\xcd\x01\x00\xfa\xb5\x81s\x01\x00.\x02\x02\xa8\xba\xa6\x07\x00\xfd\x04\x00I\x08\x00\xfd\xe5 \x8f\xb3\xe4\xbe\xa7\x04\x00V\xe7\x94"
    "b\xb1\x04\x00\x1f\xe7\xae\x97\x01\x02\x17\x17\x01\x02"
    "6%\x1c\x01\x02"
    "6\x1b\x01\x02rn/\x01\x02\x99\x8dH\xe9\x9b\xa8\x1b\x01\xed\x9c\x89\x01\x00\"=\x18\xe4\xb8\x8b\x01\x00\x07\x01\x00\xcc\xe7\x94\xbb\x07\x01\x03O\x01\x02\xc5\x02\x00\x16\xe6\x99\xb4\xe6\x9c\x06\x97\x05\x00\x16\x01\x00\xf2"
    "BK_Ra\x00indrop D\x00O\xe2\x86\x92PC14\x8d\x17\x00\xec"
    "7(\x01\xee\x0f\x04W\xe4\xb8\x80\x05\x00\x93\x00\xb0\xb1\xe3\x80\x8c\xe5\xbc\xba\x08\xe5\x88\xb6\x02\x05\x99\x8e\x89\xe5\x85\x10\xa8\xe9\x83\xa8\x04\x05$\xe3\x80\x8d\x03\x05\x04"
    "c\x04\x00.\xe6\x97\xb6\xe8\x88\xb1\x00\xe5\xa4\x96\xe6\x98\xaf\xe9\xab\x10\x98\xe6\xb9\xbf\x02\x04\x8f\xb0\x94,\x10 \xe5\xbc\x80\x04\x00"
    "3\xe7\xad\x89 \xe4\xba\x8e\xe5\xbe\x01\x04w\x93\xe9\x00\x87\x8c\xe7\x81\x8c\xe6\xbd\xae\x11\x01\x04<\xe8\xbf\x99\x01\x00"
    "5\xe7\xa1\xac \xe4\xbb\xb6\xe5\xae\x02\x00"
    "f\xe8\x81\x00\x94\xe9\x94\x81, \xe4\xbc\x10\x98\xe5\x85\x88\x01\x00"
    "5\xe6\x89\x8b"
    "A\x02\x01\xee\x91\xbd\xe4\xbb\xa4\x19\x04nl\x04oc.\x01\xa8\xe5\x8c\x97\xe6\x96"
    "0\x97\xe5\xae\x9a\x01\x04\xdb\x1a\x01\xae\xe5\x8d"
    "0\xab\xe6\x98\x9f\x04\x00(\x08\x01\x97\xe5\x9d\x10\x90\xe6\xa0\x87\x02\x02\xa5\x97\xb6\xe6 \x9d\xa5\xe8\x87\xaa\x02\x01\xaa"
    "BD\x00S \xe8\xbd\xaf\xe4\xb8\xb2\xa8\xe5\x8f\xa3\x17\x01\xa5"
    "8\x1c\x01\xa5"
    "8\x1a\x01\xa5\x03\x01\x05\x91\x01\x01\x16\xe7\x81\xbe\xe5\xae\xb3\xc0\xe7\x8e\xb0\xe5\x9c\xba\x04\x00\x9a\x01\x03"
    "Ax\xe7\xb2\xae\x01\x01`\x04\x00\x93\x04\x00\x15\x01\x01"
    "1\xe6\x00\xa8\xa1\xe5\x9d\x97 TX\x90 \xe8\xb5\xb0\x02\x00\x9b\xbb\xb6\x04\x00\x9e\x00(RX=PB2,@ 8N1);\x01\x03Y\xaa\x04\xe6\x94\x01\x00\x1b\x8d\xe5\x8f\x91,\x02 \x05\x00"
    "6RX \xe9\x82\xa3\x08\xe6\xa0\xb9\x02\x06,\x94\xa8\xe6\x8e&\xa5\n\x01\x83\x02\x07\x9dpv\x02\x07\x9d;\n\x08s3=\n\x07\x86?new\x00 S3('s3'\x00,.3,-.16\x00,0):null\t\x01\x00-4=\n\x06&?doc\x00ument.ge\x10tEle\x02\x00\x0b"
    "ByI\x92"
    "d\x01\x00>4'\x07\x00"
    "45=\n\x05\x86"
    "e\x19\x00"
    "45\x08\x00"
    "46=\n\x04\xc9\x19\x00"
    "46Y\x08\x00"
    "47=\n\x03\xfb\x19\x00"
    "47\x08\x00"
    "486=\x0b\x02\x88\x19\x00"
    "58\x07\x00"
    "5\x01\x02\xbcs3\x00)fw()}\nf\x00unction \x00mdFan(s,\x10"
    "dz){\x01\x00!!D|\x00|!D.f)re\x00turn;var\x00 i,j,R=.\x00"
    "72,SP=1.\x00"
    "78,NT=16\x01\x01\x00\x86.m=1;\nf\x00or(i=0;i\x00<4;i++){\x01\x02\x00"
    "5cx=(i-1\x00.5)*SP,r\x08un=\x01\x00V[i]|\x00"
    "0,mm=Mat\x00h.max(0,\x81\x04\x00\x0bin(1,(\x01\x00'\x00-40)/45)@),\nbc=\x01\x00\x11?\x00[56+199*\x00mm,225-3\x02"
    "1\x03\x00\n55-184\x01\x01\x00\x0b]:[50,6\x00"
    "8,94],a;H\nFA\x01\x00"
    "f=(\x03\x00\x07+\x08"
    "dz*\x02\x00U/100\x08)*7\x01\x00\x8d%6.2(832\x04\x00\xb5j\x01\x00\xb5j<\x10NT;j\x06\x00\xb6"
    "a0=\x00j*.3927,\x00"
    "a1=(j+1)\x05\x05\x00\x0fq\x01\x00\x0e%2)?.\x10"
    "82:1\x02\x00\xfbQ([\x08"
    "cx+\x03\x00\xbe"
    "cos(\x82"
    "a\x01\x00"
    "aR*.84\x04\x00\xd1\x86s\x01\x00\xd1\x08\x00\x13.1],\x0c\x00.\x9a"
    "1\x10\x00.1\x1c\x00.\r\x00*,0\x0e\x00%\n0\x0c\x00%0\x06\x00%22+1\x00"
    "4*q,34+1B8\x01\x00\x08"
    "52+2\x01\x00\x10],,1\x02\x02\x08\x07\x01\x07"
    "7\x04\x01\x06"
    "a=C\x04\x01:\x01\x01\x07"
    "8976\x12\x00\xea-h.34\x03\x00\xac"
    "2\t\x00o\t\x00\x15.T13\x0e\x00\x9c+\x12\x00"
    "3+\x1d\x00"
    "38\x9d\x04\x00\x1e"
    "9\x0b\x00"
    "3\x07\x00\x15\x13\x00"
    "316\x11\x00"
    "3\x03\x07\x00\x15\x03\x00"
    "3bc,.95\xa7\x01\x00\xfb\x05\x00\xd9\x01\x00\x16"
    "45\x01\x00\x05"
    "6\x04\x00N]\x03\x00\x0e"
    "0\x07\x00\x10\x02\x04H\t\x00\x1f-\x0b\x00\x1f"
    "2\xf0"
    "38,2\x01\x00\x10\x01\x02\x8d\x02\x01N\x02\x01,6L\x03\x00S\x02\x00!8\x04\x00@\x05\x00\x0b'rPgba(\x01\x00),\x06\x00\x04.\xc0"
    "92)',1\x02\x01\x86\t\x03\x8f\x80"
    "fl2(c,t\x04\x02\x8e\x08x=c\x02\x03\xd0"
    "Cont@ext('2\x01\x04S,fW\x01\x00\x15\x03\x05\xc3,H\x01\x00\n\x04\x05\xc2,\x00i;\nx.cle@arRect\x01\x03V00,W,H\x01\x00\x92\x02\x00Gon =(D&&\x01\x03\x80l)\x02?\x02\x00\x06[1]:0,\x01\x01\x03\xa2W*.5,by(=H*\x02\x03\xd6h\x02\x00\x08"
    "6,\x02w\x02\x00\x17"
    "08,ln=@0,\nrg=\x01\x00Zr\x00"
    "eateRadi\xb0"
    "alGr\x01\x00\x07\x01\x04_(\x01\x00\xdd\x00"
    "by-h*.4,<4,\t\x00\r\x01\x00"
    "E\x01\x01`\x03\x04oon\x0c){\x01\x00G\x07\x01\x8dt*2.\xa0"
    "3)*w*\x01\x01G;\x01\x00[\x00.addColo`rStop\x01\x00\xb6\x08\x01&1\x00"
    "46,36,.4\x10)');\x0e\x00).42q\t\x00+88,\x01\x05\xeb\x01\x01\xdc\x12\x00+1U\t\x00)6\x02\x01\x17"
    "0\x01\x00&}\x03\x07\xcc{\x01\x16\x00\x80"
    "76,124,\x04"
    "18\x1f\x00V40,70\x08,11\x06\x00Wx.fi\x00llStyle=8rg;\x04\x00\x0f\r\x01\x92\x08\x01\xebp( sc,dy\x04\x01\xeckk@=on?1+\t\x01,6\xa2.\x02\x04~055\n\x00\x15"
    "1\x01\x05\x81Y\x02\x00\x16"
    "3+\x0b\x01V\x03\x00\x14"
    "2\x04\x04\x9b"
    "c\x10*=kk\x01\x00}begHinP\x01\x00&()\x01\x00\x0em\xa0oveTo\x04\x01\xb6)\x02\x02/\x00"
    "bezierCubr\x05\x00\x18-w*\x01\x00\x90\x05\x01\xc6"
    "6!\x01\x00\x0c+dy,\x03\x00\x17.5\xf9\x03\x00\x0fln\x05\x00\x1e\x01\x05x\x06\x00\x1e\x07\x00\x15\x03\x00\x11=\x13\x00X+\x1c\x00"
    "A\x14\x00v\x03\x00\xa5\x02\x02\xd3os*e\x04\x00\xc3}\x06\x02Qf\x02\x01\x80"
    "0)\x89\x03\x06\x9bg1\x07\x02\x9eLin\x01\x02\xff%\x07\x02\x9e"
    "0\x01\x00"
    "B,0\x03\x00Y),\x12g\x01\x05\xf1g1\x0f\x01\xee#ff\xa8"
    "3d0\x01\x07\x8a;\x0e\x00\x1d.\x01\x03\xd6\xa1\x02\x00\x1f"
    "8a1e\x11\x00\x1f"
    "1\x03\x00\x1d(d23\x01\x03\x91;\x0b\x01\xf6g1\x8d\x05\x01\xf6(\x02\x01\xeb\x01\x00I66,\x02\x00\xdbr0\x01\x03\x12g2!\x00\xad\x01\x03\x82\x01\x00:g\xa2"
    "2\x12\x00\xad"
    "b52\x03\x00\x8e"
    "2\x12\x00\x8e\x80"
    "f3b0');\x0b\x00\x8d\x92"
    "2\r\x00\x8d"
    "32\x03\x00\x8d"
    "11\x0c\x00(s\r\x04\x9d\x01\x04\xd6.9\x01\x03y\n\x00=\x07\x07\xe3"
    "9\t\x08\x07\xe3p=\x01\x02{.33+\xc2i\x02\x00S7)%1\x04\x01\xff\n\x02v\xb0"
    "arc(\x06\x05\xa4\x02\x02\xaci\x02\x04\x02j+\x01\x00"
    "78\x02\x04\x07"
    "2\x01\x04o\x05\x01\xfb-\x0cp*\x01\x04"
    "9\x02\x03\xfa.5+.\x80"
    "9*(1-p)\x01\x01\x14\x8e"
    "6\x01\x04"
    "1\x0c\x01"
    "c\t\x04%96,\x01\x00\x03\x18'+(\x01\x04\xbf\x04\x00"
    "2).t\x00oFixed(3\xe8)+'\t\x00\xc4}\x05\x03\xf5\x06\x02>\x0b\x00R\x80#0f1826\n\x00\xf2\x00x.stroke\xa9\n\x00t11\x01\x04k5\x01\x03\xf1"
    "9\x01\x06\xb2T42\x03\x00^l\x01\x01\xcaW\x02\x05\x8f=W\x02\x01m\x04\x00"
    "4\x0c\x01"
    "24\x08\x01"
    "2q\x03\x01"
    "21\n2\x02\x01"
    "22$\x01"
    "1q*3.(4+i\x04\x01.2\x06\x03@-q\x81\x03\x01.,2+q*8\x19\x01&\xd3\n\x00\xb2\x02\x01'13\x02\x01'q\x1b\x01'\x01\x00\x0b\x01\x01\x06q='700 2\x00"
    "2px syst\xc0"
    "em-ui'\x0b\x01"
    "6\x01\x04\xaf!\t\x01\x8b"
    "72,6\x01\x01\x16"
    "96h)':\x05\x00|2\x01\x05H\x05\x01\xa6.\x0e"
    "7\x02\x01.\x01\x00[\x02\x06\xc9"
    "Alig\x90n='c\x01\x02\xfa"
    "er\x06\x00T\x06T\x02\x06\xe2\x02\x00S\xe6\xa3\x80\xe6\xb5\x00\x8b\xe5\x88\xb0\xe7\x81\xab\xe6\x00\xba\x90 \xc2\xb7 \xe5\xb7\x80\xb2\xe6\x8a\xa5\xe8\xad\xa6\x01\x00[\x08\xe6\x97\xa0\x0b\x00\x19\xe7\x86\x84\xe7x\x81\xad'\x02\x04"
    "3\x02\x06y\x05\x01\x07\x03\x00\xc1"
    "6\t\x01\x00\xc1"
    "13\x18\x00\xc1'#5b\xe0"
    "7597'\x06\x01:\x07\x00\x84\x01\x00"
    "b\x00\xe7\x84\xb0\xe9\x80\x9a\xe9\x81\x00\x93 PB0 \xe8\xa7\x10\xa6\xe5\x8f\x91\x02\x00s\xe8\x9c\x82\x00\xe9\xb8\xa3\xe5\x99\xa8 1\x00s \xe9\x97\xb4\xe6\xad\x87"
    "c\x01\x00\x96\n\x00"
    "1\xe6\x9c\xaa\x04\x00/\x08\x00\x95+\x0c"
    "22\x02\x01\xdc\t\x00\xfdleftR'\t\x07\xf8gsP\x07\xf8g\x05\x07\xf7g\x01\x02\x07\xf6g:null,\x80v=g?g[0\x02\x08\x00\x00ok=!!(g& &g[2]\x02\x00\x06"
    "3]\x88),f\x04\x07\xadmax\x01\x00M\x03\x04\x00\x0b\x01\x02\x85"
    "1,v/10\x00"
    "00)),\nlxD=W\x01\x04\xd0"
    "7,r\x03\x00\t9\x10"
    "3,T=\x01\x00\xe5"
    "3,B\x03\x02\x00\x07\x01\x02\x04H2=B-T\x00,aLX=(lx\x00+rx)/2,\n\x00"
    "col=f>=.\x18"
    "8?[\x02\x02"
    "0\x01\x00"
    "7122(]:(\x02\x00\x14"
    "5\x04\x00\x14"
    "19\x80"
    "4,71]:[\x01\x07\xfc"
    "D23\x01\x02\xb5"
    "68]\x01\x00\xcbr\x00r(x,lx,T\x85\x01\x00r-\x01\x00\x08H2,2\x10\x03\xc1 70c15\x19\x03\xc1#1`e2d49\x1a\x03\xb3\x02\x06`k\xa1\x04\x01\x82"
    "fw2=\x01\x00\xbc(\x03\x00r\xc8)*f\x03\x00Oav\x02\x00'\x05\x00\x8f\xb0+1,T\x01\x00\x04\x03\x00!-\x02\x00\xf5\t\x01\x00\x05"
    "19\x04\x06\xb4ip()\xe7\x10\x03\xda\x05\x07\x81\x03\x00/T-\x03\x02\x11\x02\x00\x8a\x01\x00\x13\xc7\x01\x00n\x04\x00\x12\t\x04"
    "6=22\x04\x04"
    "8\n\x00&\x91\n\x05H.8+\x02\x07\xf9"
    "6)\x02\x04!\x81\x02\x00?+H2*i/\x01\x02j\xf3\x02\x03\xc3\t\x00ZB+\n\x00l\x03\x00\x7f\x05\x00\x13\t\x07^U\x05\x02"
    "2g\x18\x06\xa2T\x01\x02\\B\x02\x06\x9bg3\x0f\x06\x9b\x03\x03\xef'+\x01\x01\xe3\x01\x02Z+'*,\x04\x00\x0b"
    "1\x08\x00\x0b"
    "2\x02\x00\x0b.9(5)'\x11\x00"
    "A1'\x00"
    "A4)M\x0f\x07og\t\x01\xf2\x02\x00\xc2nb\x04\x02\xc1r\x00ound(6+f\xc8*30\x0b\x01Xnb\x0b\x06\xc1\x01\x05\x90\x82(\x01\x06qf*.7)\x03\x06\xc8"
    "B3\x02\x05\x97),bx\x02\x02\x00"
    "9\x1c+(\x01\x01j\x03\x00\x14\x07\x03\x17"
    "14,\x13\x01\x01V\x02\x01\xf6"
    "14\x01\x03\x10"
    "byy5\x01\x02\xef"
    "7\x01\x06\xbf(\x01\x02\x04\x01\x00\x13,r\x18v=1\x01\x05\xb3\x02\x00:31)\x18%5)\x01\x03\x16\x03\x02]bx<\xcd\x02\x00:4\x02\x01\xd4\x10\x05\xf5"
    "bx\x02\x00L\x01\x00>\x07\x1d\x06\xf5\x06\x00\x04\x03\x05\xcf"
    "4+.34\x04*f\x1c\x05\xcfresto\x84re\x06\x01#rgt=\x01\x00\x88"
    "b>\x01\x03\xbd"
    "74;'\x05\xef%\x01\xa5"
    "1\x7f\x0e\x05\xe8\x01\x00o\x01\x06(\x02\x04\xae\x01\x05(\x03\x04\xf7\n\x05\xf2(\x0b\x02\x04Y\x08\x00\xbf"
    "1\x01\x00\xbf \xe2\x80\xb0\x0e'\x02\x01"
    "d\x01\x03\x7f\x01\x00<-16:\xf8"
    "16)\x01\x02\xe2\x01\x01]\x13\x05"
    "E\x03\x07\xd8\x11\x06\xfc\xf2"
    "2\x01\x04"
    "C42\x03\x06\x9b\x01\x01\xde\x03\x00w\x08\x06\x18\x1e"
    "7\x0e\x00\xea\x11\x06\x9a\n\x06\x17\x05\x05(?'-\x00- \xe4\xbc\xa0\xe6\x84\x9f\x81\x01\x06\x04\xe8\xaf\xbb\xe4\xb8\x8d\x01\x06\xac\x00\xef\xbc\x88\xe6\x9f\xa5 A\x00O\xe2\x86\x92PA6\xef\x0c\xbc\x89\x01\x00\xed\x01\x00*\xe8\xaf\xa5\xe8\x04\xb7\xaf\x01\x06\x12\xe5\x90\xaf\xe7\x94\x04\xa8'\x02\x05\x16,(T+B\x99\x01\x05\x14+6\x14\x00\xcf\x02\x01\xa4tx\x0b\x04\x80$.5\x03\x04\x81"
    "et\x02\x03\x96"
    "Da\x00sh([9,7]\x1c);\r\x04\xe5\x08\x07\x8f\x03\x05K,.5\xf4"
    "5)\x0c\x04\xf2"
    "1\x04\x00K\x13\x04\x9a\x01\x00u\x01\x01"
    "f\x86"
    "9\t\x04-\x02\x00\x12"
    "B+9)\n\x05'\x95\r\x00\x88]\x0c\x04\xb6"
    "4\x08\x03^xv\n\x00\xc3Xi/4\x0f\x05\x94\x03\x00\xaf"
    "9\x01\x01\xa6"
    "5\x88"
    "0,2\x01\x01\xae.24\x0e\x00\xaf"
    "9\x17\x00\xadxv\x06\x00\x9a\x05\x00\xac\x03\x00\x11"
    "15m\x0b\x00\xac\n\x14\x07\xef\x0b\x02\t2-\x02\t\x01\x03\xd7"
    "2\xd2"
    "5\x01\x02\xa7',\x03\x00r3\x14\x07\xda\t\x07\xdb"
    "dwd\x02\x07\xdb"
    "dz1\x07\xdc\x02\x00"
    "1w\x81\x05\x07\xc6ws)||[\x02\x07\xe2\xa1\x03\x00\x02],en\x01\x07\xc0w\x01\x07\xb6\x15\x05\x07\xcaw\x03\x07\xc7w\x03\x07\xc7v=o\xb8k?w\x01\x03\xa8\x01\x03^\x01\x07\xebq\x04\x00\x0fR1\x05\x00\x0f\nm\x17\x07\xe6"
    "6\x01\x07\xe4;\x00\nWP=(WP+\x04"
    "dz\x01\x05\x1d"
    "12+m*F1\x02\x02J\x01\x05\x19;WA\x01\x00\x1a"
    "A\x15\x04\x00\x1a"
    "5\x02\x00\x19"
    "1\x01\x00\x17"
    "6.2H832\x02\x01&cl\x01\x06"
    "5R\x04"
    "ec\x02\x06"
    "10,W,H]\x05\x04tb\x19\x06Y\x02\x00\x02\x02\x00(b\x15\x06Y1\x80"
    "6,32,58\x04\x02\xce\x08);b\x15\x06@5,9,\xd1\x01\x00,.06\x0f\x06'b\x06\x06'\x11\x00\x9b\x80"
    "dx=70,d\x01\x03q\x00"
    "722,dy=1\x84"
    "18\x01\x00\x07"
    "2=26\x01\x00\x0f\x04h=\x01\x00\x0b-dy,\n\x03\x01\x04\xe2\x02\x01"
    "F(m>.62\x04?[\x08\x03"
    "f]:[56\x03\x01\x02\xc0\x03\x05\x97]):[11I\x01\x02\xd3"
    "34\x01\x00\x8d"
    "8]\x01\x00"
    "8s\x87\x03\x02\xe9\x1f\x05:\x02\x05"
    "8/* -\x01\x00\x01\x00 \xe9\xa3\x8e\xe9\x80\x9f\xe5\x00\x88\xbb\xe5\xba\xa6(0~\x80"
    "60 m/s)\x04\x00\x1c"
    "8*/\n\x13\x02\xa4\x1a\x02\xd5\x15\x03\xa2sxL=d\x01\x03\xa2\x01\x00\xfc-d'\x03\xa3"
    "6\x89'\x03\xa3sx\x01\x01H-12\t\x03\xa5m\x04\x00\x13"
    "4%\x03\xa5\t\x03s1\x04\x03s\x05\x00S9\xa8)}\n\x10\x03u;\x1f\x00I'\x01\x01G2'\x02\x01\xec+9\x05\x00H\x03\x07\x99ok\xd5\x04\x01\x10v\x0c\x01\x10m\x17\x00\xdav\x03\x00\x87\n\x00\xd9hvx-\x02\x02"
    "6+\x01\x03\x10\n\x00\x15+\xc1\t\x00\x15"
    "close\x05\x00J\x0b\x00\xf4\xec"
    "cs\x07\x07\xb0\r\x07\x8f"
    "1\x18\x07\x8f\x01\x00"
    "5\n\x04\x96\xcav\t\x07"
    "C,\x07\x03\xdd"
    "dx\x08\x03\xde\x01\x00\xa2o\x01\x03\x1e\x01\x07\xf5\x01\x03\xc9\x03\x00\xbe"
    "8\x01\x01"
    "E\n\x02q\x81"
    "b\x93\x07\x02"
    "arr(\x01\x00\xe2\x02\x00\xe5,\x17\x04\x01\r\x01\x03\t\x01\x02\xcf)\r\x01"
    "b0700c15'\x0b\x00\xbe\x0b\x02>#1\xa0"
    "e2d49\x0c\x02"
    "12\r\x01\xfd\xd8sav\x02\x00\n\x05\x00t+\x02\x03\x84\x02\x00\x05"
    "9\x03\x00x-4\x01\x00z\x01\x00\x05\x04\x07\xfb"
    "cl\x08ip(\x06\x01\xb3&&v>\x12"
    "0\x04\x01\xb8nb\x04\x04\xd0rou&n\x01\x01\x01\x02\x04\xb1"
    "64\x0b\x06\x98nb\x05\x08\x02\xf6p\x03\x04\xdci*.13\x92"
    "7\x01\x04\xd3,p\x03\x01\xf6p*\x06\x01\xf8\x90,py=\x02\x01\xb5"
    "2+\x02\x02y\x1a"
    "9\x01\x00$(\x01\x00\x84\x01\x06\x9b),\n ln2=6\x01\x00_24\x81\x03\x00\x1d"
    "53)%7)\x14\x03"
    "5\x07\x1e\x03\xf0\x01\x00\x0b\x01\x05]+.34*\x08(1-\x03\x00\xc0"
    "abs(\x84p-\x01\x01\xa1*2))\x07\x01\xd1L3)\x04\x04\x1d\x0c\x07\xc7"
    "7;\x15\x02\x9cp\xc6x\x01\x00\xc1\t\x02\x85px+\x01\x00\xb6\x05\x00\x14\x05\x06\x01x}\x03\x01\xfb\xe5\x8f\xb6\xe8\xbd@\xae: \xe8\xbd\xac\x01\x04o \x00= \xe5\xae\x9e\xe6\xb5\x8bG\x04\x04~\x02\x02\x08\x02\x01/cx=\x01\x01\x1a+\x81\x01\x01\x1d)/2,cy\x01\x00\x0e\x0cy+\x01\x05\x15\x02\x00\x0eR=42e\n\x01g6\x08\x01"
    "fa=\x01\x06'\x01\x03\xb8.\x10"
    "0472\x17\x03Icx+\x01\x03\x00\xfa"
    "cos(a)*\xb6"
    "9\x01\x00`\x04\x00\x11s\x01\x02\xb2\x02\x00\x11)\x06\x00\xf8\x01\x0e\x00-+.44)*Rp*.98\x0c\x00"
    "5\t\x00\x19\x19\x00=6z8\x03\x00$6\x0e\x00<\x06\x00\x18\x19\x00;\x01\x02\n*\x1f\x0f\x00t\x03\x00\x15\x1b\x03\xe6(\x02\x16\x02\x06"
    "f.32\x00+.5*m):.\x16"
    "2\x04\x02\x00\x06\x03"
    "e}\x0f\x01Larc\x8f\x01\x00\xa4\x01\x00\x92\x01\x06v\x01\x06\xdb"
    "6.3\x0c\x03\xa9\x01\x01\x00P'#e8f2f\x00"
    "f':'#8ea\xd8"
    "6c4\x0c\x03\xb6\x16\x00Q4\x01\x01\"\x11\x00R\x19\x01\x03\xfb"
    "b1\x01\x06\x0f\x0c\x00"
    "Eres\x18tor\x03\x03\xd6\x06\x04X\xe5\x8f\xb3\x00\xe4\xbe\xa7: \xe5\x8a\xa8\x00\xe5\x8e\x8b\xe6\x9f\xb1 (\x01\x01\x00\x05\xe6\xbb\xa1\xe9\x87\x8f\xe7\x08\xa8\x8b \x02\x06\xadPa \xe2\x80\x89\x88 31.6\x02\x06\xe7\x00; \xe8\xb6\x85\xe4\xba\x86\x00\xe5\xb0\xb1\xe9\xa1\xb6\xe6\xa0\x00\xbc\xe5\x8f\x98\xe7\x90\xa5\xe7\x00\x8f\x80\xe8\x89\xb2, \xe6\x00\x95\xb0\xe5\xad\x97\xe4\xbb\x8d\x00\xe6\x98\xaf\xe7\x9c\x9f\xe5\x80\x06\xbc\x08\x07\x18\x02\x02xbx=83@6,pw=9\x01\x00\x06y\"T\x03\x07\xcfpyB\x03\x07\xcf"
    "FS\x84P=\x01\x00s,rr2\x02\x01\x16\x85\x07\x05(0\x08\x05'1,q/\x01\x00'\x00)):0,\nsaDt=\x02\x04jq>=\x01\x00\x14,\xb8"
    "bc=\x01\x00\x12\x0c\x07\xfc\x01\x01\xb9;\x04\x05"
    "0\xda"
    "b\x02\x03yT\x01\x00z\x02\x00o-\x02\x00\x0b\x04\x06\xfb\x0fN\x05"
    "2\x01\x04\xfe\x01\x00\xc0\x06\x04\xfc"
    "bhh=\x1a(\x05\x00q-\x01\x02\xbd\x01\x00\x1b,\np\x04g=\x01\x02\x8areate\x02L\x01\x00"
    "FarGrad\xb0ient\x01\x00\xea\x01\x00,,\x02\x00\x06\x02T\x01\x00Kpg.add\x00"
    "ColorStofp\x01\x00\x1e\x06\x02\xa6"
    "bc\x06\x02\xa5\x01\x00\n1\xd7\x07\x00\n\x03\x02\xa3\x01\x02\x0e'\x11\x00=1$\x00=\x01\x03\x97\"'\r\x03\x1bpg;\x05\x01"
    "B+4y\x03\x01=4-\x01\x00\xd7\x01\x01J\x01\x07T\x02\x00\t8N)\x0b\x02\xfe\x04\x07!\x02\x02;12\x0e\x07!t\x01\x01\x07\rAlign=' left'\n\x04\x9e=41\x08\x04\x9fty=\x02\x00i\x06\x01:)*\x08i/4\x14\x05\xc8"
    "90,1\x08"
    "50,\x01\x02\xee,.22\xba)\x0c\x01\x9a"
    "1\x17\x04\xda\x01\x00\xcc\x01\x02\nt\n\x05\x8a"
    "9\x03\x00\x13+8\x05\x00\x15\n\x07\x03\n\x02#5b\xb0"
    "7597\x06\x02#\x03\x07\xdd'\x01\x04\x15"
    "a\x01\x05K50),\x04\x00"
    "D\x01\x03\xe8t\x18y+4\x04\x01\x0e\x1d\x00<Pa'+\x08\x00"
    "6\x04\x02%;\x07\x01"
    "E7\x01\x01"
    "E26\x0b\x0e\x01"
    "E\x0b\x04;(\x02\x02\xf3'#ff\x00"
    "c247':cs\x14):\x04\x01\x19"
    "1\x01\x01\x12"
    "142\x00,172,.75W\x04\x06\xa3\x07\x00x\x01\x00:q\x07\x06\xc3"
    "1\x01\x00"
    "4-\x8a-\x02\x00\x88-\x03\x03\x95-34\t\x00\x85\xcd\x03\x01\xca"
    "3\x18\x00\x85\x1b\x00\xd8"
    "62\x08\x00Q\x0c\x04v\x00\xe5\x85\xac\xe5\xbc\x8f\xe5\x9d^\x97\x07\x04 \x1c\x02;\x12\x00q\n\x00\xd2'\x04\x04\xc5\xef\x00\xbc\x88\xe4\xbc\xaf\xe5\x8a\xaa\x00\xe5\x88\xa9\xef\xbc\x89',H103\x03\x00\x84+4\r\x01Y1v7\x19\x00"
    "c\x04\x01"
    "C6\x01\x02Y\x01\x04\x90\x02\x04Q.$86\r\x01"
    "C'q\x01\x07\x93\xc2\xbd \xcf\x81v\xc2\xb2\t\x00g32\x13\x0e\x01<0\x00\xcb\xcf\x81\x01\x00[1.2\x80 kg/m\xc2\xb3\t\x00^\n5\x0f\x00\xc6"
    "5\x1f\x00\xc6"
    "26,2d41\x04\x00\xc6"
    "78\x0e\x00\xc6\x02\x05\xdd"
    "0\x91\x01\x05\xdc\xc2\xb7 \x0c\x00\xc9"
    "78@\x00\xc9\x01\x01\x01\x8e\xe9\xa3\x8e\xe9\x80\x9f\xe7\x00\xae\x97\xe5\x87\xba\xe6\x9d\xa5X\xe7\x9a\x84\x01\x00\x12\x03\x01\xa9\x8c\t\x00m1\x08"
    "02)\x0c\x00;\xe3\x80\x80\xe4\x00\xb8\x8d\xe5\x8f\xa6\xe8\xa3\x85\x01\x01\x00/\xe5\x8a\x9b\xe4\xbc\xa0\xe6\xe0\x84\x9f\xe5\x99\xa8\x0c\x01\xd8\x01\x02\xe9\n\x02]\x00\xba\x95\xe9\x83\xa8/\xe6\x97\x02\xa0\x01\x06\x8b\xe6\x8d\xae\xe6\x8f\x90\x18\xe7\xa4\xba\x07\x02"
    "d\x01\x05\x9e!ok\x94){\x0b\x04\x8b"
    "c\x01\x05{er\x03\x00\xd1U\x08\x01X9\"\x01X1\x03\x06k6\x01\x02\x1e"
    "2\xd1\r\x01Xen?\x01\x03T \x04\x00\xfe\x07\x00\xba\x81\x01\x00\x97\xe5\xba\x94\xe7\xad\x94\x02\x01o\x00\xe6\x9f\xa5 485-\x10"
    "A/B \x01\x07/\xe5\x90\xa6\x00\xe6\x8e\xa5\xe5\x8f\x8d\xe3\x80\x00\x81\xe6\xb3\xa2\xe7\x89\xb9\xe7\x04\x8e\x87\x04\x00\x18\xe4\xb8\x80\xe8\x87"
    "0\xb4'\n \n\x00\x01\x02\x03\xb4 \xe8\x00\xaf\xa5\xe8\xb7\xaf\xe6\x9c\xaa \xe5\x90\xaf\xe7\x94\x01\x01\x1a\x88U\x80SE_WIND\x01\x00Z\x05\x01\x02"
    "E0\x03\x01,W*.5,\x00(dy+dy2) /2+7)\x11\x05\x8c}\n else{\x0c\x01\xf2.5\x01"
    "0\x03\x88\x8f\xb6\xe8\xbd\xae\xe8\xbd\x02\xac\x02\x00\xf9\xb8\x8e\xe6\xb0\x94\xe6\x00\xb5\x81\xe9\x95\xbf\xe5\xba\xa6\x81\x01\x00\x91\xe5\xae\x9e\xe6\xb5\x8b\x04\x01\x17\x11\x02\x01\x05\xe8\xaf\xbb\x01\x01\xa9\xe7\x9b\xb4#\x01\x00\xfc\x01\x02\x1f\xe8\x87\xaa\x02\x01\x14 \xe6 \x80\xbb\xe7\xba\xbf\x01\x00\xcfMo\x00"
    "dbus \xe5\xaf\x84\x08\xe5\xad\x98\x01\x01"
    "B 0x0#\x01\x00\x01\x03\x00\xd7"
    "dx,\x01\x00\xd1+2\x10"
    "6)}}\x07\x02\x01\xe9\x99\x8d\x00\xe9\x9b\xa8\xe8\xa7\x86\xe7\xaa@\x97: \xe6\x9c\x89\x01\x00\x0e \x00-> \xe4\xba\x91+\xe6\x04\x96\x9c\x01\x00\x0e+\xe5\x9c\xb0\xe9\x84\x9d\xa2\x02\x01"
    "d\xba\xb9; \x01\x01\x94\x01\x05\x00#\xe5\xa4\xaa\xe9\x98\xb3+\x00\xe5\x85\x89\xe8\x8a\x92+\xe5\x0c\xb0\x91\x01\x00"
    "4\x07\x02:func\x00tion rn2\x10(c,t\x04\x06\xa9x=c`.getC\x01\x01"
    "3\x03\x00\xfd"
    "2\xa0"
    "d'),W\x01\x00\x15w\x02\x06\x81\x04,H\x01\x00\nheigh t,i;\n\x02\x00"
    "1r=\x00(D&&D.rn\x04)?\x02\x00\x06:null\x00,en=!!(r\x00&&r[3]),&o\n\x00\x0f\x02\x00\x06"
    "0]\x03\x03\x1e"
    "cl\x00"
    "earRect(\x00"
    "0,0,W,H)Q\x04\x00Ucx=\x02\x01\xe3;\x02\x02\xdfo\x88n){\x03\x00\x15"
    "bg=\x01\x00"
    "2@reateL\x01\x06\xe2"
    "a\xc0rGradi\x01\x02\xeb\x03\x00=\x02"
    "0\x03\x00=bg.add\x00"
    "ColorSto\x06p\x01\x00\x1a\x04\x02\xdc"
    "34,54L,8\x01\x02\xd9\x01\x04\xf6);\x0e\x00'1\x87\x05\x00'\x01\x03\x01\x01\x06"
    "e8,.0\x03\x00'9\x0b\x06\xef"
    "bg\x05\x02-\r\x00\xb0\x11\x03I04(,13\x05\x06\xaa"
    "5\x03\x06\xaa"
    "fo\x00r(i=0;i<`5;i++\x02\x03\xb0\x0c\x07\xc7"
    "a\x00rc(cx-18\x00"
    "6+i*93+M\x01\x01\x00\x19.sin(t*\x00.7+i)*7,\x08H*.\x01\x03\xa2"
    "40+(\x81\x02\x07\x9e"
    "3)%18)\x01\x00\x86\xd0"
    "6.3)\x05\x00\x9a(\x0e\x07\xa2\x05\x03\xd8j8\x01\x03\xd8"
    "8\x04\x03\xd8"
    "4\x05\x07"
    "9\x0c\x00~e\xc0llipse\x01\x00\x82\x02\x00"
    "f\xd8"
    "3,2\x01\x00\xbe\x01\x00l0\x0e\x00_\n\x00\xc9\x8c"
    "88\x04\x00\xca\x02\x01\x9bp=(\x01\x00\xa9\x08"
    "1.4\x01\x00\xbd.071!\x01\x00\x9c),\nr\x01\x02Mx-\x03\x01\x05\xa7\x03\x00\xb2"
    "97)%51@0),ry=\x02\x00"
    "e+(p*(\x01\x00\x08"
    "5\x04\x05(stProke\x0b\x00\xb7"
    "5\x02\x06\xad"
    "5\x01\x03\x05\xe7'+(.5*( 1-p))\x07\x07\xe6"
    "3)T+'\x04\x00\xcdl\x01\x02\x0fW\x02\x02\xa0=\x10"
    "1.7;\x0e\x00\xddmov\xc0"
    "eTo(rx\x01\x00z\x02\x00\x10S\x02\x00.\x03\x00\x10-8\x01\x00\x12+\x01\x01"
    "F;W\x06\x00\x80\x02\x01"
    "A\x08\x00\xe2"
    "4\x08\x00\xe1q\x03\x00\xe1.\xb1\x02\x01\x9d.25\x02\x00\xdf\x15\x00\xb5"
    "2\x01\x02@\xa4"
    "90\x07\x00\xb5"
    "34\x02\x00\xb6q\x10\x00\xb6%\n\x00\xb5"
    "2\x1e\x01\x91"
    "82\x01\x02\xbe+q\xd4*2\x01\x01\x13"
    "9\x02\x00\x08"
    "4\n\x01\x9b\x08\x00\xbb"
    "9\x1f\x06\x05"
    "21#\x06\x05\x01\x02)\x03\x07]95\x01\x0e\x07]\xe6\xad\xa3\xe5\x9c\xa8\xe4\x8c\xb8\x8b\x02\x04"
    "b\x01\x04\xee\xe5\x85\xa8\x01\x06\x9e\x01\x01\x04\xfe\xe6\x9c\xba\xe5\xb7\xb2\xe5\x00\xbc\xba\xe5\x88\xb6\xe5\x81\x9c\xa0\xe6\xad\xa2',\x05\x00\xbd"
    "6\x01\x01,.}\x03\x05\x8b\x04\x04"
    "1\x08\x03\xc7R\x01\x03\xc0"
    "al\x83\x07\x03\xc7\x04\x00"
    "042,6,\x07\x00\x0b\x1b\x02\x00"
    "A\x01\x00@r\x15\x03\xd8\x02\x00\xa7"
    "208\xf4,8\x01\x03\xb4"
    "5\x02\x03\xb3\x0e\x00)\x01\x02\xf1\t\x00+\x00"
    "164,44,.\xd4"
    "12\x12\x00,1\n\x00*2\x03\x04U\x0f\x04\x06&r\x15\x04\x06\x08\x02"
    "616\x08\x02"
    "7a=\x81\x01\x02"
    "03927+t\x02\x03W\xd0L=92\n\x03\xda"
    "1\x02\x02Q\x01\x03\xdbS\x14\x02L\x04\x00\xdc"
    "6,\x02\x03\xb7"
    "7\r\x02"
    "33L.4\x10\x02"
    "5\x05\x02\xe9"
    "cx\x04\x00"
    "dc\x80os(a)*L\x04\x01J\x1b\x08\x00x\x02\x00\x14)\x06\x03"
    "7\x10\x00"
    "0(L+\xbe"
    "3\x01\x03\xa6\x10\x00"
    "5\x04\x00\x19\x0c\x02r\x02\x00\xe5s\x19\x05\x98M\x03\x00"
    "F-\x01\x05\x08\x05\x00\x0b+7\x02\x03\xefs\x01\x10\x01\xce#fff3b0\x0f\x01\x01o\x0e\x00\x1d\x01\x07\xea\x02\x00\x1e"
    "cf4a\x85\x11\x00\x1e"
    "1\x03\x00\x1d"
    "9a1f\x04\x01\xaa\x9f\x12\x05V\x05\x02K\x03\x00\x86\t\x04\xcf\x04\x01lsg\t\x04\xde_\x10\x03\x10\x03\x01\x85\x05\x00\x04\x01\x03\xce\x0c\x05\xcc"
    "3\t\x04!23\x04\x04\"\x03\x05\x02"
    "33\x06\x04\"\x13\x05\xe5"
    "46\xc0+q2*92\x05\x01\x1e\x01\x02j\xe8"
    "3+i#\x05\xca"
    "5\x02\x06U\x01\x03\x9d\x01\x06\xa6\x18.32\x13\x00\x8d\x12\x00t180U\x02\x06V6\x0b\x06V5\x02\x02{6\x02\x00\x84"
    "1w\x01\x00R\x05\x06V\x02\x05\xba"
    "3\x13\x06V?\x04Z\x06\x02\xcf"
    "1\x03\x02\x00\xbd\x10\x04Z\xe5\xa4\xa9\xe6\xb0\x94@\xe6\x99\xb4\xe6\x9c\x97\x02\x04Z\xe9\x04\x80\x9a\x01\x04W\xe8\x81\x94\xe9\x94\x00\x81\xe6\x9c\xaa\xe8\xa7\xa6\xe5,\x8f\x91\t\x04T\x1d\x00\x94"
    "6\x01\x00\x94"
    "12\x04.5\x19\x00\x96#5b75\x04"
    "97\x0c\x00\x88"
    "en?('\x00"
    "BK_Raind\x00rop DO\xe2\x86 \x92PC14\x03\x04\xef\x8e\x9f\x00\xe5\xa7\x8b\xe7\x94\xb5\xe5\xb9\x04\xb3 \x01\x05\xe0(r&&r\x80[1])||0\x01\x05\xd8\x81\x02\x00#\xe6\x9e\x81\xe6\x80\xa7\x02\x00\x1d\x01\x03\x00\x1c"
    "2]?'\xe4\xbd\x8e!\x04\x00"
    "3=\xe6\x9c\x89\x01\x05"
    "9':\x10'\xe9\xab\x98\x0c\x00\x13)):\x00'-- \xe8\xaf\xa5\xe8\x04\xb7\xaf\x01\x00\xf0\xe5\x90\xaf\xe7\x94\x00\xa8\xef\xbc\x88RAIN\x00_ENABLE @= 0\xef\xbc\x89\x04\x01\x05-\x0c"
    "15\x03\x02"
    "b\t\x01\x04left\x80'}\n/* -\x01\x00\x01\x00 \xe5\x8c\x97\xe6\x96\x97\xe5\x00\xae\x9a\xe4\xbd\x8d\xe8\xa7\x86\x00\xe7\xaa\x97: \xe5\x9c\xb0\x00\xe5\x9b\xbe\xe7\xbd\x91\xe6\xa0\x00\xbc + \xe5\x8d\xab\xe6\x00\x98\x9f\xe4\xbf\xa1\xe5\x8f\xb7\x18\xe5\xbc\xa7\x02\x00\x12\x04\x00/\x84\x89\xe5 \x86\xb2\xe7\x82\xb9\x04\x00J*/\x00\nfunctio\x00n bd2(c,\x06t\x04\x02\xf6\x01\x07\xe1.getC\x03\x01\x01s\x03\x01\xc5"
    "2d'),W%\x01\x00\x15w\x02\x04\xa6,H\x01\x00\nhe\x80ight,i;\x03\x04,\x00"
    "b=(D&&D.\x90"
    "bd)?\x02\x00\x06:[\x03\x05g\x05\x07\x00\x02"
    "9\x01\x01\xb8],st=\x00"
    "b[0]|0,e\x00n=!!b[6]\x10,ok=\x01\x00\x17==2\x10,has\x01\x00\x0b>=1\xf9\x02\x00\xff"
    "cl\x01\x04q\r\x05\x8c\x03\x00i\x19\x04\x95\x02\x00\x02\x05\x02\x00(b\x15\x06U12,28R,\x01\x03l.6\x03\x06(b\x15\x06(4\x90,8,1\x01\x04\x17"
    "06\x0f\x06'&b\x15\x06'\x12\x05\xe8"
    "70\x03\x06n19)\x01\x03\x18"
    "15\r\x05\xe9"
    "1\n\x03\xe3=1\x16"
    "2\x14\x03\xe5\x05\x05\xfa"
    "6\x03\x03\xe7"
    "0,5\x98"
    "0);\x07\x05\xdf\x06\x00\x15H-\x01\x00\xce;\x0b\x05\xbd\t\x00Y4\x1d\x00X\x01\x00S\x01\x00"
    "F64q\t\x00X114\x0b\x00\x17\x08\x00X\x01\x00\x0c"
    "a\x04ve\x03\x00"
    "AtransBl\x01\x01\x85(W*.\x01\x04\xba"
    "9D2)\x15\x05"
    "Aaa=\x02\x04\x96"
    "8:5\x02\x05"
    "A4\x01\x04\x87\x14\x01-\x01\x00\xc9"
    "23\x06"
    "0\x03\x05\x8f\x01\x03"
    "c.5*(1\x00-aa)).to\x80"
    "Fixed(3\x01\x03\x88\x1d\r\x01"
    "D2\x12\x07-\x02\x05\x18\x02\x01\x98"
    "34+\x08"
    "aa*\x02\x01\x81-2.60,-.6\x0e\x00\xcd\n\x04"
    "38fPf0ff\x0b\x05x4\t\x00\xc4"
    "2\x01\x01\x07\xf0"
    "1.5708;\r\x0e\x00pr\x02\x02\t\x08\x07h2)*200-9,\x08\x07"
    "a\x04\x00\x12"
    "5,\xda"
    "1\x01\x02U0\x0c\x05|\x11\x00J-\x01\x02\xa5\x01\x00?\x14"
    "24\x01\x00"
    "0)\r\x04\xd8"
    "cde\x06"
    "f\x02\x00\xa5\n\x06\xd8restoFr\x02\x01\x95\x03\x03\x1fpx=\x01\x01\x91"
    "3 8,py=\x01\x05P56(,pu\x03\x01\x80"
    "6\x17\x01z46\x91\x03\x01z168\x03\x01z62\x02\x01{\x94pu\x1c\x01{3\x14\x01ypx\x01\x00y\xe1\x01\x00\xe7+pu*\x01\x02\x16\x07\x06o\x06\x01w\r\x1a\x00"
    "67\x12\x07\xc6\x01\x04\x14?'#2\xe0"
    "ee6a8\x01\x05_\x07\x05\xdb\n\x01\x03"
    "3\x0e\x05*\x0b\x06\xc0"
    "19\x0c\x06*\x0b\x01\xf8okj?\x07\x01\xfb:\x05\x04$4\x02\x01\x0b\x05\x03\xb6"
    "8\x03\r\x06\xcd\x02\x00"
    "0\xe7\xba\xac\xe5\xba\xa6\" \x03\x06&b[3\x01\x06\x07S A\x01\x00=N ')+\x01\x00\x11"
    "1\xa0]/1e6\x08\x01<6\x02\x06\x01\x81\x06\x00"
    "4--',62\x02\x05\xe4H00)\x10\x00Z\xbb\x8f\t\x00Z4\xd5\x01\x00ZW\x02\x00ZE\x05\x00Z2\x13\x00Z\x05\x00"
    "49\x07\x00Z74\x04\x00Y;\x07-\x02\x07)BD\x8eS\x02\x06\xf7\x01\x00\x8d\x02\x00\x9c\xe5\xb7\xb2\x04\x06"
    "B\x08':(\x03\x01\x94\xe6\x94\xb6\xe5\x80\x88\xb0 NMEA\x03\x07\x1e"
    "7\x01\x06\xda\x06\x00 \x01\x07k'\x01\x00\x10\x04\x00\"\xe6\x95\x10\xb0\xe6\x8d\xae\x03\x00#\x9f\xa5\xe8\x00\xbd\xaf\xe4\xb8\xb2\xe5\x8f\xa3\xc8 RX\x02\x07~B2\x01\x00\xe0\r\x07\x1b\x07\x01\x07"
    "2\x05\x07r\x03\x00\\'+b[5\xe2]\x01\x00\x11\xe8\xa1\x8c\x02\x00\x15\x08\x00"
    "C\x02\x00\x1a\"7\x02\x00\x1a"
    "8N1\x05\x00\xfd"
    "34\x01\r\x05\xaf'#7f96b\"b\r\x00\xdf\xe7\xba\xbf\x01\x00s\xe4\xbd\x10\x93\xe6\xa3\x80\x02\x00U\xe7\xbf\xbb(\xe8\xbd\xac\x03\x00R8\x02\x00R\xe6\xac\x92\xa1\x03\x00\xb2\x94\xb6\x03\x00\x14"
    "13\x02\x00\x15@\xe5\xad\x97\xe8\x8a\x82\x04\x00\xed\x80\x08\xe7\x9f\xad\x02\x07L\xae\xbd \xe4\x08\xbd\x8e'\x02\x01\x9f"
    "9]||\x83\x01\x07\xea\x01\x01\xac'/\xe9\xab\x98\x03\x00\x14"
    "D10\x08\x00\x15\xc2\xb5s\x02\x00<\xe5@\xae\x9e\xe6\xb5\x8b \x04\x00\x1f"
    "2a\x08\x00\x1f bps\x05\x01p\x02\x00\x85?\x05\x02\x00\x1f"
    "1\x01\x02\x03\xe5\x8f\x8d\xe7\x9b\x02\xb8\x01\x01'\xe6\xad\xa3\xe5\xb8\xb8\x02'\x01\x01\xf4\xe6\x9e\x81\xe6\x80\xa7\xf8\xe5\xbe\x85\x01\x00I\x01\x07\x97\x04\x02Q\x1b\x00\xfa\t\x03\"fr\x02\x07\xb6\x0c\x03#21\x1d\x03#\x0e\x04'.\x0e"
    "9\x01\x06\xce\x05\x03"
    "0\x02\x05\xb1"
    "194,\x1c"
    "71\x01\x00\x17\x11\x03/\x04\x02\x16\xe6\x9c\x89\x08\xe6\x95\x88\x07\x02<\x90\x9c\xe6\x98\x10\x9f\xe4\xb8\xad\x02\x00\xd7\x97\xa0\xe4\xa0\xbf\xa1\xe5\x8f\xb7\x01\x00\xcaW\x01\x06\xca\x07\x04\x06J\x10\x03\xd5\x01\x06\xd6uncti\x00on fx(v,\x00k,d){ret\x00urn v===@null||\x02\x00\nu&n\x01\x05'\x01\x04\xa9"
    "d?\x02\x01[:(Hv/k\x08\x03"
    "5d)\t\x00It\x00g(i,v){d\x00ocument.@getEle\x02\x00\x0b"
    "B\x00yId(i).i\x00nnerHTML\x04=v\t\x00"
    "9R(lab\x00,val,bad\x00,cls){RW\x00.push(['\x00<tr><td>T'+\x01\x00%+\x01\x00\x0f"
    "d\x02\x01\xd4"
    "a\x01\x01\x00\x8f class=O\x01\x00\x0c\x01\x00\x97\x01\x00"
    "6\x07\x00\x12'+\x01\x00\x0e:\xbc''\x02\x02\xfa\x01\x00"
    "8\x01\x00Y\x01\x00"
    "8/\x01\x00H\x14',\t\x00i]\x01\x01\x19ROW\x00S++;if((\x06'\x03\x00)\x02\x00\xa3"
    "dexOf\x06(\x03\x02I\x01\x01\x00"
    "0){IN\x10V++}\t\x00\xb1SH(\x08t,u\x0b\x00\xa6/tab\x80le><div\x05\x00\x86\x08sec\x01\x00}t+(u\xd0?'<u\x01\x00\x0bu\x02\x00\x86\x01\x00\t\x1f\x02\x00\x9a\x02\x00\x0b\x01\x00+\x01\x00\xda\x03\x00"
    "7',t@,'',0]\n\x01Pd\x10r(){\x01\x00\x9b"
    "CUR\xa1\x01\x00\x88'ov'\x06\x01\xa3}\x03\x06\x9d\x84"
    "a=\x05\x00\x1a"
    "da'?\x02\x01\xb1`:(PT[\x01\x00\x14\x01\x03\x07[\x18]),\x01\x01\t\x07\x00_i,n\x00=a?a.len0gth:\x01\x00\xb9\x04\x00\n,r\x05\n\x07\xa4n\x04\x07\xa4s+=RW\x02[\x01\x00/[i]:i] [0]}\n\x01\x01\xdd'd\xa0t',s+\x07\x00\xec'\x01\x01"
    "A\x13\x08\x00\x9f\x01\x00\x85){\x02\x00&hl'\x00,'\xe5\x85\xa8\xe9\x83\xa8\xc0\xe6\x98\x8e\xe7\xbb\x86\x01\x00(\x03\x00\x18\x08v',\x03\x01p' \xe9\xa1\xf6\xb9\x02\x00=\x03\x00\x16s\x01\x00.\x04\x02\xd0\x02\x03\xad\x02\x00!\x9a-\x01\x01p)\x04\x00&\x03\x04\x1f\x97\xa0\x04\x00\x1d\x07\x02\x01\x87\x06\x00<\x02\x00;m-cnt\r\x03\x00l\xb1\x01\x00\x1e\n\x00[}\nel\xf0se{r\x03\x00\xc7\x01\x00\xbf\x02\x01\x11\x02\x01\xd0\xa2,\x03\x00\x05"
    "0];\x06\x00\xa8r\x01\x06"
    "a-\x08\x00\x9er\x01\x06\x15\t\x00\x97r\x03\x06\x8f\xe6\x9c"
    "0\xac\xe6\x9d\xa1\x04\x05x\x04\x00\x8b\xe6\x88\x10\x96\xe5\xbc\x82\x01\x04>\xef\xbc\x8c\x01\x02\x05\xc9\x9c\xa8\xe4\xb8\x8b\xe6\x96\x80\xb9\xe6\xa0\x87\xe5\x87\xba\x01\x01\xdf}\x04\x00\x91n\x04\x00\x8e\x0b\x01\x04\x06\x00\xb3\x02\x00\xa3\x02\x01\xc3t\x08=NV\t\x03'sByC\x01\x02\x02<Name('o8n')\x01\x00\xbd\x05\x00\x91\x01\x00"
    "7(t\xb4?t\x03\x03\xca"
    "C\x01\x04m\x01\x00/:\x06\x04\xed\x87\x02\x01&\x05\x00g\x04\x00XCA=[\x07\x07\xe8\x80,'#38e1\x01\x07\x95\x81\x01\x00\n7b90ac\x02\x00\n@ffc247\x02\x00\n8 b5cf6\x04\x00\x14"
    "5d\x04"
    "7a\x04\x00\n8a3d'\xc0],\nANM\x01\x00M\x05\x05"
    "0\x00,'\xe7\xa9\xba\xe6\xb4\x9e\xc1\x02\x01w\x8f\x97\xe6\xbd\xae\x01\x00\t\x01\x05\x9f\xc8\xe6\xb8\xa9\x02\x00\x1b\xb4\xa7\x01\x05\x90\x01\x00\t@\xe8\x99\xab\xe5\xae\xb3\x02\x00\x1b\x9c\x90\x89\xe5\x8f\x98\x01\x00"
    "FWY\x01\x00"
    "D\x00\xe4\xb8\x8d\xe9\x9c\x80\xe8\xa6\x82\x81\x02\x00>\x86\x85\xe5\xa4\x96\x01\x00"
    "8H\xe5\xb7\xae\x08\x00\x0f\xb9\xbf\x04\x00\x0f\xe4\xc4\xbb\x93\x01\x00\x12\xe8\xb6\x85\x01\x01T\x02\x02  \x89\x8b\xe5\x8a\xa8\x02\x00\x18\xb8\x8b\x00\xe9\x9b\xa8\xe8\x81\x94\xe9\x94\x06\x81\x03\x00\x9c\x0b\x00\x9b\xe9\xa2\x84\xe8\xad\x12\xa6\x02\x00"
    "0\x8a\xa5\x02\x00\t],P\x92"
    "C\x01\x00 ok\x01\x00\x13wn\x01\x00\x05*b\x01\x00\xd1;\x08\x03"
    "aK\x07\x04xun,it\x04\x04y\x05\x05\x04'\t\x03\xc9k \x13\x03\x04Q\x01\x04L<i\x07\x04\x87/i>\xf3\x03\x03\xd5\x04\x04]em\x02\x03\xe0\x01\x00"
    "E\x02\x00\x1a\x01\x00\r'\x02\x03\xe8\x04\x03\xe1\n\x05jup\x01\x05"
    "1{D\x84=d\x03\x03"
    "8!d.o\x04\x03"
    "2\x1ckp\x01\x00\x9c\n\x00q\x02\x00i\xe7\xad\x89\xc7\x02\x06\x94\x03\x02x\x05\x00n--<\x03\x00\\\x0c\x00"
    "a;\x05\x02\xb2\x02\x03\x9d'\r\x04\x7f\x02\x01\x1a\x02\x02\x9e\x8e\xe9\x00\x87\x87\xe9\x9b\x86\xe6\x9d\xbf\x00\xe9\x80\x9a\xe8\xae\xaf \xe2L\x80\xa6\x07\x00>\n\x04So=\x01\x00\x95,\"i\x01\x00\x06i,g\x01\x00\x06g,*l\x02\x00\rl\x01\x00\x0el\x02\x00\x0fl,\x84"
    "ac\x01\x00\x08"
    "ac,p\x01\x00\x07\x98p,a\x02\x00\x16\x01\x01@z;\x03\x00?@gok=(g\x01\x03"
    "F&\x04&g\x01\x03=)?1:0\x00,tofb=(d .tf<0\x01\x00\xacV=\x14[]\x04\x04tz\x01\x04tz<2\x10"
    "56;z\x02\x04vV[z\x00]=parseI\x04nt\x01\x00"
    "0V.cha@rAt(z)\x01\x07')\x11\x04\x04\x83m-p\x01\x07\x0f',A\xd0M[al\x06\x07\xef;\x16\x06r\x06\x00/\xc8).c\x06\x03W='\x02\x00\x12\x02\x04k\x1cPC\x05\x00"
    "C\x01\x02\x1b\x05\x01"
    "Dm-n\x02"
    "e\x01\x01Gd.n==1\x01\x01\x07m\xb7\xb2\xe8\xbf\x9e\xe6\x8e"
    "B\xa5\x02\x00/d.ss\x01\x00-'T):\x01\x00\x0bn\x02\x04\x18\xaa\x01\x02\x88\xe7\x04\xbd\x91\x02\x07v\xa8\xa1\xe5\x9d\x97\x81\x01\x04\x1e\xe5\xba\x94\xe7\xad\x94\x01\x06y\t\x04\x01\x10su\x02\x00\xf9u/10\x00"
    "00)|0,hh\x80=(su/36\x04\x00\x0fPmm=(\x02\x00\x10"
    "6\x02\x00\x0e)P%60,\x02\x01\xb8u\x02\x00\t\nHp2=\x06\x02"
    "8(v\x07\x02\x97(\x80v<10?'0\x04\x06"
    "D\x14v}\x06\x00\xb7u\x01\x02"
    "Bp2(Dhh\x01\x04\x0e:'+\x01\x00\x0bm\xa2m\x07\x00\x0bss)\x06\x04~t\x01\x04\xd7\x00new Date\x02(\x02\x07\xc7TimeSt\x10ring\x01\x00\x0fsli\x80"
    "ce(0,8)\x07\x01\x10\x08"
    "con\x02\x04"
    "7\xe7\xbd\xae\xe4 \xbf\xa1\xe5\xba\xa6\x02\x01\nac\x07\x01\x01\xdc\x01\x00\xd4\x02\x00@Fixed$(0\x01\x00l%'\x05\x00\xf5"
    "dt(=(o\x03\x02\x04i\x02\x05P?(Ri\x01\x04\xc5-o\x01\x00\x05)\x01\x00"
    "5:Q\x02\x06\xa9,dh\x0f\x00$1\x02\x00$1\x01\t\x00$k='';\n/@* KPI \x01\x02\xc4\xe5\x00\x90\x84\xe8\xa1\x8c\xe4\xb8\x80\x00\xe5\xbe\x8b\xe3\x80\x8c\xe8\xaf\x02\xbb\x01\x04P\xe5\x88\xb0\xe5\xb0\xb1\x01\x01\x03\xf0 --\xe3\x80\x8d \x00*/\nk+=K(\x1b\x05\x04"
    "B\x02\x04`\xba\x01\x04\x12\x02\x00"
    "f?fx\x01\x03\x00\x8b,10,1):Q\x04\x06\x10\xe2\x84\x83\x05\x00\x1e'\x01\x01.e/\x02\x00\xc4\x0b\x00@\x01\x04\x91\x0c\x00@1\x0c\x00@%RNH\x14\x00@\x0c\x04\xe0\x01\x01\x14==\x02\x00\xd6?S\x02\x00"
    "7\x01\x02"
    "Bt>\x01\x01\xc7+\x04\x01\xc7"
    "d\xb2t\x07\x01I1)\x05\x00\x93\t\x00"
    "1e\x04\x00"
    "0`=5?'w\x03\x00"
    "1\x08\x00"
    "d\xa3\x80\xb0\xe9\x80\x9f\xe5\x81\x8f\x03\x00"
    "d\xef\x02\x03h\x04\x00_\x01\x00\xa9\x01\x01\xa4"
    "2\x05\x00\xaa\x01\x00\xa5\x06\x00\x1d\x01\x02\x00KMath.abbs\x04\x00#)>=\x01\x02\x9d\x15\x00\x1a"
    "5\x1e"
    "0\x07\x00s\x07\x00t\x04\x05\xdb\x01\x04\x9d\xe7\xba\xa7\x04',\x01\x00jp,1,0s\x01\x00"
    "e\x03\x00\x10p>\x02\x03"
    "8\x04\x00"
    "4\x06\x00"
    "2\xe6\x00\xb0\x94\xe4\xbd\x93\xe6\xb5\x93"
    "7\x03\x01J\x01\x04#\x02\x01Ig\r\x01\x89\x02\x04"
    "5?(\xe1\x02\x00\x1d'\xe2\x80\xb0\x01\x00@\x07\x01\xdf\x01\x03qC\x02\x03l\x01\x03\x86',\n \x02\x00\x01(\"!\x02\x00.||!\x01\x00/||{\x02\x00J\x02\x00\xab"
    "0\x02\x00\x0b\x01\x02@\x03\x00\xcc\t\x00\x7f\xe8\x04\x88\xb1\x01\x01\x89\xe5\x85\x89\xe7\x85\xe9\x01\x00\xa1li\x02\x07\xd5l\x02\x02\x08\x06\x01\xc2\x06\x00\x15\x11\r\x01\xc1\xe8\xa1\xa5\x01\x00"
    "5 LE\xf2"
    "D\x03\x00\xe7gl\x03\x00\xb2\x03\x00\xeb\x01\x00"
    "3\x01\x00\x0f"
    "41]\x03\x00\xea(\x0b\x00>\x07\x00?\xe7\x81\x00\xab\xe6\xba\x90\xe7\x9b\x91\xe6\xa4\xb5\x8b\x02\x04Rfl\x02\x00&(\x03\x00\t1\x02\x00:\xe6\x9c\x89\x04\x00#\x02\x04?\x97\xa0\x97\x05\x00\x0c\x06\x02W\x01\x07\xec!\x05\x00"
    "4||\x05\x00"
    "4\xa3\t\x00\xda\x01\x03\x15\xe9\xa3\x8e\x01\x01\xfc:\x03\x03\x19\x00\x85\xb6\xe5\xae\x83\xe4\xbc\xa0\x00\xe6\x84\x9f\xe5\x99\xa8\xe5\x90"
    "A\x03\x03%\x8f\xa3\xe5\xbe\x84\x01\x05\xe9\x94\x05\x01\x00\x03 \x01\x01"
    "A\xe5\x90\xaf\xe7\x94"
    "D\xa8/\x07\x04\xa5\xe9\x83\xbd\x04\x03"
    "7,\x01\x01\x00"
    "D\x8d\xe7\xbb\x99 0.\x00"
    "0 \xe5\x86\x92\xe5\x85\x85?\x01\x03_\x01\x00(\x01\x00"
    "c\x0b\x03S\x04\x00s\x01\x00\x9b"
    "d.\xe4ws\x01\x00\x9a&&\x03\x00\t\x02\x03\xc0\x01\x01!\xe3\x03\x00\x0c\r\x03^m/s\x08\x01\xb5\x05\x00"
    "6\x01\x01\xb8\x07\x07\x00"
    "7\x04\x02\xdc\x04\x00"
    "9>=3001\x0f\x02\xe3\xe9\x99\x8d\x01\x07\xf1\x03\x00trn\xf9\x02\x00qrn\x03\x06Y\x03\x00\x10\x01\x00"
    "6\x03\x01I\x02\x00\"\xbf\x03\x01"
    "F\x02\x00\t\x08\x01"
    "C\x05\x00y\x0b\x00:\x05\x00\t0\x02\x00}\x07\x02\x01\xbd\x0e\x00X\x0e\x01\xc7\xe5\x8c\x97\xe6\x96\x80\x97\xe5\xae\x9a\xe4\xbd\x8d\x03\x00\x86\x84"
    "bd\x02\x00.bd[6\x04\x00\x86i\x01\x00\n0]\x01\x03\xac"
    "2\x03\x06/\x05\x00):\x03\t\x00\x19\x01\x03\x10\xe6\x90\x9c\xe6\x98\x9f\xf8\xe4\xb8\xad\x04\x00\xa6\x04\x07\xd4\x01\x00"
    "e\x10\x00\xaa\t\x00^q\x05\x00\t0]!\x01\x00]\t\x01\xfc\x02\x05\x98k\x01\x01\x05\xefk);if(C$UR\x01\x00"
    "d'a\x01\x06\xe4"
    "fw\x12(\x01\x00\x1fRW\x02\x07`ROW\"S\x01\x07^INV\x01\x00\x06\nS\x02H\x01\x02\x90\x8e\xaf\xe5\xa2\x83 \x18\xc2\xb7 \x01\x04\x94\x06\x04\xd7'2 \x00\xe8\xb7\xaf DHT1Z1\x02\x00\\R\x06\x03/\x06\x05"
    "8o\x05\x04\xf8o\xb1\x07\x01\xda+' \x02\x04\xa1\x04\x00\xbb!\x02\x00 |);\x03\x00\xbe\x04\x00\x01\t\x00"
    "C\x05\x00"
    "d\x08\x00"
    "C1\xbf\x08\x00"
    "C\x02\x05"
    "7\x14\x00"
    "C\x02\x00"
    "A\x1e\x05\xbc\x0c\x00\x84i\x12\x00\x84\xfb\x1e\x05\xbf\x0c\x00\x84i\x10\x00\x84"
    "6\x05\xc0\x05\x00\x9c\x08\x00"
    "2\x01\x00\n\x8e!\x04\x00\n\x01\x01\xd2\x04\x05\xc7"
    "bad\x04\x01\xcdW\n\x00j\x01\x00\xab\x03\x00jh\r\x00jh\n\x00jh\xff\x0c\x00j\x03\x06~\x07\x00"
    "2\x0b\x01\x05\x0c\x00\x01\x08\x01\x97\x07\x04'\x05\x01\x9a\xee'\x03\x01\xfd\x02\x02\x18\x01\x07\xdao\x03\x03\xa0\x02\x00y\x01\x02\x9c\x07\x04\x00\x19\x07\x04"
    "0\x03\x02;\x9f\xa5 PA\xae"
    "1\x12\x01\xf9\x11\x01~\t\x00ki\x0f\x00ki$\x00k~4\x12\x01\xe1\x07\x00k\r\x02\xd9\x03\x06i\x03\x05\x11\x04\x05\xed'\xc0MQ-135\x02\x00\x1c\x01\x00\x13\xf0\xe6\x95\x8f'\x18\x00\xb3\x1d\x06\xa6\x02\x01\xd7\x02\x06\x98\x0e(\x03\x06\xa9\x01\x00\xb7\x03\x00\xa1"
    "AO\xe2\x86\x10\x92PA6\x03\x02H (\xe8\x0c\xaf\xa5\x01\x03O\x07\x05l)'),O\x02\x06\xa4\x08\x00u\x02\x00i\x05\x00\x94"
    "DO\x04\x00"
    "f(\x02g\x03\x06\x0f\xe8\xb6\x85\xe9\x98\x88\x08\xe5\x80\xbc\x02\x00\xad\xe7\xa1\xac\xe4\x84\xbb\xb6\x01\x05\x9e\xe8\xad\xa6'\x03\x06\xfe\xe7\x07\x00\x1f\x06\x04\xdf\x04\x06\xfc!!\x03\x06\xf3\x1c\x01\xf8\x04\x00\xda\xc0\xe5\xbc\x95\xe8\x84\x9a\x01\x01\x14\x06\x00\xb4\xcc /\x01\x00\x84\x03\x00\x0b"
    "7'\x01\x06\xd6\x18\x00"
    "D\x02 \x1b\x00^\xe7\xb2\x89\xe5\xb0\x98"
    "A\x06\x01"
    "8d.de?\x01\x00\x05u\x01\x01\x01"
    "0\xce\xbcg/m\xc2\xb3\xf3\x04\x01\x1c\x02\x05\x1c\x81\x9c\x01\x01\x15\x01\x00\xbb\x02\x00%)\x00p\xff\x08\x02\xbc\x11\x07\xc5\x02\x03\x05\x06\x03\xc6\x03\x00\x17"
    "3\x00\xc9\x05\x03\xf3\x03\x00_P ADC\t\x00"
    "c1\x02\x00"
    "c\xe7P\xa0\x81 (\x02\x02\xb6l\x02\x02\x8c/\xa0"
    "1000)\x07\x03\x8a"
    "2\x02\x02\x1d\x98V )!\x00\x87\x03\x00\xcb\xa1\xa5\x01\x00i\x10 LED\x01\x01\x8dPA5)\x01\x00Wgl\x03\x00\xca%\x02\x00"
    "bgl\x81\x03\x02\x10(\xe6\x89\x8b\xe5\x8a\x01\x02"
    "C\x04:(\x04\x00\x8b'(\xe8\x87\xaa\x83\x04\x00\x12\x01\x00\x1d\x85\xa2\xe9\x97\xaa\x01\x02`\xe2)\x05\x00o&&!\x03\x00"
    "9\x04\x05\xc4\x04\x00\xc9@\xe6\x96\xb9\xe5\x90\x91\x03\x00\xcb"
    "4\x11\x01\x00@\xe5\x8f\x8d\x01\x00\x0f\xef\xbc\x88\x00\xe7\x94\xb5\xe5\x8e\x8b\xe9\xab\x00\x98=\xe6\x9a\x97\xef\xbc\x89\x89\x02\x02"
    "b\xad\xa3\x0e\x00\x1c\xe4\xba\xae\x02\x00\x1c\x03)\x02\x1a\x04\x03\x8e\x81\xab\xe6\xba\x90\xe7 \x9b\x91\xe6\xb5\x8b\x02\x02\xcb"
    "DO\xc2 \x01\x02"
    "d PB0\x02\x06\xe9\x1f\x00L\x83\x03\x02R\x04\x00"
    "C\x8a\xb6\xe6\x80\x81\x02\x02R4fl\x02\x01\x00(\x03\x00\t\x02\x01\x1b\xe6\xa3"
    "b\x80\x01\x00]\xe5\x88\xb0\x04\x00)\x03\x03\xde\xb7r\xb2\x08\x03.\x97\xa0\x04\x00\x19\x04\x03+\x11\x03\x96,\xf4(!\x05\x00V)\x02\x03"
    "C\x05\x00Y\x05\x05\x8e\x01\x00\xb1@\xe5\x8e\x9f\xe5\xa7\x8b\x02\x01\x0b\xb9\xca\xb3\t\x00\x81'\x01\x00\xc5 =\x01\x01\xa4\x03\x00\x12\xcf\x02\x01\xb1\x01\x01"
    "1\x01\x01\xb2\x05\x00\x0f?'\x01\x01"
    "7\x01\x00\x80\xe8\xe4\xbd\x8e\x01\x05\xaa'\x04\x00?\x02\x01"
    "C\x06\x03\xb2\x05\x06\x00t;\x10\x00\xf0\xe7\x84\xb0\xe6\x9e\xf0\x81\xe6\x80\xa7\x05\x00o\x02\x04"
    "c\x01\x00H\x04\x00"
    "D\xf0=\xe6\x9c\x89\x01\x00$\x01\x00[\x01\x00"
    "a\x0c\x00\x13\x03"
    "2\x03\xb8\x03\x01\xa7\xe5\xa3\xb0\xe5\xad\xa6@\xe5\xb1\x82\xe6\x9e\x90\x01\x01\x99"
    "4\x00 \xe6\x8e\xa2\xe5\xa4\xb4\xe9`\x98\xb5\xe5\x88\x97 \x01\xa7\x04\x00\xb8\xad\x05\x01\x05S\x88\x01\x00J\xe9\x80\x9f',\xc4"
    "ac\x04\x03\x8em/s6\x04O\x13\x03\x94\xa0\x9f\xba\xe7\xba\xbf\t\x00"
    "e1R\x00"
    "e\xd1\x03\x00_\xe5\x81\x8f\x03\x07\x82(\x01\x00"
    "f\x03\x03\xe5\x01\x03\x07"
    "b',Math.\x88"
    "abs\x04\x00\x18)>=\x01\x03\xff\x19\x10\x00\x15"
    "50\n\x07\xd8\x15\x01"
    "2\xbd\xae\xe4\xec\xbf\xa1\x03\x05)\x02\x00"
    "83\x03\x00"
    "e\r\x07\xd2:\x00\xd8\x01\x02\x00"
    "d\xe9\xa3\x9e\xe8\xa1\x8c\xe6 \x97\xb6\xe9\x97\xb4\x02\x02Ntf<<0\x03\x06_\x01\x00\x13\x01\x03\x1b\x01\x01\xb2':;\x02\x00\x16\x03\x05\xa1s\x06\x00\"/\x00s\x03\x06Y\xb5\x8b\xa8\xe8\xb7\x9d\x03\x05\xf5m\x02\x01\x95mF\x01\x94\x03\x05\x00"
    "c\x02\x02\xa9\xe8\x99\xab\xe5\xae\xb3\x01\x02\x07\xd4\xbc\x82\xe5\xb8\xb8\xe5\x88\x10\x86\xe5\xb8\x83\x01\x02\xb2"
    "16\xc3\x00\x97"
    "16 \xe7\xbd\x91\xe6<\xa0\xbc\x1b\x02\xb1\x03\x05t\x03\x00"
    "F\x01\x02\xb1\xe7\xba\xc3\x01\x03"
    "c\x01\x07\xd9p,1,\x03\x01\xe0\x03\x00\x11\x8ep\x01\x01\xd4=\x00\xb9\x02\x00"
    "f\xe4\xb8\xbb\x04\x00\xa6@\xe7\xb1\xbb\xe5\x9e\x8b\x03\x02\xaf"
    "40]===\x01\x01\x81\x02\x05"
    "b\xb8\xb8\xdf\x01\x02\x1e\x04\x00\"\x01\x04"
    "F\x03\x00\x1d\x04\x00#!\x01\x00#-\x07\x11\x0f\x04\x00\xf9\x01\x01\x0f\x01\x00l\x01\x02Nfunc\x00tion(){v\x00"
    "ar h='';\x00"
    "for(z=0;\x00z<7;z++)\x00{h+=(z?'\x03\x02\x05"
    "1\x03\x02\xa7+'<spa\x00n style=\x00\\'color:@'+CA[z\x01\x03"
    "f\\\x80'>'+ANM\x03\x00\rI\x03\x04\xf7"
    "ah\x03\x00\x0c</\x02\x00"
    "7>\x00'}return\xc0 h})()\x02\x01\xfb\x03\x01\xb2\x88\xe6\x89\xa7\x01\x02\x81\xe5\x99\xa8\x01\x01\xb5\x1f\x05\x05\x95\"\x05\xfa\n\x02X\x03\x00"
    "6\x06\x01\xae"
    "AM[ al]||\x03\x05"
    "Eall>0\x01\x00\x05\x03\x01Sg\x17\x03l*\x00"
    "f\x81\xe2\xaf\x02\x07\x87"
    "B12\x02\x07}\x02\x00Z\x02\x00[\xd8\xe7\x81\xad\x01\x07y\x03\x00\x0e"
    "1\x01\x01\xbc\x03\x07n@ 500ms\x02\x01\xc2\xbf\xf2\xab\x02\x00\x0f"
    "15\x02\x00\x0f\x01\x07\x82\x02\x00\x94\x1f\x02\x81\xe0\x9c\x82\xe9\xb8\xa3\x01\x01\x15\x05\x05\xe1\x02\x06\xc8"
    "C\x01\x05\xc1\x01\x00\x81 1s \x01\x03\xab\xe6\x84\xad\x87\x02\x05\xe1\x9d\x99\xe9\x9f\x01\x06"
    "5\x10\x88\xe4\xbb\x85\x04\x00\x1f\xe4\xbc\x9a"
    "8\xe5\x93\x8d\x03\x07\x8a\n\x06\xaa \x00\xdf\xe8\xaf\x86\xb1\x01\x02\xf6\x03\x00\xdf"
    "d.lm\x01\x08\x00"
    "d\xbc\x80\x02\x00\xbd\x85\xb3G\x03\xa6\x03\x04o\x80\x00\x9a\xe9\xa3\x8e\xe6\xa8\xa1\xe5\x14\xbc\x8f\x03\x00\xdc"
    "a\x01\x04m\x87\xaa\xe5\xe4\x8a\xa8\x02\x07\x8b\x89\x8b\x02\x00\t@\x00p\x08\x00j\x01\x02\x07\x9c\x9b\xa0',WY[pd.vr\x06\x02\x18"
    "B\x00\xd2\x04\x05>\x8e\x00\xe6\x9c\xba\xe5\x8f\xb0\xe6\x95\n\xb0\x03\x00\xcfo\x01\x02\xfb/ 4 \x1b\x01\x00\x13\x04\x00\x10=,\x03\xac\x14\x00gFANX1~4\x03\x00Q\x04\x07"
    "6/\x03\x03`f\xcb\x04\x06\xde\x06\x00\r2\n\x00\r3](\x06\x1e\x03\x03\x88@\xe7\xb3\xbb\xe7\xbb\x9f\x01\x01\x8c\xe6\x0c\xa3\x80*\x03\x82\x06\x00\xa4"
    "DHT  \xe6\x89\xab\xe6\x8f\x03\x01\xd8sc\x01\x03\x00\x8c"
    "7 \xe8\x84\x9a\xe5\xbap\x94\xe7\xad\x94\x04\x00\x16:\x01\x0f\x03\x02\xa7\xb6"
    "b\x85\x01\x07~\xe6\xb3\xa2\x08\x06\xac\x01\x06\xa6\xe5t\x9b\x9e\x02\x00\x13:\x06\x05"
    "76\x06\xa6\x0c\x00k\xa1\x00\xa5\xe5\x85\x89\xe6\x9e\x81\xe6\x12\x80\x01\x04Wgl\x01\x01`?'\xe9\x00\xab\x98\xe7\x94\xb5\xe5\xb9\xb3@\xe7\x82\xb9\xe4\xba\xae\x01\x00s\xe4\x9c\xbd\x8e\x0b\x00\x12"
    "6\x02\\\x03\x00s\xbf\x90\x05\x07\x8d\x00\x95\xbf',hh+'*h\x01\x01\xcfm\x01\x07.m\x01\x00\x08ss\x98+'s=\x02\xbc\x03\x00"
    "f\x81\x94\x01\x06\x0f@\xe7\x8a\xb6\xe6\x80\x81\x02\x01+n\x01\x03\x04\xb6\xe5\xb7\xb2\xe8\xbf\x9e\xe6,\x8e\xa5\x02\x04\xd9\x01\x01\xabs\x01\x03+')\xca:\x01\x00\x0bn\x01\x01"
    "d\x9c\xaa\x04\x00"
    "6\x02\x01"
    "d\xd9\x01\x03\xbd\x9d\x97\x02\x01v\x04\x01\xd8)\x02\x00"
    "C\x01\x06\x93\xc7\x14\x07\x13\x03\x02\\\x01\x03'\xe9\x80\x9f\x01\x05\xe1\x03\x03\x92\x82\x8b\x02\x06RRS485\x01\x02h\x00ZTS-3000\x01\x02\x00\x14Modbus xRTU\x01\x02~\x05\x03_\x01\x00"
    "8\x02\x00\xa4w\xe2s\x01\x02\xd2?((\x03\x00\n\x01\x01\x89\x04\x00\t\x00"
    "0]/10).t\x00oFixed(1\x89\x02\x07\xa2m/\x02\x05u-- \x07\x00\xa2!\x02\x00[\xe6\x9f\xa5 \x01\x00q-A\x0c/B\x01\x05\x84\x03\x00 (\xe8\xaf\xa5\x08\xe8\xb7\xaf\x01\x00\xdb\xe5\x90\xaf\xe7(\x94\xa8)\x01\x05"
    "3(\x05\x00j&&}\x05\x00i)\x07\x00\x89\x05\x00\xb8\x01\x04\xa1\x01\x00\n\x18\x00\x93"
    "1y\x11\x00\x93Pa\x03\x00\x92"
    "2\x00y\x05\x01\x13\x02\x04\xc7\xa7\xb0\x8b\xe5\x80\xbc\x12\x00y\x03\x00\x08"
    "4\x02\x03\xf0\x80(\xe5\xaf\x84\xe5\xad\x98\x01\x06"
    "8\xc8 0x\x01\x01U0)\n\x00y\x19\x00g%\x01\x01\x1b \x01\x01\xfb\xe7\xba\x01\x02w'A\x00\xe2\x86\x92PA3(R\x14X)\x01\x03\xc3"
    "B\x03\x00\x0e"
    "2(T\x03\x01\x00\x0e\x02\x01\x00\xae\x9e\xe7\x89\xa9\xe7@\xbb\x8f MAX\x01\x00"
    "7/(SP3\x03\x01\xca"
    "0\n\x01*\xe7\xae\x02\x97\x03\x05\xe1'q = \xc2\x00\xbd\xcf\x81v\xc2\xb2 \xe2\x00\x89\x88 0.6\xc2\xb7!\x02\x00\r (\xcf\x81\x01\x00\x1c"
    "1.\x00"
    "2 kg/m\xc2\xb3\x06)\x04\x00"
    "C\x03\x02/\x9b\xa8\xe9\x87\x8f \xe7\x9b\x91\xe6\xb5\x03\x01tBK\x00_Raindro\"p\x02\x00\x0f"
    "DO \x01\x00\x94 P\x98"
    "C14\x04\x04\xa9\x03\x00x\x99\x8d\x01\x00"
    "7\xd9\x08\x02\xd5rn\x03\x01%\x03\x00\t0\x01\x03\xb9\x01\x04\xd6"
    "1\x01\x00N\xe5\x88\xb0\x04\x00)\x03\x00\xcb\xb7\xb2\x00\xe5\x81\x9c\xe6\xad\xa2\xe5\x85"
    "0\xa8\xe9\x83\xa8\x04\x05\xcc\x02\x02\xe1\x97\xa0\x1f\x08\x00%\x05\x06\xb1\x04\x04X\x18\x01\xc0\x05\x00q||!~!\x05\x00s\x04\x00\x97\x01\x00\xac\x04\x01\xbc\x04\x04$\t\x00\x9a'\xff\x02\x00\xc0\x01\x01\x02\x02\x05\xaf\x01\x00\x13\x02\x05\xca\x01\x07\xc9\x03\x03|\x03\x00\x0f\xfb\x03\x04l\x04\x04`'\x01\x02"
    "E\x04\x00@\x02\x07\xd9\x06\x01\xcc\x05\x00J?\x05\x00\xff\x03\x01"
    "3\x06\x04\xad\x03\x00\x1b\x02\x04\xaf\x07\x04\x9d=\xe6\xfc\x9c\x89\x01\x00$\x01\x00P\x07\x04\xc2\x06\x00\x13\x07\x01\xba\x03\x00\xea\x19\x01\x03\xe7\xe9\x94\x13\x01"
    "B\x02\x04!\xa7\xa6\xe5"
    "d\x8f\x91\x02\x01\x17"
    "4 \x01\x01\x02\x05\x06\xfe\xbc\x10\xba\xe5\x88\xb6\x01\x01J\xe8\xbd\xac\x0f\x02\x01>\x01\x04+\x07\x00'\x04\x00\"\xe6\x8c\x89\xe6\x00\xb8\xa9\xe6\xb9\xbf\xe5\xba\xa6x\xe5\xb7\xae\x01\x01"
    "6\x01\x00.\x06\x00\xc8\x06\x01\xf5\xe5\x00\x8c\x97\xe6\x96\x97\xe5\xae\x9a\x88\xe4\xbd\x8d\x05\x01\xf5"
    "BDS\x02\x00\n\x00\xe8\xbd\xaf\xe4\xbb\xb6\xe4\xb8\x80\xb2\xe5\x8f\xa3 RX\x04\x01\xfd\x1c"
    "B2\x08\x01\xfc\x04\x00"
    "5\x08\x01\xfc"
    "bd[\xa6"
    "6\x03\x00\xba\x01\x00\t0]\x01\x06s2\x03\x00\xbe\x83\x04\x00'\x01\x01"
    "eRMC=A\x01\x02\xdb\x00GGA fix>\x1a"
    "0\x02\x01W:\t\x00"
    "0\x04\x05\x0f\xe6\x94\xb6"
    "A\x01\x02"
    "0 NMEA\x03\x02/\xb0>\x9a\x02\x01\xf3\x03\x00"
    "E\x04\x00\xe8\x04\x00!\x01\x07\xfb\xe6\x8d\xbe\xae\x05\x04"
    "e\x02\x00\xac\x06\x00\xa9\x02\x03"
    "9\x01\x00\xa7)\x1a\x02"
    "6Y\x03\x00\xab||\x05\x00{\x01\x05"
    "F2\x04\x00\xd3\xe7\xdc\xba\xac\x01\x01-\x05\x00\xcd\x05\x00\xc4(\x04\x00\xa2\x01\x01\x95\xc8'S \x01\x00\x82N \x01\x02\x19\x04\x00\x14)\x02\x04ve6\x08\x04w6\x01\x02"
    "2\xc2\xb0S\x06\x01"
    "c\x10\x00^\xbb\x8f\x16\x00^4\x01\x00^W\x15\x02\x00^E\x08\x00^2,\x00^\xe5\x8d\x8a\x88\xe7\x90\x83\x17\x00\xbc\xe5\x8d\x97\x01\x00_O\x01\x01\xef\x02\x02\xd7\x04\x00,\x02\x01='+\x08\x00\x81\xe8$\xa5\xbf\x02\x02\xfa\xb8\x9c\x03\x02\xfa\xbb\x8f\x03\x17\x00k\x03\x01\x9c\xe8\xaf\xad\xe5\x8f\xa5\xdb\x08\x01\xf9\x03\x00\x08"
    "5\x02\x05\x10\x01\x07M'\x07\x03.\x03\x00\x1d\x7f\x04\x00"
    "7\x07\x01\xa6\x01\x02"
    "D\x01\x01\xa3\x02\x02U\x03\x00\x82\x03\x00%71\x02\x00:8N1\x03\x01\xd2\x03\x06\xec T\x13\x05\x01\xcc\x01\x02<\xe5\x8f\x02\x01\xf7\xe4\xb8\x8d"
    "1\x01\x02\xe2\xef\xbc\x8c\x05\x00!\x01\x00"
    "A\xe9\x82\xf0\xa3\xe6\xa0\xb9\x01\x00\x19\x01\x01\xdc\x01\x05K\x02\x02S\x07\x06\x03Z\x01\x05W\x01\x01\xfa\xe4\xbf\xa1\xe5\x8f\x8a\xb7\x0e\x02\xa0"
    "8\x02\x00n\xe6\xac\xa1\x04\x03\x94\x98\xe7\xbf\xbb\x01\x03"
    "F\x03\x02n\x85\xb1\x01\x00h\t\x06\x00\x94"
    "13\x02\x00'\xe5\xad\x97\xe8t\x8a\x82\x06\x01)8\x01\x01)\x01\x00\xde\x01\x00\x94\xe8\x0c\xbf\x99\x01\x00{\x01\x00"
    "f\xe4\xb8\x80\xe7\x00\x9b\xb4\xe6\x98\xaf\xe6\xad\xbb@\xe7\x9a\x84\xef\xbc\x9a\x05\x07\xad\xb2\x8a\xa1\x01\x00\x93/\x01\x00\x07\xe4\xb8\x8a\x01\x00j\x81\x02\x00\n\xe5\xbe\x80\xe5\xa4\x96\x03\x00\xc6"
    "6\x89\x08\x06!\x07\x02\x92!\x05\x00"
    "d\x04\x00\xc2\xe6\x9c\x00\x80\xe7\x9f\xad\xe8\x84\x89\xe5P\xae\xbd',\x04\x00\x80"
    "9\x06\x02\xb8"
    "1/\x01\x04\xed\x01\x04\xd0\x01\x04l\x06\x00\xaf"
    "9\x01\x00\xae\xc2\xb5\xees\x01\x03"
    "c\x01\x04n\x07\x00\xc4"
    "0\x05\x00\x16\x02\x01X\x03\x00Pc\x01\x00\xa3\x01\x00\xb2\xe4\xbe\xa7\x03\x06\x1b\x02\x00\x0b\xbd\x80\x8d\xe6\x97\xb6\xe9\x95\xbf\x02\x01\xf1\xf8\xe8\xbf\x98\x01\x00\xa7\x01\x04\xd6\x01\x03"
    "d\x01\x00\xe5\x03\x00\xdf"
    "F\x8a\x02\x00\xc3\x03\x01H\xef\xbc\x9f\x03\x01"
    "c!\x0b\x11\x00\x8d\x06\x04\x16\x9e\x01\x05\xf2\xe6\xb3\xa2\xe7 \x89\xb9\xe7\x8e\x87\x05\x01y12'\x06\x01z\x01\x00\n\x01\x01Tbp\x03\x00\x91\xe7\x94\x02\xb1\n\x00\xe4\xe5\x8f\x8d\xe6\x8e\xa8\xc1\x02\x01\xef\xbd\x93\xe5\x89\x8d\x01\x04\xc1\x0b\x02\x1e\xcf\x01\x01\x9a\x05\x05\x8a\x04\x00\xa5\x02\x06Z\x87\xba\x06\x01?\x01\x00Y?\n\x01\xf8\x08\x05\x95\x04\x01\xb5\x05\x00~\x02\x05\xe2\x01\x00g\xe7\x9b\x02\xb8\x02\x00\xdd\xa9\xba\xe9\x97\xb2='\x01\x01"
    "C\x01\x00X\x02\x01 \x8e\xa5\x02\x04]\x99\xa8\x89\x02\x04\x87\x8c\x89\x04\x00(\xe8\xa7\xa3\x02\x04x\xff\x03\x06\x8b\x08\x00"
    "4\x01\x01"
    "b\x05\x00\x8c\x02\x04\x92\x03\x00\x89\n\x01.\x01\x05\xe5\r\x06\x01"
    "1\x8c\x04\x00\x8a\x01\x06\xdf\xe4\xbb\x8e\xe5\x1c\x88\xa4\x01\x04\xcb\n\x02\xa6\x01\x01\x00\xe8\xbf\x91\xa0\xe6\x8a\xa5\xe6\x96\x04\x01-l\x02\x03J\x8el\x03\x02"
    "b\x01\x01\x9c\x01\x03L\xe9\x83\xbd\x02\x00R\x0f\x03\x04\xf6\x04\x01}\x02\x00!\x01\x00>dr()\x00}\nvar LI\x00VES=0,OF\x00"
    "FEL=docu\x00ment.get\x08"
    "Ele\x02\x00\x0b"
    "ById`('off\x02\x05\xbc\x02\x00"
    "2S\x00MTEXT='\xe6`\xa0\xb7\xe6\x9c\xac\t\x05M\x01\x00\xce\x8e\x00\xa5\xe5\x85\xa5\xe9\x87\x87\xe9\x00\x9b\x86\xe6\x9d\xbf \xe2\x80\x8a\x94\x01\x00\x03 \x01\x00$\xe5\x80\xbc\x01\x03\x89\x01\x02\x01\xa6\x98\xa0\xe7\x89\x88\xe5\xbc\x9e\x8f\x01\x00\xdf\x01\x03|\x01\x02\xfb\x02\x01\xf7\x97\xb6\x04\x00H\x00';\nfunct\x10ion \x01\x00smsg@(t,c){\x03\x00\x9e.\x00textCont\x11\x01\x00\x96=t;\x04\x00\x14"
    "cla\x00ssName=c\x01\x05\x00\x12style.d\x00isplay='\x80"
    "block'}\x08\x00X\x00poll(){f\x00"
    "etch('/d\x00?t='+Dat\x00"
    "e.now(),\x00{cache:'\x00no-store\x84'}\x01\x05Ihen(\x06\x00"
    "B\x00(r){retu0rn r\x03\x00\x94\x01\x01L)\n\x01\r\x00$t){if(t\x00.charAt(\x04"
    "0)\x01\x05\x04'{')t\x00hrow 0;\n\x00up(JSON. parse\x01\x00/);E\x04\x01\x8b"
    "1\x14\x00\xc7non\x02\x00\x8a\nx.ca\x02\x00\xb8\x07\x00h\x03\x00g\x03\x00:)\x16{\x05\x01"
    "2\x01\x05\x88\x8e\x07\x01\x84\xe8\xbf\x9e\x81\x01\x01\x96\xe4\xb8\xad\xe6\x96\xad\x02\x02R\x01\x01\x02\x8d\x9c\xa8\xe9\x87\x8d\xe8\xaf\x10\x95\xe2\x80\xa6\x03\x07\x93}el\x0cse\x06\x00=\x04\x01\xde,'sm]\x01\x00\x19}\x01\x02%\x01\x00_\x01\x01"
    "Ba\x02\x00m.\x00protocol\x00.indexOf\x00('http')\x01\x01\x06"
    "20){setT\x80imeout(\x02\x01"
    "dX,20\x01\x02\xa9\x01\x00\x15I\x01\x01\xb2rHval\x04\x00\x16"
    "10\x01\x00\x17}\x01\x02\x00\xff"
    "D);pv()\x00;request\x90"
    "Anim\x03\x00gFr\x01\x01\xcf\x08(fr\x01\x02p</sc\x00ript>\n";

static const char PAGE_HTML_HDR[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 47396\r\n\r\n";

/* 把当前状态拼成一帧 JSON 写进 g_txbuf, 返回字节数。
 * ★ 字段顺序与 `_page_tool.py` 的 FIELDS **严格一致** —— 页面的 JS 依赖这个顺序
 *   取数组下标的(如 o[0]=舱外温度x10), 少一个字段就会整体错位。
 * 这一段是 `_gen_page_c.py` 生成的, 不要手改。 */
static uint16_t esp_build_data(void)
{
    char    *p = g_txbuf;
    uint16_t k;
    uint8_t  j;

    p = s_put(p, "{\"u\":");
    p = s_dec(p, (int32_t)g.ms, 0);
    p = s_put(p, ",\"n\":");
    p = s_dec(p, (!g_esp_at ? 0 : (g_wifi_idx < ESP_AP_NUM ? 1 : 2)), 0);
    p = s_put(p, ",\"ss\":");
    p = s_put(p, "\"");
    p = s_put(p, (g_wifi_idx < ESP_AP_NUM ? esp_ap[g_wifi_idx].ssid : ""));
    p = s_put(p, "\"");
    p = s_put(p, ",\"o\":");
    p = s_put(p, "[");
    p = s_dec(p, g.temp_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, g.rh_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.dht_ok, 0);
    p = s_put(p, ",");
    p = s_put(p, "\"");
    p = s_dhtpin(p, (uint8_t)(g_dht_pin));
    p = s_put(p, "\"");
    p = s_put(p, "]");
    p = s_put(p, ",\"i\":");
    p = s_put(p, "[");
    p = s_dec(p, g.in_temp_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, g.in_rh_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.dht_in_ok, 0);
    p = s_put(p, ",");
    p = s_put(p, "\"");
    p = s_dhtpin(p, (uint8_t)(g_dht2_pin));
    p = s_put(p, "\"");
    p = s_put(p, "]");
    p = s_put(p, ",\"g\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.gas_pct_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.gas_do, 0);
    p = s_put(p, ",");
    p = s_dec(p, GAS_ENABLE, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.gas_ok, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"du\":");
    p = s_dec(p, (int32_t)g.dust_ug, 0);
    p = s_put(p, ",\"de\":");
    p = s_dec(p, DUST_ENABLE, 0);
    p = s_put(p, ",\"li\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.light_pct, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.light_adc, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.light_mv, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.light_ok, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_light_invert, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"gl\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g_gled_duty, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_glamp_mode, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_glamp_ahigh, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"tf\":");
    p = s_dec(p, g.tof_us, 0);
    p = s_put(p, ",\"dm\":");
    p = s_dec(p, (int32_t)g.dist_mm, 0);
    p = s_put(p, ",\"ac\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.v_meas, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.v_base, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)(g.r_dev * 1000.0f), 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.conf_x1000, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.main_anom, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"p\":");
    p = s_dec(p, (int32_t)g.pest, 0);
    p = s_put(p, ",\"al\":");
    p = s_dec(p, (int32_t)g.alarm, 0);
    p = s_put(p, ",\"lm\":");
    p = s_dec(p, (int32_t)g.lamp_on, 0);
    p = s_put(p, ",\"fa\":");
    p = s_dec(p, (int32_t)g.vent_auto, 0);
    p = s_put(p, ",\"vr\":");
    p = s_dec(p, (int32_t)g.vent_why, 0);
    p = s_put(p, ",\"fo\":");
    p = s_dec(p, (int32_t)g.fan_on, 0);
    p = s_put(p, ",\"f\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.fan[0], 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.fan[1], 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.fan[2], 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.fan[3], 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"sc\":");
    {   uint8_t _c = 0;
        for (j = 0; j < (uint8_t)DHT_CAND_N; j++) if (dht_res[j] == 0u) _c++;
        p = s_dec(p, (int32_t)_c, 0); }
    p = s_put(p, ",\"V\":");
    for (k = 0; k < 256u; k++) p = s_hex1(p, (g.vmap)[k]);
    p = s_put(p, ",\"ah\":");
    p = s_put(p, "[");
    {   uint16_t h[7];
        uint8_t  t;
        for (t = 0; t < 7u; t++) h[t] = 0;
        for (k = 0; k < (uint16_t)GRID_PIX; k++) { t = g.amap[k]; if (t < 7u) h[t]++; }
        for (t = 0; t < 7u; t++)
            { if (t) p = s_put(p, ","); p = s_dec(p, (int32_t)h[t], 0); } }
    p = s_put(p, "]");
    p = s_put(p, ",\"fl\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.flame_raw, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.flame, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_flame_alow, 0);
    p = s_put(p, ",");
    p = s_dec(p, FLAME_ENABLE, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"ws\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g.wind_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.wind_press_x10, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.wind_ok, 0);
    p = s_put(p, ",");
    p = s_dec(p, USE_WIND485, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g.wind_raw, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"rn\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g_rain_on, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_rain_raw, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_rain_alow, 0);
    p = s_put(p, ",");
    p = s_dec(p, RAIN_ENABLE, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"bd\":");
    p = s_put(p, "[");
    p = s_dec(p, (int32_t)g_bds_state, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_lat, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_lon, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_ns, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_ew, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_lines, 0);
    p = s_put(p, ",");
    p = s_dec(p, BDS_ENABLE, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_baud, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_edges, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_lo_min, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_hi_min, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_inv, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_baud_m, 0);
    p = s_put(p, ",");
    p = s_dec(p, (int32_t)g_bds_bytes, 0);
    p = s_put(p, "]");
    p = s_put(p, ",\"bl\":");
    p = s_put(p, "\"");
    p = s_put(p, g_bds_last);
    p = s_put(p, "\"");
    p = s_put(p, "}");
    return (uint16_t)(p - g_txbuf);
}

/* ==== PAGE_GEN_END ==== */

/* 判一段内存里有没有子串 —— 本工程不引 <string.h>, 一律用自带的字符串助手 */
static uint8_t mem_has(const char *s, uint16_t n, const char *pat)
{
    uint16_t i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; pat[j] != '\0'; j++) {
            if ((uint16_t)(i + j) >= n) return 0;
            if (s[i + j] != pat[j]) break;
        }
        if (pat[j] == '\0') return 1;
    }
    return 0;
}

/* 按 ESP_CHUNK 分片把一个内存块发给 ESP8266。
 * ★ s 允许是 Flash 里的常量: STM32 的 Flash 与 RAM 同处一个地址空间, 直接读即可。 */
static void esp_send_mem(const char *s, uint16_t n)
{
    uint16_t off = 0, k, i;
    while (off < n) {
        k = (uint16_t)(n - off);
        if (k > (uint16_t)ESP_CHUNK) k = (uint16_t)ESP_CHUNK;
        u3_print("AT+CIPSEND=0,");
        u3_int((int32_t)k);
        u3_print("\r\n");
        esp_wait(30);                    /* 等 '>' 提示符; 超时照发(模块不在也不卡死) */
        for (i = 0; i < k; i++) u3_putc(s[off + i]);
        esp_wait(60);                    /* 片间留点时间给模块 */
        off = (uint16_t)(off + k);
    }
}

/*==============================================================================
 *  十五-B、网页常量解压 (LZSS) —— 2026-10-04 为 64KB Flash 让路
 *----------------------------------------------------------------------------
 * 为什么有这一段:
 *   C8T6 只有 64KB Flash, 而整页 HTML 是烧进 Flash 的最大一块。页面涨到 38733
 *   字节(≈59%)之后, 再加一个功能就顶爆 —— 2026-10-04 加 485 风速页, 平台实测:
 *       ld: section `.rodata' will not fit in region `FLASH'
 *       ld: region `FLASH' overflowed by 5060 bytes
 *   砍页面内容会毁掉大屏观感, 所以改成 **压缩存放 + 发送时解压**:
 *       页面 38733 字节 --LZSS--> PAGE_LZ 22599 字节 (省 16134, 41.7%)
 *   代价: 2KB 环形窗 + 1KB 分片缓冲 (RAM) + 下面这几十行代码。
 *   ★ PAGE_LZ / PAGE_LZ_WIN / _LEN / _RAW / _XOR 这几个都由生成器写在
 *     PAGE_GEN 区里(在本节上面), **不要在这里再定义一遍**(会重定义报警告)。
 *
 * 格式 (与 `_lzss.py` 的 compress() 严格一一对应, `_check15.py` 会核对两边同源):
 *   每 8 个 token 前有 1 个 flags 字节, **LSB 优先**: 位=1 -> 匹配, 位=0 -> 字面量
 *   字面量: 1 字节
 *   匹配  : 3 字节 = (len-2), dist 高字节, dist 低字节   (len 3..257, dist 1..PAGE_LZ_WIN)
 *   编码器**只在需要时**才吐 flags 字节, 所以解码器"读完 PAGE_LZ_LEN 就停", 不会多读。
 *
 * ★★ PAGE_LZ 里**含 0x00 字节**(压缩数据里太正常了), 所以:
 *    长度**只能**用 PAGE_LZ_LEN; **绝对不要** sizeof()-1 / strlen / 任何字符串函数
 *    —— 它们会在第一个 NUL 处提前截断, 而且不报错。`_check15.py` 会扫一遍防呆。
 *
 * ★★ 为什么开机先自检:
 *    网页是"字符流", 解压差一个字节整页就废。自检**只解压不发送**, 比对解压长度与
 *    XOR 校验, 结论直接打 USART1 —— **不依赖 ESP、不依赖网络**, 仿真里一跑就知道。
 *    自检没过就不发整页, 改回一个明确的 500 文本, 免得浏览器收到半截 HTML,
 *    把人引到"网页怎么白了"这个错方向上去。串口按 `Z` 可随时重跑自检。
 *============================================================================*/
#define PAGE_LZ_MASK (PAGE_LZ_WIN - 1u)     /* PAGE_LZ_WIN 由生成器给出(2 的幂) */

static uint8_t  pg_hist[PAGE_LZ_WIN];   /* 环形窗: 最近 2KB 输出 (本节唯一的 RAM 开销) */
static char     pg_chunk[ESP_CHUNK];    /* 发送分片缓冲: 与 ESP 侧 1024 字节/片对齐 */
static uint32_t pg_pos;                 /* 已产出字节数 */
static uint16_t pg_xor;                 /* 产出字节的 XOR (自检用) */
static uint16_t pg_chunk_n;             /* pg_chunk 里已攒的字节数 */
static uint8_t  pg_sink;                /* 1 = 只自检不发送 */
static uint8_t  pg_ok;                  /* 自检结论: 1 = 解压链路可用 */

/* 解压链路坏了时的兜底应答: 明确报错, 而不是发半截 HTML 让人以为"网页白了"。 */
static const char PAGE_ERR[] =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n"
    "Content-Length: 39\r\n"
    "\r\n"
    "PAGE DECOMPRESS FAILED - see USART1 log";

/* 产出一个字节: 进环形窗 -> 累 XOR -> (非自检时)攒片, 攒满 ESP_CHUNK 就发一片。 */
static void pg_emit(uint8_t b)
{
    pg_hist[pg_pos & PAGE_LZ_MASK] = b;
    pg_pos++;
    pg_xor = (uint16_t)(pg_xor ^ (uint16_t)b);
    if (pg_sink) return;
    pg_chunk[pg_chunk_n] = (char)b;
    pg_chunk_n++;
    if (pg_chunk_n >= (uint16_t)ESP_CHUNK) {
        esp_send_mem(pg_chunk, pg_chunk_n);
        pg_chunk_n = 0u;
    }
}

/* 解压 PAGE_LZ (逐字节走 pg_emit)。返回 1 = 完整走到流尾, 0 = 格式错/被截断。 */
static uint8_t pg_decode(void)
{
    const uint8_t *src   = (const uint8_t *)PAGE_LZ;
    uint16_t       left  = (uint16_t)PAGE_LZ_LEN;
    uint8_t        flags = 0u;
    uint8_t        nbits = 0u;

    pg_pos     = 0u;
    pg_xor     = 0u;
    pg_chunk_n = 0u;

    while (left > 0u) {
        if (nbits == 0u) {                  /* 取一组 flags (LSB 先到) */
            flags = *src;
            src++;
            left--;
            /* 合法流里 flags 之后**必定**还有至少一个 token 字节(编码器就是这么吐的),
               所以这里能提前判掉"只有一个孤立 flags 的畸形流", 免得后面读到数组外面。 */
            if (left == 0u) return 0u;
            nbits = 8u;
        }
        if ((flags & 1u) != 0u) {           /* ---- 匹配: 从环形窗里回抄 ---- */
            uint16_t len, dist;
            if (left < 3u) return 0u;
            len = (uint16_t)((uint16_t)(*src) + 2u);
            src++;
            dist = (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
            src += 2;
            left = (uint16_t)(left - 3u);
            if (dist == 0u || dist > PAGE_LZ_WIN) return 0u;
            while (len > 0u) {
                pg_emit(pg_hist[(pg_pos - (uint32_t)dist) & PAGE_LZ_MASK]);
                len--;
            }
        } else {                            /* ---- 字面量 ---- */
            pg_emit(*src);
            src++;
            left--;
        }
        flags = (uint8_t)(flags >> 1);
        nbits--;
    }
    return 1u;
}

/* 把整页发给 ESP。返回 1 = 发全了; 0 = 自检没过, 已降级成明确的 500 文本。 */
static uint8_t pg_serve_page(void)
{
    if (pg_ok == 0u) {
        esp_send_mem(PAGE_ERR, (uint16_t)(sizeof(PAGE_ERR) - 1u));
        return 0u;
    }
    esp_send_mem(PAGE_HTML_HDR, (uint16_t)(sizeof(PAGE_HTML_HDR) - 1u));
    if (pg_decode() == 0u) {                /* 开机自检过了, 理论上到不了这里 */
        u1_print("[LZ] !! decode failed at send time -- page NOT sent\r\n");
        return 0u;
    }
    if (pg_chunk_n > 0u) {                  /* 最后不满一片的尾巴 */
        esp_send_mem(pg_chunk, pg_chunk_n);
        pg_chunk_n = 0u;
    }
    return 1u;
}

/* 自检: 只解压不发送, 比对解压长度与 XOR。结论打 USART1 —— 不依赖 ESP/网络。 */
static void pg_selftest(void)
{
    uint8_t ok;

    pg_sink = 1u;
    ok = pg_decode();
    pg_sink = 0u;

    pg_ok = (uint8_t)((ok != 0u)
                      && (pg_pos == (uint32_t)PAGE_LZ_RAW)
                      && (pg_xor == (uint16_t)PAGE_LZ_XOR));
    if (pg_ok != 0u) {
        u1_print("[LZ] page decompress self-test OK: ");
        u1_print_int((int32_t)pg_pos);
        u1_print(" bytes, xor=");
        u1_hex2((uint8_t)(pg_xor >> 8));
        u1_hex2((uint8_t)(pg_xor & 0xFFu));
        u1_print("  (Flash 里只存了 ");
        u1_print_int((int32_t)PAGE_LZ_LEN);
        u1_print(" 字节)\r\n");
    } else {
        u1_print("[LZ] !! page decompress self-test FAILED -- 网页不会发出去!\r\n");
        u1_print("[LZ]    got  ");
        u1_print_int((int32_t)pg_pos);
        u1_print(" bytes, xor=");
        u1_hex2((uint8_t)(pg_xor >> 8));
        u1_hex2((uint8_t)(pg_xor & 0xFFu));
        u1_print("\r\n[LZ]    want ");
        u1_print_int((int32_t)PAGE_LZ_RAW);
        u1_print(" bytes, xor=");
        u1_hex2((uint8_t)(PAGE_LZ_XOR >> 8));
        u1_hex2((uint8_t)(PAGE_LZ_XOR & 0xFFu));
        u1_print("\r\n");
    }
}

/* 把数据 JSON 打到 USART1 —— 这是"仿真里看真实数据"的通路:
 *     固件 → 平台 uart_monitor → 页面注入的 JS → 本机 _live.py → 3D 大屏
 * ★ 首尾的 ===== 标记是给本机解析定位用的, 改标记要同步改 _live.py 里的 MARK。
 * ★ 数据与走 ESP 那条路(esp_serve 的 /d 应答)是**同一份 esp_build_data()**,
 *   所以本机大屏看到的数值和真机浏览器上看到的完全一致, 不存在两套口径。 */
static void u1_json_report(void)
{
    uint16_t n = esp_build_data();       /* 组包进 g_txbuf */
    uint16_t i;

    u1_print("\r\n===== JSON BEGIN (");
    u1_print_int((int32_t)n);
    u1_print(" bytes) =====\r\n");
    for (i = 0; i < n; i++) u1_putc(g_txbuf[i]);
    u1_print("\r\n===== JSON END =====\r\n");
}

/* 应答一次浏览器请求: 命中 " /d" 给数据 JSON, 其余一律给整页 */
static void esp_serve(void)
{
    uint16_t n = 0;
    char    *p;

    g_req_data = (uint8_t)(mem_has(g_req, g_req_n, " /d") ? 1u : 0u);
    g_req_n    = 0;                      /* 判别完就复位, 免得下一拍重复命中 */
    g_req[0]   = '\0';

    if (g_req_data) {
        /* ---- 数据端点: 头 + 一小段 JSON ---- */
        n = esp_build_data();            /* 体在 g_txbuf[0..n) */
        p = g_hdr;
        p = s_put(p, "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Connection: close\r\nCache-Control: no-store\r\n"
                     "Access-Control-Allow-Origin: *\r\nContent-Length: ");
        p = s_dec(p, (int32_t)n, 0);
        p = s_put(p, "\r\n\r\n");
        esp_send_mem(g_hdr, (uint16_t)(p - g_hdr));
        esp_send_mem(g_txbuf, n);
    } else {
        /* ---- 整页: 头(Content-Length = **未压缩**长度) + 边解压边发 PAGE_LZ ----
         * ★ 浏览器收到的是解压后的字节, 所以 Content-Length 仍是原始页面长度;
         *   解压/分片/自检都在十五-B 节的 pg_serve_page() 里。 */
        (void)pg_serve_page();
    }

    u3_print("AT+CIPCLOSE=0\r\n");
    esp_wait(100);

    /* 把本次应答期间客户端又发来的字节丢掉, 免得它们凑够长度又触发一次应答 */
    while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET) (void)USART_ReceiveData(USART3);

#if ESP_HTML_ECHO
    /* 仿真平台没有能渲染网页的浏览器: 把**数据 JSON** 回吐到 USART1 ——
     * 在 uart_monitor 的接收列表里就能实时看到各传感器的值在跳。
     * (整页 HTML 有两万字节, 倒进串口监视器没意义; 要看整页请用
     *  `python _page_tool.py --preview -o 网页预览.html` 生成后双击打开。) */
    if (g_req_data) u1_json_report();
#endif
}
/*==============================================================================
 *                          十四、上行数据帧
 *============================================================================*/

static void send_vmap(void)
{
    pkt_send(PKT_VMAP, 0, 2, &g.vmap[0],   128);
    pkt_send(PKT_VMAP, 1, 2, &g.vmap[128], 128);
}

static void send_amap(void)
{
    pkt_send(PKT_AMAP, 0, 2, &g.amap[0],   128);
    pkt_send(PKT_AMAP, 1, 2, &g.amap[128], 128);
}

static void send_summary(uint32_t elapsed_ms)
{
    uint8_t d[7];
    d[0] = g.main_anom;
    d[1] = (uint8_t)(g.conf_x1000 >> 8);
    d[2] = (uint8_t)(g.conf_x1000 & 0xFF);
    d[3] = (uint8_t)((elapsed_ms >> 24) & 0xFF);
    d[4] = (uint8_t)((elapsed_ms >> 16) & 0xFF);
    d[5] = (uint8_t)((elapsed_ms >> 8) & 0xFF);
    d[6] = (uint8_t)(elapsed_ms & 0xFF);
    pkt_send(PKT_SUMMARY, 0, 1, d, 7);
}

static void send_level(void)
{
    uint8_t d[4];
    d[0] = (uint8_t)(g.dist_mm >> 8);
    d[1] = (uint8_t)(g.dist_mm & 0xFF);
    d[2] = (uint8_t)(300 >> 8);           /* 仓体高度 300mm */
    d[3] = (uint8_t)(300 & 0xFF);
    pkt_send(PKT_LEVEL, 0, 1, d, 4);
}

static void send_env(void)
{
    uint8_t d[8];
#if DUST_ENABLE
    d[0] = (uint8_t)(g.dust_ug >> 8);       /* 借用 lux 字段传粉尘浓度 */
    d[1] = (uint8_t)(g.dust_ug & 0xFF);
#else
    d[0] = 0; d[1] = 0;                     /* 粉尘作废: 不编数据, 传 0 由上位机按无效处理 */
#endif
    d[2] = g.fan[0];                        /* FAN1 转速 % (PA15/TIM2_CH1) */
    d[3] = g.fan[1];                        /* FAN2 转速 % (PB3 /TIM2_CH2) */
    d[4] = g.fan[2];                        /* FAN3 转速 % (PB4 /TIM3_CH1) */
    d[5] = g.fan[3];                        /* FAN4 转速 % (PB5 /TIM3_CH2) */
    d[6] = g.lamp_on;                       /* 灯带状态 → 诱虫灯 */
    d[7] = g.pest;                          /* 扩展: 害虫等级 */
    pkt_send(PKT_ENV, 0, 1, d, 8);
}

static void send_vent(void)
{
    uint8_t   d[16];
    int16_t   to = g.temp_x10;              /* 舱外 */
    uint16_t  ho = (uint16_t)g.rh_x10;
    int16_t   ti = g.in_temp_x10;           /* 仓内 */
    uint16_t  hi = (uint16_t)g.in_rh_x10;
    uint8_t   i;

    for (i = 0; i < 16; i++) d[i] = 0;

    d[0] = (uint8_t)((to >> 8) & 0xFF);   d[1] = (uint8_t)(to & 0xFF);   /* 舱外温度 */
    d[2] = (uint8_t)((ho >> 8) & 0xFF);   d[3] = (uint8_t)(ho & 0xFF);   /* 舱外湿度 */
    d[4] = (uint8_t)((ti >> 8) & 0xFF);   d[5] = (uint8_t)(ti & 0xFF);   /* 仓内温度 */
    d[6] = (uint8_t)((hi >> 8) & 0xFF);   d[7] = (uint8_t)(hi & 0xFF);   /* 仓内湿度 */
    d[8] = 0xFF; d[9] = 0xFF;                              /* 气压: 无效(本设计无此传感器) */
    d[10] = g.fan_pct;                                     /* 通风总强度 % */
    d[11] = g.vent_why;                                    /* 原因 1温差 2湿差 3仓内超标 4手动 */
    d[13] = (uint8_t)((g.dht_ok    ? 0x01u : 0u) |         /* bit0 = 舱外温湿度有效 */
                      (g.dht_in_ok ? 0x02u : 0u));         /* bit1 = 仓内温湿度有效 */
    d[15] = g.vent_auto;
    pkt_send(PKT_VENT, 0, 1, d, 16);
}

/* 害虫识别结果转发 (等级 → 虫声特征合成量) */
static void send_sound(void)
{
    uint8_t d[13];
    uint16_t rms  = (uint16_t)(200u + (uint16_t)g.pest * 800u);
    uint16_t peak = (uint16_t)(400u + (uint16_t)g.pest * 1500u);
    uint16_t freq = (uint16_t)(3000u + (uint16_t)g.pest * 1200u);

    d[0] = (uint8_t)(rms >> 8);  d[1] = (uint8_t)(rms & 0xFF);
    d[2] = (uint8_t)(peak >> 8); d[3] = (uint8_t)(peak & 0xFF);
    d[4] = (uint8_t)(freq >> 8); d[5] = (uint8_t)(freq & 0xFF);
    d[6] = d[0]; d[7] = d[1];
    d[8] = d[2]; d[9] = d[3];
    d[10] = d[4]; d[11] = d[5];
    d[12] = g.pest;
    pkt_send(PKT_SOUND, 0, 1, d, 13);
}

#if USE_ASCII_LOG
static void send_ascii_log(void)
{
    u1_print("[GT] OUT ");
    if (g.dht_ok) {
        u1_print_int(g.temp_x10 / 10); u1_print("."); u1_print_int(g.temp_x10 % 10);
        u1_print("C RH=");
        u1_print_int(g.rh_x10 / 10);   u1_print("."); u1_print_int(g.rh_x10 % 10);
        u1_print("%");
    } else {
        u1_print("--.-C RH=--.-% (DHT11 ERR)");   /* 如实报错, 不编数字 */
    }
    u1_print(" IN ");
    if (g.dht_in_ok) {
        u1_print_int(g.in_temp_x10 / 10); u1_print("."); u1_print_int(g.in_temp_x10 % 10);
        u1_print("C RH=");
        u1_print_int(g.in_rh_x10 / 10);   u1_print("."); u1_print_int(g.in_rh_x10 % 10);
        u1_print("%");
    } else {
        u1_print("--.-C RH=--.-% (DHT11 ERR)");
    }
    u1_print(" FAN=");
    u1_print_int(g.fan[0]); u1_print("/");
    u1_print_int(g.fan[1]); u1_print("/");
    u1_print_int(g.fan[2]); u1_print("/");
    u1_print_int(g.fan[3]);
    u1_print(" TOF=");
    u1_print_int(g.tof_us);
    u1_print("us V=");
    u1_print_int((int32_t)(g.v_meas * 10.0f));
    u1_print(" DUST=");
#if DUST_ENABLE
    u1_print_int(g.dust_ug);
#else
    u1_print("--");                    /* 粉尘已停用: 如实报 -- */
#endif
#if GAS_ENABLE
    u1_print(" GAS=");
    /* ★ 与光照同一口径: 读不到就报 --, 不拿 0.0 冒充"空气干净"。
     *   现场排障时 "GAS=--" 的意思很明确: 这一路没读到, 去查 AO 有没有接到 PA6。 */
    if (g.gas_ok) u1_print_int(g.gas_pct_x10 / 10);
    else          u1_print("--");
    u1_print(" graw="); u1_print_int(g.gas_adc);
    u1_print(" glo=");  u1_print_int(g_gas_lo);
    u1_print(" ghi=");  u1_print_int(g_gas_hi);
#else
    u1_print(" GAS=--");              /* 整路作废: 如实报 -- */
#endif
    /* 光照: 百分比之外**连 raw 与 mV 一起打** —— 这是现场定 LIGHT_INVERT 方向
       的唯一依据(拖光敏元件上的滑条, 看 raw 往哪边走)。 */
    u1_print(" LGT=");
    if (g.light_ok) { u1_print_int(g.light_pct); u1_print("%"); }
    else            { u1_print("--"); }
    u1_print(" raw="); u1_print_int(g.light_adc);
    u1_print(" lo=");  u1_print_int(g_light_lo);
    u1_print(" hi=");  u1_print_int(g_light_hi);
    u1_print(" mv=");  u1_print_int(g.light_mv);
    u1_print(" GLP="); u1_print_int(g.glamp_on);
    /* ★ 把"为什么会亮"直接打出来 —— 现在是**调光**, 所以给的是占空比和它的来由:
     *   GLP=1 duty=70% (auto)    自动: 环境光 30% → 补光 70%(越暗越亮)
     *   GLP=1 duty=100% (manual) 手动强制全亮; 想交回自动发 C5 B7
     *   GLP=0 duty=0 (noData)    光照读不到 → **不动**, 既不猜天黑也不猜天亮
     *   duty 与 LGT 相加应为 100 —— 这就是"光敏控制 LED 亮度"的算式本身。 */
    u1_print(" duty="); u1_print_int(g_gled_duty); u1_print("%");
    if (g_glamp_mode)                     u1_print(" (manual)");
    else if (!g.light_ok)                 u1_print(" (noData:blink)");   /* 灯在慢闪提示 */
    else                                  u1_print(" (auto)");
    u1_print(" inv="); u1_print_int(g_light_invert);
    u1_print(" pol="); u1_print_int(g_glamp_ahigh);
    u1_print(" FLAME=");
#if FLAME_ENABLE
    /* ★ 判定与**原始电平**一起打 —— 现场判据就这一条:
     *     FLAME=1 raw=0  → 判为有火(默认极性: 低电平=有火)
     *     FLAME=0 raw=1  → 无火
     *   拿火源靠近/移开, raw 若始终不变 ⇒ 线没接上、或极性与实际相反(发 'F' 翻)。 */
    u1_print_int(g.flame);
    u1_print(" raw=");
    u1_print_int(g.flame_raw);
    u1_print(" pol=");
    u1_print_int(g_flame_alow);
#else
    u1_print("--");                      /* 整路作废: 如实报 -- */
#endif
    u1_print(" WIND=");
#if USE_WIND485
    /* ★ 与温度/气体同一口径: 读不到就报 --, 绝不拿 0 m/s 冒充"无风"。
     *   现场判据: WIND=-- 说明 USART2 这一路没有应答 —— 先查 485 的 A/B 两根线
     *   (接反的现象就是永远无应答, 对调即可) 与波特率; 发 'V' 能看到逐字节收发。 */
    if (g.wind_ok) {
        u1_print_int(g.wind_x10 / 10);  u1_print(".");
        u1_print_int(g.wind_x10 % 10);  u1_print("m/s P=");
        u1_print_int(g.wind_press_x10 / 10); u1_print(".");
        u1_print_int(g.wind_press_x10 % 10); u1_print("Pa raw=");
        u1_print_int((int32_t)g.wind_raw);
    } else {
        u1_print("--");
    }
#else
    u1_print("--");                    /* 整路作废: 如实报 -- */
#endif
    u1_print(" PEST=");
    u1_print_int(g.pest);
    u1_print(" ALARM=");
    u1_print_int(g.alarm);
    u1_print(" ANOM=");
    u1_print_int(g.main_anom);
    u1_print("\r\n");
}
#endif

/*==============================================================================
 *                          十五、下行命令
 *============================================================================*/

static uint8_t handle_cmd(uint8_t cmd)
{
    switch (cmd) {
        case CMD_LED_ON:   LAMP_ON();  g.lamp_on = 1; beep_n(1, 40); break;
        case CMD_LED_OFF:  LAMP_OFF(); g.lamp_on = 0; beep_n(1, 40); break;
        case CMD_BEEP:     beep_n(3, 80); break;
        case CMD_FAN_ON:   g.vent_auto = 0; fan_all_set(60); beep_n(1, 40); break;
        case CMD_FAN_OFF:  g.vent_auto = 0; fan_all_set(0);  beep_n(1, 40); break;
        case CMD_VENT_AUTO:   g.vent_auto = 1; beep_n(2, 60); break;
        case CMD_VENT_MANUAL: g.vent_auto = 0; beep_n(3, 60); break;
        case CMD_PEST_0:   g.pest = 0; beep_n(1, 40); break;
        case CMD_PEST_1:   g.pest = 1; beep_n(2, 40); break;
        case CMD_PEST_2:   g.pest = 2; beep_n(3, 60); break;
        case CMD_CAL:      g.v_base = 0.0f; beep_n(1, 60); break;   /* 重新标定 */
        case CMD_GLAMP_ON:
            g_glamp_mode = 1; g_glamp_force = 1; g_glamp_hold = millis();
            u1_print("[GLP] FORCE 100% for 5s -> then auto\r\n");
            beep_n(1, 40); break;
        case CMD_GLAMP_OFF:
            g_glamp_mode = 1; g_glamp_force = 0; g_glamp_hold = millis();
            u1_print("[GLP] FORCE 0% for 5s -> then auto\r\n");
            beep_n(1, 40); break;
        case CMD_GLAMP_AUTO:
            g_glamp_mode = 0;
            u1_print("[GLP] back to auto (duty = 100 - light)\r\n");
            beep_n(2, 40); break;
        case CMD_LIGHT_INV:
            g_light_invert = (uint8_t)(!g_light_invert);
            u1_print("[GT] LIGHT_INVERT=");
            u1_print_int(g_light_invert);
            u1_print(g_light_invert ? " (volt_hi_is_DARK)\r\n" : " (volt_hi_is_BRIGHT)\r\n");
            beep_n(1, 40);
            break;
        case CMD_GLAMP_POL:
            g_glamp_ahigh = (uint8_t)(!g_glamp_ahigh);
            u1_print("[GT] GLAMP_ACTIVE_HIGH=");
            u1_print_int(g_glamp_ahigh);
            u1_print(g_glamp_ahigh ? " (HIGH=lit)\r\n" : " (LOW=lit)\r\n");
            beep_n(1, 40);
            break;
        case CMD_FLAME_POL:
            g_flame_alow = (uint8_t)(!g_flame_alow);
            u1_print("[GT] FLAME_ACTIVE_LOW=");
            u1_print_int(g_flame_alow);
            u1_print(g_flame_alow ? " (LOW=fire)\r\n" : " (HIGH=fire)\r\n");
            beep_n(1, 40);
            break;
        default:           return ACK_UNKNOWN_CMD;
    }
    return ACK_OK;
}

static uint8_t g_rx_state = 0;

static void rx_poll(void)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART1);
        uint8_t ack[2];

        if (b == '0' || b == '1' || b == '2') {   /* 虚拟终端 ASCII 快捷注入 */
            g.pest = (uint8_t)(b - '0');
            beep_n((uint8_t)(g.pest + 1), 40);
            continue;
        }
        if (b == 'I' || b == 'i') {   /* 现场翻光照方向 (等价 0xC5 0xB8) —— 排障用 */
            (void)handle_cmd(CMD_LIGHT_INV);
            continue;
        }
        if (b == 'P' || b == 'p') {   /* 现场翻 LED 极性 (等价 0xC5 0xB9) —— 排障用 */
            (void)handle_cmd(CMD_GLAMP_POL);
            continue;
        }
        if (b == 'F' || b == 'f') {   /* 现场翻火焰传感器 DO 极性 (等价 0xC5 0xBA) —— 排障用 */
            (void)handle_cmd(CMD_FLAME_POL);
            continue;
        }
#if USE_WIND485
        if (b == 'V' || b == 'v') {   /* 485 风速排障: 立刻读一次 + 翻转"逐字节 hex 打印"开关。
                                       * ★ 默认只在"成败翻转"时打一行, 所以第一次按 'V'
                                       *   必定能看到完整的 TX/RX 十六进制 —— 线上到底有没有
                                       *   数据、回的是什么, 一眼就有答案。 */
            g_wind_verbose = (uint8_t)(!g_wind_verbose);
            u1_print("[WND] verbose=");
            u1_print_int(g_wind_verbose);
            u1_print(g_wind_verbose ? " (每次轮询都打 TX/RX)\r\n"
                                    : " (仅在成败翻转时打)\r\n");
            g_wind_lastok = 0xFFu;    /* 强制这一次至少打一遍 */
            wind_poll();
            continue;
        }
#endif
        /* ★ 2026-10-02: 加三个字符键 —— 现场排查"灯不亮"时**不用记十六进制命令码**:
         *     'L' 强制点亮 5 秒  → 灯亮 = 驱动和灯都是好的, 问题在光照数据/调光逻辑
         *                          灯不亮 = 问题在灯这一侧(10K 太大 / 元件不对 / 极性)
         *     'K' 强制熄灭 5 秒  → 用来验证"该灭的时候真能灭"
         *     'A' 交回自动       → 立刻恢复 duty = 100 − 光照
         *   5 秒后自动回 AUTO, 不用记得再按一次。 */
        if (b == 'L' || b == 'l') { (void)handle_cmd(CMD_GLAMP_ON);   continue; }
        if (b == 'K' || b == 'k') { (void)handle_cmd(CMD_GLAMP_OFF);  continue; }
        if (b == 'A' || b == 'a') { (void)handle_cmd(CMD_GLAMP_AUTO); continue; }
        if (b == 'Z' || b == 'z') {   /* 现场重跑"网页解压自检" —— 网页打不开时先按它:
                                       * 串口会直接报解压长度与 XOR 对不对, 不用连 ESP。
                                       * ★ 用 'Z' 是因为 'L'/'K'/'A' 已被补光灯占用。 */
            u1_print("[LZ] rerun page decompress self-test\r\n");
            pg_selftest();
            continue;
        }
#if RAIN_ENABLE
        if (b == 'R') {   /* 现场翻"雨量 DO 极性" —— ★ 大写 R: 小写 r 已被光照标定(hi)占了 */
            g_rain_alow = (uint8_t)(!g_rain_alow);
            rain_read();
            u1_print("[RAIN] active_low=");
            u1_print_int(g_rain_alow);
            u1_print(" raw=");
            u1_print_int(g_rain_raw);
            u1_print(" rain=");
            u1_print_int(g_rain_on);
            u1_print("\r\n");
            continue;
        }
#endif
#if BDS_ENABLE
        if (b == 'B' || b == 'b') {   /* 北斗软串口波特率循环: 4800/9600/19200/115200 */
            static const uint32_t bd_baud[4] = {4800u, 9600u, 19200u, 115200u};
            switch (g_bds_baud) {
                case 4800u:   g_bds_baud = 9600u;   break;
                case 9600u:   g_bds_baud = 19200u;  break;
                case 19200u:  g_bds_baud = 115200u; break;
                default:      g_bds_baud = 4800u;   break;
            }
            (void)bd_baud;                       /* 表只用于文档化这四档 */
            g_bds_lines = 0u; g_bds_badsum = 0u; g_bds_state = 0u;
            g_bds_manual = 1u;                   /* 人定了档, 体检不再自动改回去 */
            u1_print("[BDS] baud = ");
            u1_print_int((int32_t)g_bds_baud);
            u1_print(" (send 'N' to dump last NMEA line)\r\n");
            continue;
        }
        if (b == 'Y') {   /* 重测线路: 清掉旧统计, 重新体检(改完接线后按) */
            g_bds_edges = 0u; g_bds_bytes = 0u;
            g_bds_lo_min = 0u; g_bds_hi_min = 0u;
            g_bds_hi_n = 0u; g_bds_all_n = 0u; g_bds_baud_m = 0u;
            g_bds_manual = 0u; g_bds_pb_ms = 0u;
            u1_print("[BDS] line check restarted -- watch the web page or send 'N'\r\n");
            continue;
        }
        if (b == 'N') {   /* 北斗排障: 线路体检 + 最近一行原文一起打(线上到底有没有东西) */
            u1_print("[BDS] state=");
            u1_print_int((int32_t)g_bds_state);
            u1_print(" lines=");
            u1_print_int((int32_t)g_bds_lines);
            u1_print(" badsum=");
            u1_print_int((int32_t)g_bds_badsum);
            u1_print(" bytes=");
            u1_print_int((int32_t)g_bds_bytes);
            u1_print(" baud=");
            u1_print_int((int32_t)g_bds_baud);
            u1_print("\r\n[BDS] line: edges=");
            u1_print_int((int32_t)g_bds_edges);
            u1_print(" lo_min=");
            u1_print_int((int32_t)g_bds_lo_min);
            u1_print("us hi_min=");
            u1_print_int((int32_t)g_bds_hi_min);
            u1_print("us inv=");
            u1_print_int((int32_t)g_bds_inv);
            u1_print(" baud_meas=");
            u1_print_int((int32_t)g_bds_baud_m);
            u1_print("\r\n[BDS] last: ");
            u1_print(g_bds_last[0] ? g_bds_last : "(none)");
            u1_print("\r\n");
            continue;
        }
#endif
#if USE_ESP8266
        /* ★★ 模拟量两点定标 (2026-10-04): 把"当前这一拍读到的 raw"锁成端点。
         *   光敏 q=暗端(0%) r=亮端(100%);  MQ-135 s=零端(0‰) t=满端(100‰)。
         *   现场: 滑条拖到一端 → 按键 → 拖到另一端 → 再按键; 之后读数才准。
         *   为什么不写死: 每个元件满量程不同(MQ-135 实测 1287), 写死 4095 必错。 */
        if (b == 'q') { g_light_lo = g.light_adc; u1_print("[CAL] LGT lo(raw)="); u1_print_int(g_light_lo); u1_print("\r\n"); continue; }
        if (b == 'r') { g_light_hi = g.light_adc; u1_print("[CAL] LGT hi(raw)="); u1_print_int(g_light_hi); u1_print("\r\n"); continue; }
        if (b == 's') { g_gas_lo   = g.gas_adc;   u1_print("[CAL] GAS lo(raw)="); u1_print_int(g_gas_lo);   u1_print("\r\n"); continue; }
        if (b == 't') { g_gas_hi   = g.gas_adc;   u1_print("[CAL] GAS hi(raw)="); u1_print_int(g_gas_hi);   u1_print("\r\n"); continue; }
        if (b == 'W' || b == 'w') {               /* 手动取一次数据 JSON (走 USART1, 不依赖 ESP) */
            u1_print("[GT] 'W' -> report data JSON now (uart_monitor 接收区可见)\r\n");
            u1_json_report();
            g_http_req = 1;                       /* 照旧再模拟一次网页访问(走 ESP 那边, 不影响本口) */
            g_http_t   = 0;                       /* 取消去抖, 本拍立即组包 */
            continue;
        }
#endif
        if (g_rx_state == 0) {
            if (b == CMD_PREFIX) g_rx_state = 1;
        } else {
            g_rx_state = 0;
            ack[0] = b;
            ack[1] = handle_cmd(b);
            pkt_send(PKT_ACK, 0, 1, ack, 2);
        }
    }
}

/*==============================================================================
 *                          十六、主程序
 *============================================================================*/

#if LAMP_KIND == 0
/* 开机时序扫描自检: 每轮按 SK_TRY 里那套时序/颜色发一帧, 轮次打在 OLED 与串口上。
 * 判读(看灯珠, 不是看屏):
 *   亮红       -> 这套能用了: 第 1~3/6 轮亮红 = 平台认这套时序;
 *                 第 4/6 轮亮红 = **模块内部把数据反了相**, 正常工作得发反相色;
 *                 第 5/6 轮亮红 = 反相 + 只能吃 µs 级位宽;
 *   纯白       -> 平台把 32 位全判成 1(两个码没分开), 换下一号;
 *   一直是黑罩 -> 平台没解析出颜色(整帧被丢弃), 换下一号;
 *   其它颜色   -> 能解码但分量次序不同(GRB 之类) —— 那是 sk_show() 里 4 次
 *                 sk_send_byte() 的次序问题, 时序不用动。
 * 全部轮次结束后按 SK_TM_PICK 选定正常工作用的时序。
 * (本函数要调 OLED 与串口, 所以放在十几节之后、main() 之前) */
static void sk_lamp_sweep(void)
{
    uint8_t k;

    if (!SK6812_ENABLE || !g_sk_ok) return;

    for (k = 0; k < SK_TRY_N; k++) {
        sk_tm = &SK_TRY[k].tm;

        u1_print("[GT] lamp sweep ");
        u1_print(SK_TRY[k].label);
        u1_print(" t0h=");   u1_print_int((int32_t)SK_TRY[k].tm.t0h);
        u1_print(" t1h=");   u1_print_int((int32_t)SK_TRY[k].tm.t1h);
        u1_print(" bit=");   u1_print_int((int32_t)SK_TRY[k].tm.bit);
        u1_print(SK_TRY[k].tm.use72m ? " @72MHz" : " @1MHz");
        u1_print(" send=");  u1_print_int((int32_t)SK_TRY[k].r);
        u1_print(",");       u1_print_int((int32_t)SK_TRY[k].g);
        u1_print(",");       u1_print_int((int32_t)SK_TRY[k].b);
        u1_print("\r\n");

        oled_clear();
        oled_header(0, "LAMP SWEEP", "RED");
        oled_str(0, UI_L2, SK_TRY[k].label, 0);
        oled_str(0, UI_L3, "watch the lamp", 0);
        oled_flush();

        sk_set(SK_TRY[k].r, SK_TRY[k].g, SK_TRY[k].b);
        ms_dly(500);
        sk_set(0u, 0u, 0u);             /* 熄灭, 便于分辨轮次 */
        ms_dly(250);
    }

    k = (uint8_t)((SK_TM_PICK >= 1u && SK_TM_PICK <= SK_TRY_N) ? (SK_TM_PICK - 1u) : 0u);
    sk_tm = &SK_TRY[k].tm;
    u1_print("[GT] lamp sweep done, normal timing = ");
    u1_print(SK_TRY[k].label);
    u1_print("\r\n");
}

#else   /* ---------------- LAMP_KIND == 1: 报警灯的极性测试 ---------------- */

/* 开机"极性测试": 报警灯只有亮/灭两态, 到底是**高电平点亮还是低电平点亮**, 一测便知。
 * 做法: 先用**低电平**驱动 2 秒, 再用**高电平**驱动 2 秒; 轮次同时打在 OLED 与串口上
 * (灯要靠眼睛看, 屏幕只负责告诉你"现在是第几轮")。
 * 判读:
 *   第 1 轮亮("1/2 LOW ") -> ALARM_ACTIVE_LOW 保持 1 —— 5V 在另一端, 拉低才有电流;
 *   第 2 轮亮("2/2 HIGH") -> 把 ALARM_ACTIVE_LOW 改成 0;
 *   两轮都不亮            -> 平台一个 status 都没下发(前端连亮灯贴图都不画), 先查接线:
 *                            信号线是否落在模块引线的外端、另一端是否接 5V(或 3.3V,
 *                            模块供电 3.0~5.0V 都行)电源符号上。
 * 用 ms_dly 而不是 delay_ms —— 平台的 delay_ms 是桩, 会一闪而过根本看不见(第四节)。 */
static void alarm_probe(void)
{
    if (!ALARM_ENABLE) return;

    u1_print("[ALM] probe 1/2: drive LOW  2s\r\n");
    oled_clear();
    oled_header(0, "ALARM LAMP", "1/2 LOW");
    oled_str(0, UI_L2, "drive LOW 2s", 0);
    oled_str(0, UI_L3, "lit? -> low=1", 0);
    oled_flush();
    ALARM_PORT->BRR = ALARM_PIN;              /* 低电平 */
    ms_dly(2000);

    u1_print("[ALM] probe 2/2: drive HIGH 2s\r\n");
    oled_clear();
    oled_header(0, "ALARM LAMP", "2/2 HIGH");
    oled_str(0, UI_L2, "drive HIGH 2s", 0);
    oled_str(0, UI_L3, "lit? -> low=0", 0);
    oled_flush();
    ALARM_PORT->BSRR = ALARM_PIN;             /* 高电平 */
    ms_dly(2000);

    ALARM_SET_OFF();
    u1_print("[ALM] probe done. ALARM_ACTIVE_LOW=");
    u1_print_int(ALARM_ACTIVE_LOW);
    u1_print("\r\n");
}
#endif  /* LAMP_KIND */

/* =============================================================================
 *  补光 LED 开机自检 (PA5, 软件 PWM)
 *  ★ 与报警灯不同: 这盏灯的电路是**明确的** —— 阴极接地、阳极串 10K 电阻接 PA5,
 *    所以只有 PA5 输出高电平才有电流 ⇒ **高电平点亮**, 不需要再做极性扫描。
 *    这里只做一件事: **确认灯能亮、也能灭**, 顺便把"不该亮的时候亮"暴露出来。
 *     第 1 段("1/2 ON ") 常亮(占空比 100%) 2 秒
 *     第 2 段("2/2 OFF") 全灭(占空比 0%)   1 秒
 *  判读:
 *     第 1 段不亮   -> 大概率是**限流电阻太大**: 3.3V / 10K ≈ 0.16mA, 未必够驱动
 *                      仿真里的 LED。建议换成 **330Ω~1K**(亮度也会明显些)。
 *                      其次查阳极/阴极有没有接反(阴极必须接地那一侧)。
 *     第 2 段还亮   -> 极性反了, 串口发 **P** 翻一下(等价 0xC5 0xB9)。
 *  用 ms_dly 而不是 delay_ms —— 平台的 delay_ms 是桩, 会一闪而过根本看不见(第四节)。
 * ========================================================================== */
static void glamp_probe(void)
{
    if (!GLAMP_ENABLE) return;

    u1_print("[GLP] LED probe 1/2: ON  (duty 100%) 2s\r\n");
    oled_clear();
    oled_header(0, "GROW LED", "1/2 ON");
    oled_str(0, UI_L2, "LED ON 2s", 0);
    oled_str(0, UI_L3, "dark? R too big", 0);
    oled_flush();
    glamp_drive(1);                           /* 常亮 */
    ms_dly(2000);

    u1_print("[GLP] LED probe 2/2: OFF (duty 0%) 1s\r\n");
    oled_clear();
    oled_header(0, "GROW LED", "2/2 OFF");
    oled_str(0, UI_L2, "LED OFF 1s", 0);
    oled_str(0, UI_L3, "lit? -> send P", 0);
    oled_flush();
    glamp_drive(0);                           /* 灭 */
    ms_dly(1000);

    g_gled_duty = 0;
    u1_print("[GLP] probe done. GLAMP_ACTIVE_HIGH=");
    u1_print_int(GLAMP_ACTIVE_HIGH);
    u1_print(" (now ");
    u1_print_int(g_glamp_ahigh);
    u1_print(")  flip: send 'P'\r\n");
}

int main(void)
{
    uint32_t t_dht = 0, t_us = 0, t_tomo = 0, t_ui = 0, t_log = 0, t_rep = 0, t_wind = 0;

    /* 1. 先点亮 OLED —— 放最前, 后续任何外设初始化若卡死, 屏上能看出来 */
    GPIO_AllInit();
    TimeBase_Init();             /* 时间基准要先有 (I2C 超时保护也要用) */
    us_dly_init();               /* 独立微秒时基(TIM1)+自检: DHT 单总线要用 */
    oled_i2c_init();
    oled_init();

#if BOOT_SELFTEST
    /* 全屏点亮 300ms 再清屏: 能刷白说明 PB6/PB7 软件 I2C 通路与地址全对;
       全黑 = 屏没被驱动起来, 查 SCL/SDA 接线、上拉与从机地址(0x3C/0x3D) */
    oled_fill();  oled_flush();  delay_ms(300);
    oled_clear(); oled_flush();  delay_ms(150);
    oled_header(0, "GRAIN TOMO", "BOOT");
    oled_str(0, UI_L2, "SELF CHECK ...", 0);
    oled_flush();
    delay_ms(400);
#endif

    /* 2. 其余外设 */
    ADC_PinsInit();
    USART1_Init(115200);
    /* 页面解压自检: 只解压不发送, 比对长度与 XOR。**不依赖 ESP/网络** ——
       所以哪怕模块没联网, 串口也能立刻回答"网页这辈子发得出去吗"。
       (串口按 `Z` 可随时重跑。) */
    pg_selftest();
#if USE_TIM_CAPTURE
    TIM2_CaptureInit();          /* 超声波输入捕获 (仅 USE_TIM_CAPTURE=1 时启用) */
#endif
    fan_pwm_init();              /* 4 路风扇 PWM (TIM2_CH1/CH2 + TIM3_CH1/CH2, 1kHz) */
    adc_safe_init();             /* 带超时的安全 ADC (不可再用模板 Adc_Init) */
#if LAMP_KIND == 0
    sk_init();                   /* SK6812 状态灯 (PB12; 随报警灯一起让位); 位时序由 sk_lamp_sweep() 扫描决定 */
    u1_print("[GT] SK6812 DIN=PB12 ");
    u1_print(g_sk_ok ? "timer OK" : "NO TIMER (状态灯不可用)");
    u1_print("\r\n");
#else
    alarm_init();                /* 报警灯 AlarmLight (PB12; 2026-10-03 由 PA3 让位给 485) */
    u1_print("[GT] AlarmLight SIG=PB12 active_low=");
    u1_print_int(ALARM_ACTIVE_LOW);
    u1_print("\r\n");
#endif
    glamp_init();                /* 补光 LED (PA5, 软件 PWM; 阳极经 10K 接本脚, 阴极接地) */
    u1_print("[GT] GrowLight SIG=PA5 active_high=");
    u1_print_int(GLAMP_ACTIVE_HIGH);
    u1_print(" dark<");
    u1_print_int(LIGHT_DARK_PCT);
    u1_print("% ON\r\n");
#if GAS_ENABLE
    u1_print("[GT] MQ-135 AO=PA6 DO=PA7\r\n");
#else
    u1_print("[GT] MQ-135 DISABLED (PA5 -> GrowLight); GAS=-- everywhere\r\n");
#endif
#if FLAME_ENABLE
    u1_print("[GT] Flame DO=PB0 active_low=");
    u1_print_int(FLAME_ACTIVE_LOW);
    u1_print(" raw_now=");
    u1_print_int(FLAME_RAW());
    u1_print("  ('F' flips polarity)\r\n");
#endif
#if RAIN_ENABLE
    u1_print("[GT] Rain DO=PC14 active_low=");
    u1_print_int(g_rain_alow);
    u1_print(" raw_now=");
    u1_print_int(RAIN_RAW());
    u1_print("  (rain -> ALL fans stop; 'R' flips polarity)\r\n");
#endif
#if BDS_ENABLE
    u1_print("[GT] BDS soft-UART RX=PB2 (Beidou TX -> PB2) ");
    u1_print_int((int32_t)g_bds_baud);
    u1_print(" 8N1; 'B' cycles baud, 'N' dumps last NMEA line\r\n");
    u1_print("[GT]   (module RX on PB1 is NOT used -- Beidou only talks, never listens)\r\n");
    u1_print("[GT]   line check: 网页\"北斗定位\"页会报 翻转次数/最短脉宽/实测波特率/极性;\r\n");
    u1_print("[GT]   翻转一直是 0 次 = 模块没往这根线上发数据(线没接/没上电); 'Y' 重测。\r\n");
#endif
#if USE_WIND485
    /* 485 风速的接线与自检口径一次说清 —— 现场不用翻文档 */
    u1_print("[GT] Wind RS485: A->PA3(USART2_RX) B->PA2(USART2_TX) ");
    u1_print_int((int32_t)WIND_BAUD);
    u1_print(" 8N1 addr=");
    u1_print_int((int32_t)WIND_ADDR);
    u1_print("  ('V' dumps TX/RX bytes)\r\n");
    u1_print("[GT]   TRIG moved PA2->PB1, AlarmLight moved PA3->PB12  (platform has no MAX485;\r\n");
    u1_print("[GT]   A/B are treated as a plain serial pair in sim, use SP3485 on real HW)\r\n");
#endif
    delay_ms(300);

#if USE_WIND485
    /* RS485 风速: USART2 (PA2=TX 接 485-B, PA3=RX 接 485-A), 9600 8N1。
     * ★ 平台没有 485 收发器 —— 平台把这类器件简化成"一对串口线", 直连即可;
     *   实物上要在中间插 MAX485/SP3485(见六-B 节与 固件说明.md)。
     * 上电就先问一次: 成败当场打在串口上, 顺带 WR(网页三处)立刻有个真值或 --。 */
    USART2_Init(WIND_BAUD);
    wind_poll();
#endif

#if USE_ESP8266
    USART3_Init(115200);
    esp_start();                 /* AT 建网 + 开 80 端口 TCP 服务器 */
#endif

    /* 3. 状态初值 —— 注意: 温湿度**不给"看起来正常"的默认值**。
       宁可显示 --.- 让人一眼看出没读到, 也不拿一个编造的 25.0C/50.0% 充数。 */
    g.temp_x10 = 0; g.rh_x10 = 0; g.dht_ok = 0;
    g.in_temp_x10 = 0; g.in_rh_x10 = 0; g.dht_in_ok = 0;
    g.tof_us = -1; g.v_base = 0.0f; g.v_meas = 0.0f; g.r_dev = 0.0f;
    g.pest = 0; g.alarm = 0; g.lamp_on = 0; g.vent_auto = 1;
    g.phase = 0.0f; g.dist_mm = 0; g.wind_x10 = 0;
    /* 光照: 同样**不给"看起来正常"的默认值** —— 上电先标无效, 等 light_read()
       真正读到 ADC 才置 light_ok=1; 在那之前屏/网页/串口一律显示 --。 */
    g.light_adc = 0; g.light_mv = 0; g.light_pct = 0; g.light_ok = 0; g.glamp_on = 0;
    g.gas_pct_x10 = 0; g.gas_do = 0;
    g.gas_ok      = 0;  /* ★ 气体也走"先标无效"这条路: 等 gas_read() 真读到 ADC 才置 1,
                         *   在那之前三处一律显示 -- (与上面 light_ok 完全对称)。 */
    /* 火焰传感器: 上电先读一次**原始电平** —— 这一读数同时就是接线自检:
       无火源时它应停在"无火"那一态; 之后拿火源靠近若不翻转, 就是接线或极性的事。
       (不设 flame_ok: 一位数字线无法自证"是否接上", 见 flame_read() 的说明。) */
    g.flame_raw = 0; g.flame = 0;
    /* 雨量: 与火焰同一口径 —— 不设 rain_ok, 只报**原始电平**(一位数字线无法自证接没接);
       上电先读一次, 这一读数本身就是接线自检。 */
    g_rain_raw = 0; g_rain_on = 0;
#if !GAS_ENABLE
    /* 气体整路作废: 上面那三个 0 就是终态, 屏/网页/串口三处都显示 -- */
#endif
    fan_all_off();               /* 4 路风扇上电全停, 等内外温差决策再启动 */

    /* 4. 上电自检: 蜂鸣1声 + 两盏灯各做**极性测试**(alarm_probe / glamp_probe,
     *    见 main() 前那两段)。它们都只有亮/灭两态, 所以只测一件事:**高电平点亮
     *    还是低电平点亮** —— 各用低/高电平驱动 2 秒, 共 4 轮:
     *      第 1 轮(ALARM LOW )亮 -> ALARM_ACTIVE_LOW 保持 1;
     *      第 2 轮(ALARM HIGH)亮 -> 把 ALARM_ACTIVE_LOW 改成 0;
     *      第 3 轮(GROW  LOW )亮 -> GLAMP_ACTIVE_HIGH 保持 0;
     *      第 4 轮(GROW  HIGH)亮 -> 把 GLAMP_ACTIVE_HIGH 改成 1;
     *      两轮都不亮            -> 平台没下发状态(连亮灯贴图都不画), 先查接线。
     * 轮次与代号同时打在 OLED 与 USART1 上, 便于对照。
     * 延时用 ms_dly 而非 delay_ms —— 平台的 delay_ms 是桩, 会一闪而过看不见。 */
    beep_n(1, 60);
#if LAMP_KIND == 0
  #if SK_SWEEP_ENABLE
    sk_lamp_sweep();
  #endif
#else
    alarm_probe();               /* 第 1/2 轮: 报警灯 PB12 */
#endif
    glamp_probe();               /* 第 3/4 轮: 补光灯 PA5 —— 与报警灯同一套判读办法 */
    LAMP_OFF();

    /* 5. 首次采集 + 声速基线自标定 (DHT11 需上电稳定 1s, 用 ms_dly 保证是真 1s) */
    ms_dly(1000);
    dht_autodetect();            /* 扫全部空闲脚, 找出 DHT11 实际接在哪根 */

#if DHT_DIAG_MODE
    dht_diag_screen();           /* 两屏诊断: 延时是否真在计时 + 每个候选脚的应答 */
#endif
    {
        int32_t t = hc_read_us();
        if (t > 0) {
            g.tof_us = t;
            g.v_base = 2000.0f * PATH_MM / (float)t;
        }
    }
#if DUST_ENABLE
    g.dust_ug = dust_read();
#endif
    light_read();
#if GAS_ENABLE
    gas_read(&g.gas_pct_x10, &g.gas_do, &g.gas_ok);
#endif
#if FLAME_ENABLE
    flame_read();
#endif
#if RAIN_ENABLE
    rain_read();
#endif
    tomo_update();
    alarm_update();
    ui_draw();
    oled_flush();

    u1_print("[GT] system ready, protocol 0xAA 0x55 @115200\r\n");
    u1_print("[GT] grow-lamp keys: 'L' force ON 5s | 'K' force OFF 5s | 'A' auto |"
             " 'I' flip light dir | 'P' flip lamp polarity | 'F' flip flame polarity\r\n");
    u1_print("[GT] 排障口径: 灯不亮先按 'L' 强制点亮 —— 亮=驱动好(查光照数据),"
             " 不亮=查灯这一侧(10K 太大/元件类型/换 'P' 翻极性)\r\n");
    u1_print("[GT]   (same as 0xC5 0xB8 / 0xC5 0xB9)\r\n");
#if USE_ESP8266 && ESP_HTML_ECHO
    u1_print("[GT] 网页已固化在 Flash(约 2.5 万字节, 流式发送, 不占 RAM);\r\n");
    u1_print("[GT] 浏览器开 http://<模块IP>/ 看整页; 页面每 1 秒自己取 /d 的 JSON。\r\n");
    u1_print("[GT] 数据 JSON 每 3 秒自动打到本口 (===== JSON BEGIN/END ===== 夹着)\r\n");
    u1_print("[GT]   上网页: 本机跑 python _live.py 8765, 再在平台页面控制台粘 平台注入.js\r\n");
    u1_print("[GT] 发 'W' 可随时手动补打一次数据 JSON\r\n");
#endif

    /* 6. 主循环 */
    while (1) {
        uint32_t now = millis();

        rx_poll();

#if BDS_ENABLE
        /* 北斗软串口: 线空闲时 ~1µs 返回; 见到起始位就阻塞着收完那一行(9600≈73ms)。
         * 这一步是为了让 FAN_USE_TIM_PWM=0(软件 PWM 风扇)那个变体里 bds_task
         * 仍然被引用 —— 成本几乎为零, 覆盖率翻倍。 */
        bds_task();
#endif

#if USE_ESP8266
        esp_poll();
        /* 请求字节收了但一直没凑到 24 个(极少见) → 静默 120ms 后按"整页"处理 */
        if (!g_http_req && g_req_n > 0u && (uint32_t)(now - g_req_ms) > 120u) g_http_req = 1;
        /* 去抖 400ms: 一次访问可能分几段到达, 别为同一个请求答两遍。
         * (2026-10-02 之前是 3000ms —— 页面要每 3 秒取一次数据, 3 秒的去抖会把
         *  轮询的一半请求吃掉, 所以必须调小。) */
        if (g_http_req && (now - g_http_t) >= 400) {
            g_http_req = 0;
            g_http_t   = now;
            esp_serve();
        }
#endif

        /* ★ 数据 JSON 定时上报 —— "仿真里真实数据上网页"的通路, **不经过 ESP8266**:
         *   平台 uart_monitor 收到这段文本 → 页面里注入的 JS 推给本机 _live.py
         *   → 本机浏览器渲染 3D 大屏。平台有没有模拟网络都不影响这条演示路径。 */
        if (JSON_AUTO_MS > 0 && (uint32_t)(now - t_rep) >= (uint32_t)JSON_AUTO_MS) {
            t_rep = now;
            u1_json_report();
        }

        /* 温湿度 2s。两路各自独立读取、各自独立报错:
             舱外(脚由扫描认到) —— 还没认到脚时每拍换一根候选脚继续找, 现场补线可自愈
             仓内(固定 DHT_IN_PIN) —— 不扫描
           注: DHT11 的"两次采样间隔 ≥1s"是**针对同一颗器件**的要求; 内外是两颗
               独立器件, 所以同一拍里连着读没有冲突。 */
        if (now - t_dht >= 2000) {
            t_dht = now;

            /* ---------------- 舱外 DHT11 ---------------- */
            if (!g.dht_ok) {
                uint8_t id = dht_cand[g_scan_i];
                g_scan_i = (uint8_t)((g_scan_i + 1u) % DHT_CAND_N);

                if (id != (uint8_t)DHT_IN_PIN) {     /* 仓内那根不参与舱外扫描 */
                    if (dht_read_pin(id, &g.temp_x10, &g.rh_x10, dht_raw) == 0) {
                        char  b[48];
                        char *p = b;
                        g_dht_pin = id;
                        g.dht_ok  = 1; g.dht_ever = 1;
                        p = s_put(p, "[GT] DHT11 OUT found on ");
                        p = s_dhtpin(p, id);
                        p = s_put(p, "\r\n");
                        u1_print(b);
                    }
                }
            } else {
                uint8_t st = dht_read(&g.temp_x10, &g.rh_x10);
                if (st == 0) {
                    g.dht_ever = 1;
                } else {                             /* 读失败: 如实报错, 不留旧值 */
                    char   rb[64];
                    char  *rp = rb;
                    uint8_t ri;

                    g.dht_ok = 0;
                    rp = s_put(rp, "[GT] DHT11 OUT ");
                    rp = s_put(rp, (st == 2) ? "checksum error" : "no response");
                    if (st == 2) {                   /* 原始 5 字节打出来看 */
                        rp = s_put(rp, " raw=");
                        for (ri = 0; ri < 5; ri++) {
                            if (ri) rp = s_put(rp, ",");
                            rp = s_dec(rp, dht_raw[ri], 0);
                        }
                    }
                    rp = s_put(rp, "\r\n");
                    u1_print(rb);
                }
            }

            /* ---------------- 仓内 DHT11 (固定脚) ---------------- */
            {
                uint8_t st2 = dht_read_in(&g.in_temp_x10, &g.in_rh_x10);

                if (st2 == 0) {
                    g.dht_in_ok = 1; g.dht_in_ever = 1;
                } else {
                    if (g.dht_in_ok) {               /* 只在"由好变坏"时报一次, 免得每 2s 刷屏 */
                        char  ib[48];
                        char *ip = ib;
                        ip = s_put(ip, "[GT] DHT11 IN  lost: ");
                        ip = s_put(ip, (st2 == 2) ? "checksum error" : "no response");
                        ip = s_put(ip, "\r\n");
                        u1_print(ib);
                    }
                    g.dht_in_ok = 0;                 /* 立即失效: 通风决策随即停用差值 */
                }
            }
        }

#if USE_WIND485
        /* 485 风速 1s 一问 —— Modbus 是"主站问、从站答", 不轮询就永远读不到。
         * 9600 波特率下一问一答约 8ms, 摊在 1s 节拍里对主循环毫无影响。 */
        if (now - t_wind >= 1000) {
            t_wind = now;
            wind_poll();
        }
#endif

        /* 超声波 + 粉尘 + 光照 500ms (气体已随 MQ-135 作废, GAS_ENABLE=0 时不再采样) */
        if (now - t_us >= 500) {
            int32_t t;
            t_us = now;
            t = hc_read_us();
            if (t > 0) g.tof_us = t;
#if DUST_ENABLE
            g.dust_ug = dust_read();
#endif
            light_read();
#if GAS_ENABLE
            gas_read(&g.gas_pct_x10, &g.gas_do, &g.gas_ok);
#endif
#if FLAME_ENABLE
            flame_read();     /* 火焰传感器 DO(PB0): 500ms 一采, 明火无需更快 */
#endif
#if RAIN_ENABLE
            rain_read();      /* 雨量 DO(PC14): 500ms 一采, 一下雨就联锁停风机 */
#endif
        }

        /* 层析重建 1s */
        if (now - t_tomo >= 1000) {
            uint32_t t0 = millis();
            uint32_t el;
            t_tomo = now;
            tomo_update();
            alarm_update();
            el = millis() - t0;

            send_vmap();
            send_amap();
            send_summary(el);
            send_level();
            send_env();
            send_vent();
            send_sound();
        }

        /* OLED 刷新 1s (本地摘要; 完整信息在网页端) */
        if (now - t_ui >= 1000) {
            t_ui = now;
            ui_draw();
            oled_flush();
        }

#if LAMP_KIND == 1
        /* 报警灯闪烁节拍: 每个 10ms 节拍算一次"这一刻该亮还是该灭"。
         * 放在节拍这里(而不是采集/上报里)是因为节奏必须**由时间决定** ——
         * 慢闪 500ms / 快闪 150ms 都与采集周期无关, 换采集周期也不用改灯。 */
        alarm_lamp_task();
#endif

        /* 光控补光灯: 同样每 10ms 刷一次。灯的状态 = f(当前光照), 而光照每 500ms
         * 才更新一次, 所以这里**不需要**再算延时 —— 反复写同一个 BSRR/BRR 无副作用,
         * 反而保证"光照一变, 灯在 10ms 内就跟着变"。 */
        glamp_task();

#if USE_ASCII_LOG
        if (now - t_log >= 2000) {
            t_log = now;
            send_ascii_log();
        }
#endif

        /* ★ 节拍 10ms —— 现在这段"空等"被**补光 LED 的软件 PWM** 借用:
         *   切成 GLED_PWM_SLICES 个 1ms 片, 每片开头按占空比设一次 LED 电平,
         *   10 片走完 = 一个完整 PWM 周期(100Hz, 人眼看不出闪烁)。
         *   ⇒ **调光不额外占用时间**: 这里原本就是空等, 只是把空等换成带输出的忙等。 */
        {
            uint8_t kk;
            for (kk = 0; kk < (uint8_t)GLED_PWM_SLICES; kk++) {
                gled_pwm_slice(kk);              /* 先按本片决定 LED 亮/灭 */
#if FAN_USE_TIM_PWM
  #if BDS_ENABLE
                /* 这 1ms 本来也是空等 —— 借来**高频轮询北斗软串口**:
                 * bds_task() 在 PB2 为高(线空闲)时立刻返回, 不占时间; 一旦看到起始位
                 * 就阻塞着把整行 NMEA 收完(9600 下约 73ms/行 —— 期间补光 PWM 的 LED
                 * 保持在当前电平不动, 亮度会短暂抖一下, 可接受)。
                 * ★ 一行都还没收到时, 每 500ms 拿一片出来做**线路体检**: 量出来的
                 *   翻转次数/最短脉宽/极性直接上报网页, 用来分开"线没接"与"我们收错了"。 */
                if (g_bds_lines == 0u && (uint32_t)(millis() - g_bds_pb_ms) >= 500u) {
                    g_bds_pb_ms = millis();
                    bds_probe();
                    bds_autocfg();
                } else {
                    uint32_t t0 = (uint32_t)us_now();
                    while ((uint16_t)(us_now() - (uint16_t)t0) < 1000u) bds_task();
                }
  #else
                us_dly(1000);                    /* 硬件 PWM 风扇自己跑, 这里只把 1ms 等满 */
  #endif
#else
                fan_soft_cycle();                /* 软件 PWM 风扇: 内部自带 1ms 忙等 */
#endif
            }
        }
    }
}
