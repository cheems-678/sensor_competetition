/**
 * 传感器采集与驱动层 - 头文件
 * 控制: 压电换能器阵列切换、发射激励、ADC采样、穿越时间计算
 */
#ifndef __SENSOR_H
#define __SENSOR_H

#include "config.h"

class Sensor {
public:
    Sensor();
    
    // 初始化ADC, DAC, GPIO
    void begin();
    
    // 选择发射换能器 (0-7)
    void selectTransmitter(uint8_t idx);
    
    // 选择接收换能器 (0-7)
    void selectReceiver(uint8_t idx);
    
    // 发射激励脉冲 (40kHz, 5个周期)
    void transmit();
    
    // 接收并采样
    void receive(uint16_t* buffer, uint16_t count);
    
    // 测量单条射线的穿越时间和衰减
    void measureRay(uint8_t txIdx, uint8_t rxIdx, RayMeasurement* result);
    
    // 扫描全部28条射线
    void scanAllRays(RayMeasurement* results);
    
    // 声发射监听 (虫害检测模式)
    void listenAcousticEmission(uint16_t* buffer, uint16_t count, uint32_t duration_ms);
    
    // 料位测量 (顶部换能器向下反射)
    float measureGrainLevel_mm();
    
private:
    ADC_HandleTypeDef hadc1;
    DAC_HandleTypeDef hdac;
    TIM_HandleTypeDef htim3;
    
    // 74HC138 译码器: 选择0-7号换能器
    void muxSelect(uint8_t ch);
    
    // 收发切换
    void setMode(bool isTransmit);
    
    // DAC输出正弦波脉冲
    void dacBurst(uint16_t freq, uint8_t cycles, uint16_t amplitude);
    
    // 从采样数据中提取穿越时间和幅度
    void extractTimeOfFlight(uint16_t* data, uint16_t count, 
                              float* tof_ms, float* amp_dB);
};

#endif // __SENSOR_H
