/**
 * @file    systick.c
 * @brief   从板时基/GPIO基础服务
 *
 * - SysTick: 1ms 系统节拍 (中断在 stm32f10x_it.c 的 SysTick_Handler 中累加)
 * - DWT 周期计数器(72MHz, 见 dwt_us.h): 用于单总线温湿度那类需要微秒级
 *   脉冲宽度测量的场合。DWT是Cortex-M3内核自带的, 不占用任何定时器。
 *   若DWT不可用(个别调试器/芯片配置), SysTick_DwtOk() 返回0, 驱动侧
 *   会自动改用自适应阈值判位(见 dht11.c)。
 * - LED (PB2) 状态指示
 */

#include "systick.h"
#include "../../dwt_us.h"

/* 系统毫秒节拍 (stm32f10x_it.c 的 SysTick_Handler 中累加) */
volatile uint32_t g_tick_ms = 0;

/* DWT 周期计数器可用标志 */
static uint8_t dwt_ok = 0;

/* ---------- DWT 周期计数器初始化 + 自检 ---------- */
static void dwt_init(void)
{
    uint32_t a, b;
    volatile uint32_t i;

    DWT_US_ENABLE();                              /* 使能DWT周期计数器(绝对地址, 不依赖CMSIS) */

    a = DWT_US_CYCLES();
    for (i = 0; i < 300; i++) { }                 /* 空转一会儿 */
    b = DWT_US_CYCLES();

    dwt_ok = (b != a) ? 1 : 0;
}

uint8_t SysTick_DwtOk(void) { return dwt_ok; }

uint32_t SysTick_RawCycles(void) { return dwt_ok ? DWT_US_CYCLES() : 0; }

uint32_t SysTick_UsNow(void)
{
    if (!dwt_ok) return 0;
    return DWT_US_CYCLES() / (SYS_CLK / 1000000UL);   /* 周期 -> 微秒 */
}

/* ---------- SysTick 1ms 节拍 ---------- */
void SysTick_Init(void)
{
    /* 72MHz / 1000 = 72000 -> 1ms 中断一次 */
    if (SysTick_Config(SYS_CLK / 1000UL) != 0) {
        while (1) { }        /* 配置失败, 停机 */
    }
    dwt_init();
}

void SysTick_Delay_Ms(uint32_t ms)
{
    uint32_t t = g_tick_ms;
    while ((uint32_t)(g_tick_ms - t) < ms) { }
}

void SysTick_Delay_Us(uint32_t us)
{
    if (us == 0) return;

    if (dwt_ok) {
        uint32_t start  = DWT_US_CYCLES();
        uint32_t cycles = us * (SYS_CLK / 1000000UL);
        while ((uint32_t)(DWT_US_CYCLES() - start) < cycles) { }
    } else {
        /* DWT不可用时的粗略延时 (72MHz下1次循环约0.1us) */
        volatile uint32_t n = us * 8UL;
        while (n--) { }
    }
}

uint32_t SysTick_Ms(void) { return g_tick_ms; }

/* ---------- LED 状态指示 (PB2) ---------- */
void LED_Set(uint8_t on)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    g.GPIO_Pin   = LED_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &g);

    GPIO_WriteBit(LED_PORT, LED_PIN, on ? Bit_RESET : Bit_SET);  /* 低电平点亮 */
}
