/**
 * 声学传感器驱动 - SPL版本
 *
 * 硬件链路 (实际接线):
 *   STM32 DAC(PA4) → 压电驱动 → CD74HC4067(8选1, S0~S2=PA6/PA7/PA8, S3=PB0接地) → 压电片 T1~T8
 *   压电片 T1~T8 → CD74HC4067 → OPA2356 放大 → STM32 ADC(PA0)
 *   (无继电器, 同一通道先发射后立即接收采样)
 *
 * 采集流程:
 *   1. MUX选发射换能器
 *   2. DAC输出40kHz × 5周期 正弦脉冲 → 压电振动
 *   3. MUX切到接收端
 *   4. ADC 以1MHz采样512点
 *   5. 首达波检测(5σ门限) → 输出穿越时间(ms)和幅度(dB)
 */

#ifndef __SENSOR_H
#define __SENSOR_H

#include "../../config.h"

/* 初始化ADC/DAC/GPIO/TIM */
void    Sensor_Init(void);

/* 选择发射/接收换能器(0~7) */
void    Sensor_SelTx(uint8_t idx);
void    Sensor_SelRx(uint8_t idx);

/* 发射一个脉冲(40kHz×5周期) */
void    Sensor_Transmit(void);

/* ADC采样1024点到 buffer */
void    Sensor_Receive(uint16_t* buf);

/* 测一对换能器的射线数据 */
void    Sensor_MeasureRay(uint8_t tx, uint8_t rx, RayMeas* out);

/* 扫描全部28条射线 */
void    Sensor_ScanAll(RayMeas* results);

/* 声发射监听(虫害) duration_ms内循环采样到 buf (512) */
void    Sensor_ListenAE(uint16_t* buf, uint16_t count, uint32_t duration_ms);

/* 料位测量(顶部换能器T2:索引2, 向下发射, 测反射时间) */
float   Sensor_Level_mm(void);

/* 按键读取 (0=START/PC13, 1=MODE/PC14, 2=SAVE/PC15), 返回1=按下, 50ms去抖 */
uint8_t Key_Read(uint8_t idx);

#endif /* __SENSOR_H */
