/**
 ******************************************************************************
 * @file    ds18b20.h
 * @brief   DS18B20 数字温度传感器驱动头文件
 *
 * 声明 DS18B20 单总线（1-Wire）通信的初始化、温度读取及底层 IO 控制接口。
 * 默认使用 STM32F1 的 PA0 作为 DQ 数据引脚，通过直接操作寄存器实现
 * 精确的时序控制。
 *
 * @note    - 温度值以整数形式返回：单位为 0.0001°C（如 251250 表示 25.1250°C）
 *          - 所有延时依赖 delay.h 提供的 delay_us() / delay_ms()
 ******************************************************************************
 */
#ifndef __DS18B20_H
#define __DS18B20_H 
#include "sys.h"   

/**
 * @defgroup DS18B20_IO_Config DS18B20 IO 引脚配置（PA0）
 * @{
 * 使用直接寄存器操作设置 GPIOA.0 的输入/输出模式，确保单总线时序精确。
 * - 输出模式：推挽输出，50MHz（0b0011）
 * - 输入模式：浮空输入（0b1000），配合外部上拉电阻
 *
 * @note   在切换为输入前，先置 PA0 为高电平（通过 BSRR），避免毛刺。
 */
/**
 * @brief  将 DS18B20_DQ (PA0) 配置为输入模式（浮空输入）
 *
 * 先通过 BSRR 设置 PA0 输出高，再切换为输入，防止总线被意外拉低。
 */
#define DS18B20_IO_IN()  {GPIOA->BSRR=(((uint32_t)0x01)<<0);GPIOA->CRL&=0XFFFFFFF0;GPIOA->CRL|=8<<0;}
#define DS18B20_IO_OUT() {GPIOA->CRL&=0XFFFFFFF0;GPIOA->CRL|=3<<0;}

/**
 * @brief  将 DS18B20_DQ (PA0) 配置为输出模式（推挽，50MHz）
 */										   
#define	DS18B20_DQ_OUT PAout(0) //数据端口	PA0 

/**
 * @brief  DS18B20 数据引脚输出宏（写 PA0）
 */
#define	DS18B20_DQ_IN  PAin(0)  //数据端口	PA0 

/** @} */ // end of DS18B20_IO_Config

/**
 * @defgroup DS18B20_API DS18B20 驱动 API
 * @{
 */

/**
 * @brief  初始化 DS18B20 并配置默认参数
 * @retval 0: 初始化成功，设备存在
 * @retval 1: 未检测到 DS18B20
 *
 * 执行复位检测，并配置：
 * - 报警上限 = 100°C，下限 = 0°C
 * - 分辨率 = 12 位（0.0625°C）
 * - 将配置保存至 EEPROM
 *
 * @note   必须在系统启动时调用一次。
 */
u8 DS18B20_Init(void);//初始化DS18B20
/**
 * @brief  获取当前温度值
 * @return 温度 × 10000（单位：0.0001°C），范围：-550000 ～ +1250000
 *
 * @note   - 内部会自动启动一次温度转换（阻塞式，但未显式延时！）
 *         - 实际使用中建议在调用前确保转换已完成（或增加延时）
 *         - 返回值可直接用于整数运算或格式化显示
 */
int DS18B20_Get_Temp(void);//获取温度
/**
 * @brief  启动一次温度转换（非阻塞）
 *
 * 发送 Skip ROM + Convert T 命令，启动内部 ADC 转换。
 * 转换时间取决于分辨率（12 位需约 750ms）。
 *
 * @note   若需非阻塞读取，应先调用此函数，延时后再调用 \ref DS18B20_Read_Byte 等读取数据。
 */
void DS18B20_Start(void);//开始温度转换
/**
 * @brief  向 DS18B20 写入一个字节数据（LSB 先传）
 * @param  dat: 要写入的 8 位数据
 *
 * @warning 此为底层通信函数，通常不应在应用层直接调用。
 */
void DS18B20_Write_Byte(u8 dat);//写入一个字节
/**
 * @brief  从 DS18B20 读取一个字节数据（LSB 先传）
 * @retval 读取到的 8 位数据
 *
 * @warning 此为底层通信函数，通常不应在应用层直接调用。
 */
u8 DS18B20_Read_Byte(void);//读出一个字节
/**
 * @brief  从 DS18B20 读取一个数据位
 * @retval 0 或 1
 *
 * @warning 此为底层通信函数，通常不应在应用层直接调用。
 */
u8 DS18B20_Read_Bit(void);//读出一个位
/**
 * @brief  检测 DS18B20 是否响应主机的复位信号
 * @retval 0: DS18B20 存在并正确响应
 * @retval 1: 无响应（可能未连接、损坏或上拉缺失）
 *
 * @note   调用前需先调用 \ref DS18B20_Rst()。
 */
u8 DS18B20_Check(void);//检测是否存在DS18B20
/**
 * @brief  向 DS18B20 发送复位脉冲（启动通信）
 *
 * 拉低总线 750μs，然后释放，触发 DS18B20 发送存在脉冲。
 * 调用后应立即调用 \ref DS18B20_Check() 等待响应。
 */
void DS18B20_Rst(void);//复位DS18B20    
/** @} */ // end of DS18B20_API
#endif /* __DS18B20_H */
