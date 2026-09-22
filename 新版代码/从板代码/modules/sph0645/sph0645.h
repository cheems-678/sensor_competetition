/**
 * @file    sph0645.h
 * @brief   SPH0645LM4H 数字MEMS麦克风(双路)驱动 —— 粮堆虫害声监测
 *
 * 硬件 (从板原理图):
 *   两颗 SPH0645LM4H 并联在同一组 I2S 时钟上, 用 SEL 脚区分左右声道:
 *     H3: SEL=GND → 左声道;   H4: SEL=3V3 → 右声道
 *   信号线 (不占用额外引脚):
 *     PB13 = I2S2_CK  → 两模块第3脚 BCLK
 *     PB12 = I2S2_WS  → 两模块第4脚 LRCLK(WS)
 *     PB15 = I2S2_SD  ← 两模块第5脚 DOUT   (I2S接收时SD为输入)
 *
 * 采集: I2S2 主模式接收, 16bit/16kHz, DMA1_Channel4 搬 MIC_FRAME_SAMPLES 个
 *       单声道采样(左右交织共 2×N 个半字), 采完立即做特征提取。
 *
 * 特征(每通道): RMS能量、峰值、过零率估算主频、脉冲(虫鸣/啃食)次数;
 *       另外给出 64 点幅度包络, 便于上位机画"声音波形"。
 */
#ifndef __SPH0645_H
#define __SPH0645_H

#include "../../config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 I2S2 + DMA1_CH4 (PB12/PB13/PB15)
 * @note  初始化后 I2S 时钟持续输出(麦克风需要BCLK才工作), DMA 按需启动。
 */
void     Mic_Init(void);

/**
 * @brief 采集一帧声音并做特征分析 (阻塞约 MIC_FRAME_SAMPLES/16000 秒)
 * @param  f 输出分析结果
 * @return 0=成功 1=超时/失败
 */
uint8_t  Mic_Capture(MicFrame *f);

/** @brief 取原始交织PCM缓冲(左,右,左,右...), 供调试/进一步处理 */
const int16_t* Mic_GetRawBuffer(void);

/** @brief 每声道采样点数(用于上位机换算时间轴) */
uint16_t Mic_GetFrameSamples(void);

#ifdef __cplusplus
}
#endif

#endif /* __SPH0645_H */
