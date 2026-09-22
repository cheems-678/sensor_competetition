/**
 * @file    servo.h
 * @brief   4路SG90舵机驱动 (软件PWM, TIM1中断)
 * @note    舵机引脚 (PA5原来那路"窗户1"已拆除, PA5改接DHT11):
 *          窗户2: PB15
 *          窗户3: PB14
 *          窗户4: PA11
 *          门:     PA12
 *
 *          软件PWM: TIM1 100us中断, 200步=20ms(50Hz)
 *          0度=0.5ms(5步), 90度=1.5ms(15步)
 *          舵机VCC接5V(LM2596), GND接功率地, 信号接MCU引脚
 */
#ifndef __SERVO_H
#define __SERVO_H

#include <stm32f10x.h>
#include <stdint.h>

/* 舵机通道编号 */
enum {
    SERVO_WIN2 = 0,    /* 窗户2 PB15 */
    SERVO_WIN3,        /* 窗户3 PB14 */
    SERVO_WIN4,        /* 窗户4 PA11 */
    SERVO_DOOR,        /* 门    PA12 */
};

/**
 * @brief 初始化4路舵机软件PWM (TIM1 100us中断)
 */
void Servo_Init(void);

/**
 * @brief 设置舵机角度
 * @param ch    舵机通道 0~3
 * @param angle 角度 0~90 (0=关闭, 90=打开)
 */
void Servo_SetAngle(uint8_t ch, uint8_t angle);

/**
 * @brief 打开窗户/门 (转到90度)
 * @param ch 舵机通道 0~3
 */
void Servo_Open(uint8_t ch);

/**
 * @brief 关闭窗户/门 (转到0度)
 * @param ch 舵机通道 0~3
 */
void Servo_Close(uint8_t ch);

/**
 * @brief 打开全部窗户和门
 */
void Servo_OpenAll(void);

/**
 * @brief 关闭全部窗户和门
 */
void Servo_CloseAll(void);

/**
 * @brief 获取舵机当前角度
 * @param ch 舵机通道 0~3
 * @return 角度 0~90
 */
uint8_t Servo_GetAngle(uint8_t ch);

/**
 * @brief TIM1更新中断服务函数 (在stm32f10x_it.c中调用)
 *        每100us执行一次, 维护4路软件PWM
 */
void Servo_PWM_ISR(void);

#endif /* __SERVO_H */
