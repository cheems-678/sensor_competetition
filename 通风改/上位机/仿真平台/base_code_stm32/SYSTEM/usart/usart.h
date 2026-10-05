/**
 * @file    usart.h
 * @brief   STM31F10x 串口驱动接口声明：USART1 与 USART2
 *
 * 本头文件提供两个串口的初始化接口及接收缓冲机制：
 * - **USART1**：主调试串口，支持 `printf` 重定向 + 中断接收（需启用 EN_USART1_RX）；
 * - **USART2**：辅助通信串口，仅提供初始化接口（中断需用户自行配置）。
 *
 * @note
 * - USART1 引脚：TX=PA9, RX=PA10；
 * - USART2 引脚：TX=PA2, RX=PA3；
 * - 接收使用全局缓冲区 `USART_RX_BUF` 和状态字 `USART_RX_STA`（仅 USART1）；
 * - 调用 `uart_init()` 后，可直接使用 `printf()` 输出到 USART1。
 *
 * @warning
 * - 接收缓冲区无自动溢出保护，请确保上位机单次发送 ≤ `USART_REC_LEN - 1` 字节；
 * - 若禁用 `EN_USART1_RX`，则接收中断逻辑应被移除以节省资源。
 */
#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "sys.h" 

/* ==============================================================================
 *                              配置选项
 * ============================================================================== */

/**
 * @brief 定义 USART1 接收缓冲区最大长度（字节）
 * @note 实际可用数据长度为 USART_REC_LEN - 1（预留终止符或防止溢出）
 */
#define USART_REC_LEN  			200  	//定义最大接收字节数 200
#define EN_USART1_RX 			1		//使能（1）/禁止（0）串口1接收

/**
 * @brief 使能/禁止 USART1 接收功能
 * @details
 * - 1: 使能接收中断，使用全局缓冲区 `USART_RX_BUF`
 * - 0: 禁止接收（仅用于发送，如纯 printf 调试）
 * @note 即使设为 0，当前实现仍会开启 RXNE 中断，建议配合 ISR 逻辑调整。
 */
extern u8  USART_RX_BUF[USART_REC_LEN]; //接收缓冲,最大USART_REC_LEN个字节.末字节为换行符 

/**
 * @brief USART1 接收状态标记（位域）
 * @details
 * - bit15: 接收完成标志（1 = 已接收到完整帧）
 * - bit14: 已接收到回车符（0x0D）
 * - bit13～0: 当前已接收的有效字节数（0 ～ USART_REC_LEN-1）
 * @note 用户应在主循环中轮询 bit15 判断是否接收完成。
 */
extern u16 USART_RX_STA;         		//接收状态标记	

/* ==============================================================================
 *                            函数声明
 * ============================================================================== */

/**
 * @brief 初始化 USART1（PA9-TX, PA10-RX）
 * @param bound 波特率（如 115200）
 * @details
 * - 自动重定向 `printf` 到 USART1；
 * - 使能接收中断（即使 EN_USART1_RX=0，当前代码仍开启中断）；
 * - NVIC 优先级：抢占 3，子优先级 3。
 * @note 调用后即可使用 `printf("Hello\r\n");` 进行调试输出。
 */
void uart_init(u32 bound);

/**
 * @brief 初始化 USART2（PA2-TX, PA3-RX）
 * @param BaudRatePrescaler 波特率（如 9600）
 * @details
 * - 仅配置 GPIO 和 USART 寄存器；
 * - **未配置 NVIC 中断**，若需接收中断，需用户额外调用 NVIC_Init；
 * - 默认格式：8N1，无流控。
 * @note 适用于 GPS、蓝牙、WiFi 模块等外设通信。
 */
void Usart_Int2(uint32_t BaudRatePrescaler);

#endif // __USART_H


