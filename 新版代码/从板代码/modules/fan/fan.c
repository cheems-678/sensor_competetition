/**
 * @file    fan.c
 * @brief   4路DC4010 12V风扇PWM驱动实现 (STM32F103C8T6, SPL)
 *
 * 通道映射 (均为定时器默认引脚, 无需重映射):
 *   ch0 = 风扇1 = PA1 = TIM2_CH2
 *   ch1 = 风扇2 = PB1 = TIM3_CH4
 *   ch2 = 风扇3 = PB8 = TIM4_CH3
 *   ch3 = 风扇4 = PB9 = TIM4_CH4
 *
 * PWM频率 = FAN_PWM_FREQ_HZ (config.h, 默认25kHz)
 *   定时器计数时钟: 72MHz/(PSC+1) = 1MHz  (PSC=71)
 *   ARR = 1MHz / FAN_PWM_FREQ_HZ - 1
 *   占空比: CCR = pct * (ARR+1) / 100
 *
 * 注意:
 *   1. FAN_ACTIVE_LOW=1 时占空比取反, 用于驱动级反相(低电平导通)的板子。
 *   2. 2线直流风扇在25kHz下可能无法启动, 改小 FAN_PWM_FREQ_HZ 或
 *      用 Fan_SetSpeedKick() 全速启动后再降到目标转速。
 */

#include "fan.h"
#include "../../config.h"

/* 定时器计数时钟 1MHz (72MHz / (71+1)) */
#define FAN_PSC          71
#define FAN_CCR_MAX      ((uint16_t)(1000000UL / FAN_PWM_FREQ_HZ))   /* = ARR+1 */
#define FAN_ARR          ((uint16_t)(FAN_CCR_MAX - 1))

/* 每路通道的硬件资源 */
typedef struct {
    TIM_TypeDef*  tim;
    uint8_t       ch;      /* 定时器通道号 2/3/4 */
    GPIO_TypeDef* port;
    uint16_t      pin;
} FanCh;

static const FanCh fan_tab[FAN_NUM] = {
    { TIM2, 2, GPIOA, GPIO_Pin_1 },   /* 风扇1 */
    { TIM3, 4, GPIOB, GPIO_Pin_1 },   /* 风扇2 */
    { TIM4, 3, GPIOB, GPIO_Pin_8 },   /* 风扇3 */
    { TIM4, 4, GPIOB, GPIO_Pin_9 },   /* 风扇4 */
};

static uint8_t fan_speed[FAN_NUM] = {0, 0, 0, 0};

/* 有效反相掩码: 旧全局开关(FAN_ACTIVE_LOW=1 → 全部反相) 与 逐路掩码 合并 */
#if (FAN_ACTIVE_LOW)
#define FAN_AL_MASK     (0x0F | (FAN_ACTIVE_LOW_CH))
#else
#define FAN_AL_MASK     (FAN_ACTIVE_LOW_CH)
#endif

/* ---------- 占空比 -> CCR (ch用于判断该路是否反相) ---------- */
static uint16_t fan_pct2ccr(uint8_t ch, uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (FAN_AL_MASK & (1u << ch))
        pct = (uint8_t)(100 - pct);          /* 该路驱动级低电平导通 */
    return (uint16_t)(((uint32_t)pct * FAN_CCR_MAX) / 100u);
}

/* ---------- 写某路CCR ---------- */
static void fan_apply(uint8_t ch, uint16_t ccr)
{
    TIM_TypeDef* T = fan_tab[ch].tim;

    switch (fan_tab[ch].ch) {
        case 2: TIM_SetCompare2(T, ccr); break;
        case 3: TIM_SetCompare3(T, ccr); break;
        case 4: TIM_SetCompare4(T, ccr); break;
        default: break;
    }
}

/* ---------- 定时器时基初始化 (每个定时器只做一次) ---------- */
static void fan_base_init(TIM_TypeDef* T)
{
    TIM_TimeBaseInitTypeDef tim;

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler         = FAN_PSC;
    tim.TIM_CounterMode       = TIM_CounterMode_Up;
    tim.TIM_Period            = FAN_ARR;
    tim.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(T, &tim);

    /* ARR预装载: 改频率时不会出现异常周期 */
    TIM_ARRPreloadConfig(T, ENABLE);
    TIM_ClearFlag(T, TIM_FLAG_Update);
}

/* ---------- 某个通道的PWM输出初始化 ---------- */
static void fan_oc_init(uint8_t ch)
{
    TIM_OCInitTypeDef oc;
    TIM_TypeDef*      T = fan_tab[ch].tim;

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 0;                    /* 初始0% */
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;

    switch (fan_tab[ch].ch) {
        case 2:
            TIM_OC2Init(T, &oc);
            TIM_OC2PreloadConfig(T, TIM_OCPreload_Enable);
            break;
        case 3:
            TIM_OC3Init(T, &oc);
            TIM_OC3PreloadConfig(T, TIM_OCPreload_Enable);
            break;
        case 4:
            TIM_OC4Init(T, &oc);
            TIM_OC4PreloadConfig(T, TIM_OCPreload_Enable);
            break;
        default: break;
    }
}

/* ---------- 初始化4路风扇PWM ---------- */
void Fan_Init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;

    /* 时钟: GPIOA/B + TIM2/3/4 (APB1) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 |
                           RCC_APB1Periph_TIM3 |
                           RCC_APB1Periph_TIM4, ENABLE);

    /* 1. 4个PWM引脚 -> 复用推挽输出 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    for (i = 0; i < FAN_NUM; i++) {
        gpio.GPIO_Pin = fan_tab[i].pin;
        GPIO_Init(fan_tab[i].port, &gpio);
    }

    /* 2. 时基 (TIM4被两路共用, 只初始化一次) */
    fan_base_init(TIM2);
    fan_base_init(TIM3);
    fan_base_init(TIM4);

    /* 3. 各通道PWM输出 */
    for (i = 0; i < FAN_NUM; i++) {
        fan_oc_init(i);
        fan_apply(i, fan_pct2ccr(i, 0));
        fan_speed[i] = 0;
    }

    /* 4. 启动计数 */
    TIM_Cmd(TIM2, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

/* ---------- 设置转速 (0~100%) ---------- */
void Fan_SetSpeed(uint8_t ch, uint8_t pct)
{
    if (ch >= FAN_NUM) return;
    if (pct > 100) pct = 100;

    fan_speed[ch] = pct;
    fan_apply(ch, fan_pct2ccr(ch, pct));
}

/* ---------- 全速启动后再降到目标转速 ----------
 * 用于2线直流风扇: 25kHz低占空比时可能启动困难,
 * 先给100%运行 kick_ms 毫秒, 再降到 pct。
 */
void Fan_SetSpeedKick(uint8_t ch, uint8_t pct, uint16_t kick_ms)
{
    if (ch >= FAN_NUM) return;
    if (pct == 0) { Fan_SetSpeed(ch, 0); return; }

    Fan_SetSpeed(ch, 100);
    delay_ms(kick_ms);
    Fan_SetSpeed(ch, pct);
}

/* ---------- 全部风扇 ---------- */
void Fan_SetAll(uint8_t pct)
{
    uint8_t i;
    for (i = 0; i < FAN_NUM; i++)
        Fan_SetSpeed(i, pct);
}

void Fan_SetAllKick(uint8_t pct, uint16_t kick_ms)
{
    uint8_t i;
    for (i = 0; i < FAN_NUM; i++)
        Fan_SetSpeedKick(i, pct, kick_ms);
}

void Fan_AllOff(void)
{
    Fan_SetAll(0);
}

uint8_t Fan_GetSpeed(uint8_t ch)
{
    if (ch >= FAN_NUM) return 0;
    return fan_speed[ch];
}

/* ---------- 单路全速测试 (排查硬件用) ----------
 * 打开指定通道100%占空比, 保持 ms 毫秒后关闭。
 * 期间用万用表量该引脚应为稳定的高电平(约3.3V, FAN_ACTIVE_LOW=0时)。
 * 引脚有高电平但风扇不转 => 驱动级(MOS/三极管)或风扇接线问题。
 */
void Fan_TestSingle(uint8_t ch, uint16_t ms)
{
    if (ch >= FAN_NUM) return;
    Fan_SetSpeed(ch, 100);
    delay_ms(ms);
    Fan_SetSpeed(ch, 0);
}

/* ---------- 引脚静态电平测试 ----------
 * 用于把"定时器PWM"这个因素排除掉:
 *   先把4路引脚切成普通推挽GPIO(定时器停), 再整组输出高/低电平。
 *   若静态高电平能转、PWM不转 => PWM频率/极性不匹配;
 *   若静态高低都不转 => 驱动级或接线问题(与程序无关)。
 */

/* 4路引脚切为普通推挽输出, 定时器停止 (引脚电平由 Fan_DirectSet 控制) */
void Fan_DirectMode(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;

    TIM_Cmd(TIM2, DISABLE);
    TIM_Cmd(TIM3, DISABLE);
    TIM_Cmd(TIM4, DISABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    for (i = 0; i < FAN_NUM; i++) {
        gpio.GPIO_Pin = fan_tab[i].pin;
        GPIO_Init(fan_tab[i].port, &gpio);
        GPIO_ResetBits(fan_tab[i].port, fan_tab[i].pin);
    }
}

/* 直接设定某路引脚电平 (需先调用 Fan_DirectMode) */
void Fan_DirectSet(uint8_t ch, uint8_t high)
{
    if (ch >= FAN_NUM) return;
    GPIO_WriteBit(fan_tab[ch].port, fan_tab[ch].pin, high ? Bit_SET : Bit_RESET);
}

/* 恢复PWM输出 (重新按AF_PP配置引脚并启动定时器) */
void Fan_PwmMode(void)
{
    Fan_Init();
    Fan_SetAll(0);
}
