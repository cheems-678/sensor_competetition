/**
 * @file    servo.c
 * @brief   5路SG90舵机软件PWM驱动实现
 *
 * TIM1: PSC=719(100kHz), ARR=99(100us中断)
 * 每100us中断一次, 200步=20ms=50Hz
 * 0度=5步(0.5ms), 90度=15步(1.5ms)
 */

#include "servo.h"
#include "../../config.h"

/* 舵机引脚定义 */
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} ServoPin;

static const ServoPin servo_pins[SERVO_NUM] = {
    {GPIOA, GPIO_Pin_5},    /* 窗户1 */
    {GPIOB, GPIO_Pin_15},   /* 窗户2 */
    {GPIOB, GPIO_Pin_14},   /* 窗户3 */
    {GPIOA, GPIO_Pin_11},   /* 窗户4 */
    {GPIOA, GPIO_Pin_12},   /* 门 */
};

/* 当前脉冲宽度 (步数 5~15) */
static volatile uint8_t servo_pulse[SERVO_NUM] = {5, 5, 5, 5, 5};
/* 当前角度 (0~90) */
static uint8_t servo_angle[SERVO_NUM] = {0, 0, 0, 0, 0};
/* PWM计数器 (0~199) */
static volatile uint16_t pwm_cnt = 0;

void Servo_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef tim;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_TIM1, ENABLE);

    /* PA5, PA11, PA12 推挽输出 */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PB14, PB15 推挽输出 */
    gpio.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOB, &gpio);

    /* 初始全部低电平 */
    GPIO_ResetBits(GPIOA, GPIO_Pin_5 | GPIO_Pin_11 | GPIO_Pin_12);
    GPIO_ResetBits(GPIOB, GPIO_Pin_14 | GPIO_Pin_15);

    /* TIM1: 100us中断 */
    tim.TIM_Prescaler     = 719;       /* 72MHz/720 = 100kHz */
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    tim.TIM_Period        = 99;        /* 100kHz/100 = 1kHz(100us) */
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim);

    /* 清除更新标志, 使能中断 */
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);
    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

    NVIC_SetPriority(TIM1_UP_IRQn, 1);
    NVIC_EnableIRQ(TIM1_UP_IRQn);

    TIM_Cmd(TIM1, ENABLE);
}

void Servo_SetAngle(uint8_t ch, uint8_t angle)
{
    if (ch >= SERVO_NUM) return;
    if (angle > 90) angle = 90;
    servo_angle[ch] = angle;
    /* pulse = 5 + angle * 10 / 90, 0度->5, 90度->15 */
    servo_pulse[ch] = SERVO_MIN_PULSE +
                     (uint8_t)((uint16_t)angle * (SERVO_MAX_PULSE - SERVO_MIN_PULSE) / 90);
}

void Servo_Open(uint8_t ch)
{
    Servo_SetAngle(ch, SERVO_ANGLE_OPEN);
}

void Servo_Close(uint8_t ch)
{
    Servo_SetAngle(ch, SERVO_ANGLE_CLOSE);
}

void Servo_OpenAll(void)
{
    int i;
    for (i = 0; i < SERVO_NUM; i++)
        Servo_Open((uint8_t)i);
}

void Servo_CloseAll(void)
{
    int i;
    for (i = 0; i < SERVO_NUM; i++)
        Servo_Close((uint8_t)i);
}

uint8_t Servo_GetAngle(uint8_t ch)
{
    if (ch >= SERVO_NUM) return 0;
    return servo_angle[ch];
}

/* TIM1更新中断调用, 每100us执行 */
void Servo_PWM_ISR(void)
{
    int i;

    if (pwm_cnt == 0) {
        /* 周期开始: 全部拉高 */
        for (i = 0; i < SERVO_NUM; i++) {
            servo_pins[i].port->BSRR = servo_pins[i].pin;
        }
    }

    /* 检查每个舵机是否到达脉冲宽度, 到达则拉低 */
    for (i = 0; i < SERVO_NUM; i++) {
        if (pwm_cnt == servo_pulse[i]) {
            servo_pins[i].port->BRR = servo_pins[i].pin;
        }
    }

    pwm_cnt++;
    if (pwm_cnt >= SERVO_PERIOD)
        pwm_cnt = 0;
}
