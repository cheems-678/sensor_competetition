/**
 * @file    stepper_motor.c
 * @brief   四相混合式步进电机驱动实现（四相八拍模式）
 * @version V1.0
 *
 * 本文件实现了基于 GPIO 控制的四相步进电机驱动，采用 **四相八拍** 工作模式，
 * 支持正转、反转及电机关闭功能。
 *
 * @note
 * - 使用引脚：PB12 (IN1), PB13 (IN2), PB14 (IN3), PB15 (IN4)
 * - 电机类型：四相双极性/单极性混合式步进电机
 * - 控制模式：四相八拍（Half-Step），每周期 8 拍，步距角减半，运行更平稳
 * - 转速控制：通过调节每拍延时 `n`（单位：微秒）实现，`n` 越大，转速越慢
 * - 引脚宏定义（需在 stepper_motor.h 中定义）：
 *     - IN1_H / IN1_L → 控制 PB12
 *     - IN2_H / IN2_L → 控制 PB13
 *     - IN3_H / IN3_L → 控制 PB14
 *     - IN4_H / IN4_L → 控制 PB15
 *
 * @warning 长时间通电会导致电机发热，建议在空闲时调用 Motor_Ctrl_Off() 关闭输出。
 */
#include "stm32f10x.h"
#include "delay.h"
#include "stepper_motor.h"
#include <stm32f10x_conf.h>

/**
 * @brief 初始化步进电机控制引脚（PB12～PB15）
 * @details 配置为推挽输出模式，初始状态全部拉低（电机静止）
 */
void motor_configuration(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);

	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStruct);
	GPIO_ResetBits(GPIOB, GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15);
}

/**
 * @brief 步进电机正转（四相八拍模式）
 * @param n 每拍延时时间（单位：微秒），值越大转速越慢
 *
 * @details 相序（八拍）：
 *   1: A↑      → IN1=H
 *   2: A+B↑    → IN1=H, IN4=L (即 B 相激活，假设 IN4 控制 B 相反向)
 *   3: B↑      → IN2=H
 *   4: B+A↓    → IN1=L
 *   5: Ā↑      → IN3=H
 *   6: Ā+B↓    → IN2=L
 *   7: B̄↑      → IN4=H
 *   8: B̄+A↑    → IN3=L
 *
 * @note 实际相序依赖于硬件接线，此处按常见四相八拍顺序实现。
 *       若转向不符，可交换任意两相引脚或调整相序。
 */
void motor_control_F(int n)
{
	
	IN1_H;
	delay_us(n);
	IN4_L;
	delay_us(n);
	IN2_H;
	delay_us(n);
	IN1_L;
	delay_us(n);
	IN3_H;
	delay_us(n);
	IN2_L;
	delay_us(n);
	IN4_H;
	delay_us(n);
	IN3_L;
	delay_us(n);
}

/**
 * @brief 步进电机反转（四相八拍模式）
 * @param n 每拍延时时间（单位：微秒），值越大转速越慢
 *
 * @details 反转相序为正转的逆序，同样为八拍：
 *   1: B̄+A↑ → IN3_L
 *   2: B̄↑   → IN4_H
 *   3: Ā+B↓ → IN2_L
 *   4: Ā↑   → IN3_H
 *   5: B+A↓ → IN1_L
 *   6: B↑   → IN2_H
 *   7: A+B↑ → IN4_L
 *   8: A↑   → IN1_H
 *
 * @note 原注释“四相四拍”有误，实际代码为八拍，已修正。
 */
void motor_control_Z(int n)
{
	IN2_L;
	IN3_L;
	IN4_L;	
	IN1_H;	
	delay_us(n);
	IN1_L;
	IN2_L;
	IN3_L;
	IN4_H;
	delay_us(n);
	IN4_L;
	IN1_L;
	IN2_L;
	IN3_H;
	delay_us(n);
	IN3_L;
	IN4_L;
	IN1_L;
	IN2_H;
	delay_us(n);
}

/**
 * @brief 关闭步进电机所有输出（防止发热）
 * @details 将 IN1～IN4 全部拉低，使电机绕组断电，进入自由停止状态
 *
 * @warning 在电机长时间静止时务必调用此函数，避免过热或功耗过高。
 */
void Motor_Ctrl_Off(void)
{
	GPIO_ResetBits(GPIOB, GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15);
}

