/**
 * @file mcu680.h
 * @brief MCU680 气体传感器驱动头文件
 *
 * 提供与 MCU680 模块通讯的接口声明：中断配置、指令发送、帧打包与校验等。
 */

#ifndef _MCU680_H_
#define _MCU680_H_

#include "stm32f10x.h"
#include "string.h"
#include "usart.h"
#include "stdio.h"

/** @defgroup MCU680_Functions MCU680 函数
 *  @{ */

/**
 * @brief 配置 MCU680 使用的中断（USART2）
 */
void mcu680_irq_Configuration(void);

/**
 * @brief 发送初始化/控制指令给 MCU680 模块
 */
void send_Instruction(void);

/**
 * @brief 发送单字节到 USART2（阻塞）
 * @param byte 要发送的字节
 */
void USART_send_byte(uint8_t byte);

/**
 * @brief 发送多字节数据到 USART2
 * @param Buffer 指向数据缓冲
 * @param Length 字节数
 */
void USART_Send_bytes(uint8_t *Buffer, uint8_t Length);

/**
 * @brief 将 16-bit 数据数组打包并发送为一帧
 */
void send_out(int16_t *data,uint8_t length,uint8_t send);

/**
 * @brief 将 8-bit 数据打包并发送为一帧
 */
void send_8bit_out(uint8_t *data,uint8_t length,uint8_t send);

/**
 * @brief 校验并复制接收到的帧数据
 * @param data_buf 输出缓冲，校验通过时函数会把内部接收缓冲复制到此处
 * @return 1 校验通过，0 校验失败
 */
uint8_t CHeck(uint8_t *data_buf);

/** @} */
#endif
