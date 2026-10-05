/**
 * @file    spi.h
 * @brief   SPI通信模块头文件
 * @version V1.0
 */
#ifndef __SPI_H
#define __SPI_H
#include "sys.h" 
/**
 * @defgroup SPI_Functions SPI函数
 * @{
 */
/**
 * @brief 初始化 SPI 外设（SPI1），配置 GPIO 与 SPI 参数
 */		  	    													  
void SPIx_Init(void);			 //初始化SPI口

/**
 * @brief 设置 SPI 波特率分频系数
 * @param SpeedSet 波特率分频值（例如 SPI_BaudRatePrescaler_2/8/16/256）
 */
void SPIx_SetSpeed(u8 SpeedSet); //设置SPI速度   
/**
 * @brief SPI 读写一个字节（阻塞）
 * @param TxData 要发送的字节
 * @return 接收到的字节；发生超时或错误返回 0
 */
u8 SPIx_ReadWriteByte(u8 TxData);//SPI总线读写一个字节
/** @} */
#endif

