/**
 ******************************************************************************
 * @file    HX711.h
 * @brief   HX711 24位高精度ADC模块驱动头文件
 *
 * 声明 HX711 电子秤专用 ADC 芯片的引脚定义、校准参数及数据读取接口。
 * 默认使用 STM32F1 的 GPIOB 引脚：
 * - PB8 → SCK（时钟输出）
 * - PB9 → DOUT（数据输入，上拉）
 *
 * @note    - 本驱动默认配置为通道 A、增益 128（最常用称重模式）
 *          - 校准系数 scalar 表示 1 克对应的 ADC 值（需实际标定）
 *          - 所有延时依赖 delay.h 提供的微秒级精度函数
 ******************************************************************************
 */
#ifndef __HX711_H
#define __HX711_H

#include "sys.h"

/**
 * @defgroup HX711_GPIO_Macros HX711 GPIO 控制宏（PB8/PB9）
 * @{
 */
#define HX711_SCK PBout(8) // 
#define HX711_DOUT PBin(9) // 
/** @} */ // end of HX711_GPIO_Macros

/**
 * @defgroup HX711_Calibration HX711 校准参数
 * @{
 * @brief  标定系数：1 克重量对应的 ADC 原始值
 *
 * 示例：若放上 100g 标准砝码，读数为 42000，则 scalar = 42000 / 100 = 420。
 * 实际项目中应通过多点标定确定精确值，并考虑零点偏移。
 *
 * @warning 此值仅为示例！必须根据实际传感器和机械结构重新标定。
 */
#define scalar    420

/**
 * @defgroup HX711_API HX711 驱动 API
 * @{
 */

/**
 * @brief  初始化 HX711 所需的 GPIO 引脚
 *
 * 配置：
 * - PB8 (SCK) 为推挽输出（50MHz）
 * - PB9 (DOUT) 为上拉输入
 *
 * @note   必须在调用 \ref HX711_Read() 前执行一次。
 */
void Init_HX711pin(void);
/**
 * @brief  从 HX711 读取 24 位 ADC 原始数据（通道 A，增益 128）
 * @return 24 位 ADC 值（已转换为标准二进制格式，范围 0~0xFFFFFF）
 *
 * @note   - 函数会阻塞等待 DOUT 变低（表示转换完成）
 *         - 返回值可直接用于重量计算：weight(g) = (raw_value - zero_offset) / scalar
 *         - 内部已对偏移二进制码进行符号修正（异或 0x800000）
 *
 * @warning 若传感器未连接，可能无限等待。建议在产品代码中增加超时机制。
 */
u32 HX711_Read(void);

/** @} */ // end of HX711_API
#endif  /* __HX711_H */
