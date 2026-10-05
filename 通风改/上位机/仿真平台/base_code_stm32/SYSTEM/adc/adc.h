/**
 * @file    adc.h
 * @brief   STM32F10x ADC 驱动接口声明
 *
 * 本头文件提供两种 ADC 使用模式的接口：
 * 
 * 1. **单通道软件触发模式**  
 *    - 初始化：Adc_Init()
 *    - 读取：Get_Adc() / Get_Adc_Average()
 *    - 默认使用 PA6（ADC_Channel_6）
 *    - 适用于低频、按需采样场景（如电池电压、按键检测）
 *
 * 2. **5 通道 DMA 循环采集模式**  
 *    - 初始化：ADC1_DMA_Config()
 *    - 数据自动存入全局数组 ADC_ConvertedValue[5]
 *    - 使用 PA0～PA4（ADC_Channel_0～4）
 *    - 适用于多路传感器实时同步采集
 *
 * @note
 * - 两种模式互斥，请勿同时初始化
 * - ADC 时钟配置在 .c 文件中完成（≤14 MHz）
 * - DMA 模式下无需调用 Get_Adc()，直接读取缓冲区即可
 */

#ifndef __ADC_H
#define __ADC_H	
#include "sys.h"

/** 
 * @brief 存放 ADC 5 通道 DMA 采集结果（对应 PA0～PA4 → ADC_Channel_0～4）
 * @details 在 ADC1_DMA_Config() 启动后，该数组由 DMA 自动更新
 */
extern volatile uint16_t ADC_ConvertedValue[5];

/**
 * @brief 初始化 ADC1 为单通道连续转换模式（默认通道：PA6 / ADC_Channel_6）
 * @details 配置 GPIO、ADC 参数并执行校准，不启用 DMA 或中断
 * @note 调用后需使用 Get_Adc() 主动读取数据
 */
void Adc_Init(void);

/**
 * @brief 配置 ADC1/2 中断优先级（用于 EOC 中断）
 * @details 当前中断服务函数仅清除标志位，无实际处理逻辑
 * @note 若需在转换完成时执行操作，请确保在 adc.c 中实现处理代码
 */
void Adc_NVIC_Config(void);

/**
 * @brief 获取指定 ADC 通道的单次转换值
 * @param ch ADC 通道号（如 ADC_Channel_6）
 * @return 12 位 ADC 转换结果（范围 0～4095）
 * @note
 * - 每次调用会重新配置通道并启动一次转换
 * - 采样时间较短（1.5 cycles），仅适用于低输出阻抗信号源
 */
u16  Get_Adc(u8 ch); 

/**
 * @brief 对指定通道进行多次采样并返回平均值
 * @param ch ADC 通道号
 * @param times 采样次数（建议 4～16 次）
 * @return 平均后的 ADC 值
 * @note 每次采样间隔约 1ms，用于抑制随机噪声
 */
u16 Get_Adc_Average(u8 ch,u8 times); 

/**
 * @brief 初始化 ADC1 + DMA1 实现 5 通道（PA0～PA4）循环采集
 * @details
 * - 自动配置 ADC 扫描模式、连续转换、DMA 循环传输
 * - 转换结果持续写入 ADC_ConvertedValue[0..4]
 * - 主程序可随时读取该数组获取最新采样值
 * @note 调用后 ADC 持续工作，无需手动触发转换
 */
void ADC1_DMA_Config(void);

#endif // __ADC_H
