/**
 * @file    buzzer.h
 * @brief   蜂鸣器驱动 (支持有源/无源蜂鸣器)
 * @note    支持两种蜂鸣器:
 *          - 有源蜂鸣器: GPIO 高电平响, 低电平停 (需设置 BUZZER_ACTIVE_HIGH)
 *          - 无源蜂鸣器: 需 PWM 驱动 (使用 TIM 输出指定频率)
 *          
 *          本驱动封装三种控制方式:
 *          1. 基本开关: Buzzer_On() / Buzzer_Off()
 *          2. 延时响: Buzzer_Beep(ms)
 *          3. 定时器 PWM: Buzzer_PWM_Start(hz) / Buzzer_PWM_Stop()
 *          
 *          接线:
 *          有源蜂鸣器: SIG → PB12, VCC → 3.3V/5V, GND → GND
 */
#ifndef __BUZZER_H
#define __BUZZER_H

#include <stm32f10x.h>
#include <stdint.h>

/*==================================================================*
 *                  引脚配置宏 (用户可在此重定义)                    *
*==================================================================*/
#define BUZZER_PORT         GPIOB           /* 蜂鸣器控制端口        */
#define BUZZER_PIN          GPIO_Pin_12     /* 控制引脚 (PB12)       */

/* 有源蜂鸣器电平极性 */
#define BUZZER_ACTIVE_HIGH  1               /* 1=高电平响, 0=低电平响 */

/*==================================================================*
 *                         API 返回状态码                            *
 *==================================================================*/
typedef enum {
    BUZZER_OK = 0,
    BUZZER_ERR,
} BuzzerStatus;

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================*
 *                         API 函数声明                              *
 *==================================================================*/

/**
 * @brief   初始化蜂鸣器 GPIO (有源蜂鸣器模式)
 * @note    配置引脚为推挽输出, 初始为关闭状态
 * @return  BUZZER_OK  初始化成功
 */
BuzzerStatus Buzzer_Init(void);

/**
 * @brief   打开蜂鸣器 (有源)
 * @note    根据 BUZZER_ACTIVE_HIGH 输出相应电平
 */
void Buzzer_On(void);

/**
 * @brief   关闭蜂鸣器 (有源)
 */
void Buzzer_Off(void);

/**
 * @brief   翻转蜂鸣器状态
 */
void Buzzer_Toggle(void);

/**
 * @brief   蜂鸣器响指定时长 (阻塞)
 * @param   ms  鸣响时长 (毫秒)
 * @note    调用 Buzzer_On() 后 delay_ms, 再 Buzzer_Off()
 *          阻塞期间 CPU 忙等, 不适合在中断中调用
 */
void Buzzer_Beep(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
