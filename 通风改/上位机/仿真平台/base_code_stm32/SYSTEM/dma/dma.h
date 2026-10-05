/**
 * @file    dma.h
 * @brief   STM32F10x DMA1 通用通道配置接口声明
 *
 * 本头文件声明一个通用的 DMA 初始化函数，适用于大多数 **存储器到外设**
 * 的数据传输场景（如 USART 发送、SPI 发送等）。
 *
 * @note
 * - 当前实现固定为以下配置：
 *   - 传输方向：存储器 → 外设（Memory-to-Peripheral）
 *   - 数据宽度：8 位（Byte）
 *   - 存储器地址自增，外设地址固定
 *   - 工作模式：Normal（单次传输，非循环）
 *   - 优先级：High
 * - 若需其他配置（如 16 位、循环模式、外设→存储器），请扩展此接口或创建专用函数。
 *
 * @warning
 * - 仅适用于 DMA1（STM32F10x 系列中 DMA1 支持 USART1/2/3、SPI1/2、ADC1/2 等）；
 * - 调用后需手动使能外设的 DMA 请求（如 USART_DMACmd()）；
 * - 不支持内存到内存（M2M）传输。
 */
#ifndef __DMA_H
#define	__DMA_H	   


#include "sys.h"
#include "stm32f10x_dma.h"

/**
 * @brief 配置 DMA1 指定通道的基本参数（存储器→外设，8位，Normal 模式）
 * @param DMA_CHx       DMA 通道指针，例如 DMA1_Channel4（常用于 USART1_TX）
 * @param cpar          外设寄存器地址（如 (u32)&USART1->DR）
 * @param cmar          存储器起始地址（如 (u32)tx_buffer）
 * @param cndtr         待传输的数据项数量（注意：按“项”计数，非字节数）
 * @details
 * 此函数适用于典型的 **发送类** 应用，例如：
 *   - USART 发送字符串
 *   - SPI 发送数据帧
 *   - DAC 输出波形数据
 *
 * @note
 * - 数据宽度固定为 8 位，若外设要求 16 位（如某些 SPI 模式），需修改底层实现；
 * - 传输完成后通道自动停止（Normal 模式）；
 * - 函数内部会自动使能 DMA1 时钟；
 * - 调用后仍需通过外设控制寄存器使能 DMA 请求（如 USART_DMAReq_Tx）。
 *
 */
void DMA_Config(DMA_Channel_TypeDef*DMA_CHx,u32 cpar,u32 cmar,u16 cndtr);//配置DMA1_CHx


#endif // __DMA_H

