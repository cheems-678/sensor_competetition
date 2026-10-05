/**
 * @file    pt8211.h
 * @brief   PT8211 双通道 16 位 DAC 驱动头文件
 *
 * 本头文件声明了用于驱动 PT8211 音频 DAC 芯片的初始化与数据输出接口。
 * PT8211 通过三线制串行接口（WS/BCK/DIN）接收左右声道 16 位音频数据，
 * 广泛应用于嵌入式音频播放场景。
 *
 * @note
 * - 引脚分配（默认）：
 *     - WS  → PB12
 *     - BCK → PB13
 *     - DIN → PB14
 * - 数据格式：16 位有符号整数（补码），范围 [-32768, +32767]
 * - 输出参考电压：VCC/2（交流耦合后可得音频信号）
 *
 * @warning 函数参数中的 "lift_ch" 为拼写错误，正确应为 "left_ch"（左声道），
 *          但为保持与 `.c` 文件实现一致，此处予以保留。
 */
#ifndef __DHT11_H
#define __DHT11_H 
#include "sys.h"   

/**
 * @defgroup PT8211_Driver PT8211 DAC 驱动接口
 * @{
 * 
 * 提供 PT8211 的初始化与音频数据输出功能。
 * 支持双声道 16 位 PCM 音频流输出。
 */

/**
 * @brief 初始化 PT8211 所需的 GPIO 引脚
 * @details 配置 PB12 (WS)、PB13 (BCK)、PB14 (DIN) 为推挽输出模式
 */
void PT8211_Init(void);

/**
 * @brief 向 PT8211 输出左右声道音频样本
 * @param right_ch 右声道音频数据（16 位有符号整数）
 * @param lift_ch  左声道音频数据（⚠️ 实际应为 left_ch，拼写保留兼容）
 *
 * @note
 * - 数据范围：-32768 ～ +32767
 * - 负值输出电压 < VCC/2，正值 > VCC/2
 * - 函数内部自动处理补码转换与 MSB 优先串行发送
 * - 先发送右声道（WS=0），再发送左声道（WS=1）
 */
void PT8211_Output(int16_t right_ch, int16_t lift_ch);

/** @} */ // end of PT8211_Driver

#endif /* __PT8211_H */















