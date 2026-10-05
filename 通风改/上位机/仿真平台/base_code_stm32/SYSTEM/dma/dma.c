/**
 * @file    dma.c
 * @brief   STM32F10x DMA1 通用通道配置函数
 *
 * 本文件提供一个通用的 DMA 通道初始化函数，适用于大多数外设（如 USART、SPI、ADC 等）
 * 的 **存储器到外设（Memory-to-Peripheral）** 数据传输场景。
 *
 * @note
 * - 当前实现固定为以下配置：
 *   - 传输方向：存储器 → 外设（DMA_DIR_PeripheralDST）
 *   - 数据宽度：8 位（Byte）
 *   - 存储器地址自增，外设地址固定
 *   - 模式：Normal（非循环）
 *   - 优先级：High
 * - 若需其他模式（如外设→存储器、16位、循环模式等），请扩展此函数或创建专用接口。
 *
 * @warning
 * - 本函数仅适用于 DMA1（STM32F10x 中 DMA2 用于高级外设如 ADC3、SPI3 等）；
 * - 调用前需确保目标外设时钟已使能；
 * - 不支持内存到内存（M2M）传输（当前强制禁用）。
 */

#include "dma.h"


/*------------------ 通用 DMA 通道配置函数 ------------------*/

/**
 * @brief 配置 DMA1 指定通道的基本参数（存储器→外设，8位，Normal 模式）
 * @param DMA_CHx       DMA 通道，如 DMA1_Channel4（对应 USART1_TX）
 * @param cpar          外设寄存器地址（如 (u32)&USART1->DR）
 * @param cmar          存储器起始地址（如 (u32)tx_buffer）
 * @param cndtr         待传输的数据项数量（注意：不是字节数，而是“项数”）
 * @details
 * 此函数适用于典型的发送场景，例如：
 *   - USART 发送：cpar = &USARTx->DR, cmar = buffer, cndtr = len
 *   - SPI 发送：cpar = &SPIx->DR, cmar = data, cndtr = count
 *
 * @note
 * - 数据宽度固定为 8 位（Byte），若外设要求 16 位（HalfWord），需修改配置；
 * - 传输完成后通道自动停止（Normal 模式）；
 * - 函数内部使能 DMA1 时钟，无需外部调用；
 * - 建议在启用外设 DMA 请求前调用此函数。
 *
 */
void DMA_Config(DMA_Channel_TypeDef* DMA_CHx,u32 cpar,u32 cmar,u16 cndtr)
{
	DMA_InitTypeDef DMA_InitStructure;
	
 	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);	/* 使能DMA钟源 */
	
	DMA_DeInit(DMA_CHx);   /* 将DMA的通道寄存器重设为缺省值 */

	DMA_InitStructure.DMA_PeripheralBaseAddr = cpar;  /* DMA外设基地址 */
	DMA_InitStructure.DMA_MemoryBaseAddr = cmar;  /* DMA内存基地址 */
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;  /* 数据传输方向，从内存读取发送到外设 */
	DMA_InitStructure.DMA_BufferSize = cndtr;  /* DMA通道的DMA缓存的大小 */
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;  /* 外设地址寄存器不变 */
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;  /* 内存地址寄存器递增 */
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  /* 数据宽度为8位 */
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; /* 数据宽度为8位 */
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;  /* 工作在正常模式 */
	DMA_InitStructure.DMA_Priority = DMA_Priority_High; /* DMA通道 x拥有中优先级 */
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;  /* DMA通道没有设置为内存到内存传输 */
	DMA_Init(DMA_CHx, &DMA_InitStructure);  	
} 
