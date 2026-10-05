/**
 * @file    sys.h
 * @brief   STM32F10x 系统核心头文件：中断分组配置与位带操作宏
 *
 * 本文件提供两类核心功能：
 * 1. **NVIC 中断优先级分组配置**（通过 NVIC_Configuration()）
 * 2. **GPIO 位带操作宏**（实现类似 51 单片机的 PAout(5)=1 风格）
 *
 * @note
 * - 位带操作基于 Cortex-M3 的 **Bit-Band 特性**（参考《CM3 权威指南》第5章）；
 * - 所有 GPIO 宏仅适用于 STM32F10x 系列（外设基地址固定）；
 * - 本文件包含 stm32f10x.h，无需重复包含。
 *
 */

#ifndef __SYS_H
#define __SYS_H	
#include "stm32f10x.h"

/* ==============================================================================
 *                              系统配置选项
 * ============================================================================== */

/**
 * @brief 是否支持 uC/OS-II 实时操作系统
 * @details
 * - 0: 不支持（裸机系统）
 * - 1: 支持（需链接 uC/OS 相关库）
 * @note 若启用 uC/OS，通常需调整 SysTick、PendSV 等系统异常处理。
 */
#define SYSTEM_SUPPORT_UCOS		0		//定义系统文件夹是否支持UCOS
																	    
/* ==============================================================================
 *                          位带操作（Bit-Banding）宏
 * ============================================================================== */

/**
 * @defgroup BitBand_Macros Bit-Band Operation Macros
 * @brief 基于 Cortex-M3 位带区的单比特原子操作
 * @{
 */

/** 
 * @brief 计算位带别名地址
 * @param addr  字节地址（必须位于 SRAM 或外设位带区）
 * @param bitnum 比特位编号（0～7）
 * @return 位带别名地址（可直接读写该地址操作单个比特）
 * @note 仅适用于 STM32F10x 的 SRAM (0x20000000～0x200FFFFF) 和外设 (0x40000000～0x400FFFFF)
 */
#define BITBAND(addr, bitnum) ((addr & 0xF0000000)+0x2000000+((addr &0xFFFFF)<<5)+(bitnum<<2)) 

/** 
 * @brief 通过地址访问 volatile 32 位内存
 */
#define MEM_ADDR(addr)  *((volatile unsigned long  *)(addr)) 

/** 
 * @brief 通过位带宏访问单个比特
 */
#define BIT_ADDR(addr, bitnum)   MEM_ADDR(BITBAND(addr, bitnum)) 

/** @} */ // end of BitBand_Macros

/* ==============================================================================
 *                        GPIO 位带地址映射（STM32F10x）
 * ============================================================================== */

// ODR (Output Data Register) 地址
#define GPIOA_ODR_Addr    (GPIOA_BASE+12) //0x4001080C 
#define GPIOB_ODR_Addr    (GPIOB_BASE+12) //0x40010C0C 
#define GPIOC_ODR_Addr    (GPIOC_BASE+12) //0x4001100C 
#define GPIOD_ODR_Addr    (GPIOD_BASE+12) //0x4001140C 
#define GPIOE_ODR_Addr    (GPIOE_BASE+12) //0x4001180C 
#define GPIOF_ODR_Addr    (GPIOF_BASE+12) //0x40011A0C    
#define GPIOG_ODR_Addr    (GPIOG_BASE+12) //0x40011E0C    

// IDR (Input Data Register) 地址
#define GPIOA_IDR_Addr    (GPIOA_BASE+8) //0x40010808 
#define GPIOB_IDR_Addr    (GPIOB_BASE+8) //0x40010C08 
#define GPIOC_IDR_Addr    (GPIOC_BASE+8) //0x40011008 
#define GPIOD_IDR_Addr    (GPIOD_BASE+8) //0x40011408 
#define GPIOE_IDR_Addr    (GPIOE_BASE+8) //0x40011808 
#define GPIOF_IDR_Addr    (GPIOF_BASE+8) //0x40011A08 
#define GPIOG_IDR_Addr    (GPIOG_BASE+8) //0x40011E08 
 
/* ==============================================================================
 *                      GPIO 位操作宏（单引脚读写）
 * ============================================================================== */

/**
 * @defgroup GPIO_Bit_Ops GPIO Single-Pin Bit Operations
 * @brief 类似 51 单片机的 PAout(5) = 1 风格操作
 * @note 参数 n 必须为 0～15 的常量表达式（编译期确定），否则行为未定义。
 * @{
 */
#define PAout(n)   BIT_ADDR(GPIOA_ODR_Addr, (n))  ///< 设置 PA[n] 输出电平（1=高，0=低）
#define PAin(n)    BIT_ADDR(GPIOA_IDR_Addr, (n))  ///< 读取 PA[n] 输入电平

#define PBout(n)   BIT_ADDR(GPIOB_ODR_Addr, (n))  ///< 设置 PB[n] 输出电平
#define PBin(n)    BIT_ADDR(GPIOB_IDR_Addr, (n))  ///< 读取 PB[n] 输入电平

#define PCout(n)   BIT_ADDR(GPIOC_ODR_Addr, (n))  ///< 设置 PC[n] 输出电平
#define PCin(n)    BIT_ADDR(GPIOC_IDR_Addr, (n))  ///< 读取 PC[n] 输入电平

#define PDout(n)   BIT_ADDR(GPIOD_ODR_Addr, (n))  ///< 设置 PD[n] 输出电平
#define PDin(n)    BIT_ADDR(GPIOD_IDR_Addr, (n))  ///< 读取 PD[n] 输入电平

#define PEout(n)   BIT_ADDR(GPIOE_ODR_Addr, (n))  ///< 设置 PE[n] 输出电平
#define PEin(n)    BIT_ADDR(GPIOE_IDR_Addr, (n))  ///< 读取 PE[n] 输入电平

#define PFout(n)   BIT_ADDR(GPIOF_ODR_Addr, (n))  ///< 设置 PF[n] 输出电平（仅部分型号有 PF）
#define PFin(n)    BIT_ADDR(GPIOF_IDR_Addr, (n))  ///< 读取 PF[n] 输入电平

#define PGout(n)   BIT_ADDR(GPIOG_ODR_Addr, (n))  ///< 设置 PG[n] 输出电平
#define PGin(n)    BIT_ADDR(GPIOG_IDR_Addr, (n))  ///< 读取 PG[n] 输入电平

/** @} */ // end of GPIO_Bit_Ops

/* ==============================================================================
 *                            函数声明
 * ============================================================================== */

/**
 * @brief 配置 NVIC 中断优先级分组
 * @details 设置为 Group 2：2 位抢占优先级 + 2 位子优先级
 * @note 应在 main() 开始处调用一次，且仅调用一次。
 */
void NVIC_Configuration(void);

#endif // __SYS_H
