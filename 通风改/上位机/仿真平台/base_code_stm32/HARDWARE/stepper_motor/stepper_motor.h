/**
 * @file    stepper_motor.h
 * @brief   四相步进电机驱动头文件（四相八拍控制）
 * @version V1.0
 *
 * 本头文件定义了四相混合式步进电机的 GPIO 控制接口与引脚映射，
 * 支持正转、反转及电机关闭功能，配合 stepper_motor.c 使用。
 *
 * @note
 * - 控制模式：四相八拍（Half-Step），每周期 8 拍，运行平稳
 * - 转速控制：通过调节延时参数 `n`（单位：微秒）实现，`n` 越大，转速越慢
 * - 引脚分配（注意顺序！）：
 *     - IN1 → PB15
 *     - IN2 → PB14
 *     - IN3 → PB13
 *     - IN4 → PB12
 *   即：IN1～IN4 对应 GPIO_Pin_15～GPIO_Pin_12（逆序）
 *
 * @warning 长时间通电会导致电机发热，空闲时务必调用 Motor_Ctrl_Off() 关闭输出。
 */
#ifndef __stepper_motor_H
#define __stepper_motor_H	 
#include "sys.h"
#include <stm32f10x_conf.h>

#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include <stdio.h>
#include <string.h>

/*------------------ 步进电机相位控制宏 ------------------*/

/**
 * @name 相位控制宏（高/低电平）
 * @{
 * 
 * 每个 INx_H / INx_L 宏用于控制对应相绕组的通断。
 * 
 * @note 实际硬件接线必须与以下定义一致：
 *       - IN1 连接 PB15
 *       - IN2 连接 PB14
 *       - IN3 连接 PB13
 *       - IN4 连接 PB12
 */
#define IN1_H   GPIO_SetBits(GPIOB, GPIO_Pin_15)      ///< 激活 IN1（PB15）
#define IN1_L   GPIO_ResetBits(GPIOB, GPIO_Pin_15)    ///< 关闭 IN1

#define IN2_H   GPIO_SetBits(GPIOB, GPIO_Pin_14)      ///< 激活 IN2（PB14）
#define IN2_L   GPIO_ResetBits(GPIOB, GPIO_Pin_14)    ///< 关闭 IN2

#define IN3_H   GPIO_SetBits(GPIOB, GPIO_Pin_13)      ///< 激活 IN3（PB13）
#define IN3_L   GPIO_ResetBits(GPIOB, GPIO_Pin_13)    ///< 关闭 IN3

#define IN4_H   GPIO_SetBits(GPIOB, GPIO_Pin_12)      ///< 激活 IN4（PB12）
#define IN4_L   GPIO_ResetBits(GPIOB, GPIO_Pin_12)    ///< 关闭 IN4

/** @} */

/*------------------ 函数声明 ------------------*/

/**
 * @defgroup StepperMotor_Driver 步进电机驱动接口
 * @{
 */

/**
 * @brief 初始化步进电机控制引脚（PB12～PB15）
 * @details 配置为推挽输出模式，初始状态全部拉低（电机静止）
 */
void motor_configuration(void); 

/**
 * @brief 步进电机正转（四相八拍模式）
 * @param n 每拍延时时间（单位：微秒），值越大转速越慢
 * @note 调用一次完成一个完整八拍周期（即 1 步 = 1/8 周期，具体取决于电机）
 */
void motor_control_F(int n);    

/**
 * @brief 步进电机反转（四相八拍模式）
 * @param n 每拍延时时间（单位：微秒），值越大转速越慢
 * @note 相序为正转的逆序
 */
void motor_control_Z(int n);	 

/**
 * @brief 关闭电机所有输出（防止发热）
 * @details 将 IN1～IN4 全部拉低，绕组断电，电机进入自由停止状态
 */
void Motor_Ctrl_Off(void);	
			    
/** @} */ // end of StepperMotor_Driver

#endif /* __STEPPER_MOTOR_H */
