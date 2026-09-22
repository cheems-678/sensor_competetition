/**
 * 传感器采集与驱动层 - 实现
 *
 * 硬件链路:
 *   STM32 DAC(PA4) → 压电驱动 → T1~T8换能器
 *   T1~T8换能器 → OPA2356放大 → STM32 ADC(PA0)
 *   74HC138译码器选择当前发射/接收的换能器
 *   继电器切换发射/接收方向
 *
 * 采集流程:
 *   1. 74HC138选择发射换能器
 *   2. 继电器切到发射模式
 *   3. DAC输出40kHz×5周期脉冲 → 压电片振动
 *   4. 74HC138切换到接收换能器
 *   5. 继电器切到接收模式
 *   6. ADC以1MHz采样1024点
 *   7. 从波形中提取穿越时间(首达波时间)和信号幅度
 */

#include "sensor.h"

// 40kHz在72MHz时钟下的DAC查找表 (32点/周期)
static const uint16_t sineTable32[32] = {
    2048, 2447, 2831, 3185, 3495, 3761, 3974, 4130,
    4224, 4255, 4224, 4130, 3974, 3761, 3495, 3185,
    2831, 2447, 2048, 1649, 1265, 911, 601, 335,
    122, -34, -128, -97, 6, 162, 375, 641
};

Sensor::Sensor() {
    memset(&hadc1, 0, sizeof(hadc1));
    memset(&hdac, 0, sizeof(hdac));
    memset(&htim3, 0, sizeof(htim3));
}

void Sensor::begin() {
    // === ADC1 初始化 (PA0, 12位, 1MHz采样) ===
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = ADC_RX_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(ADC_RX_PORT, &gpio);
    
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T3_TRGO;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    HAL_ADC_Init(&hadc1);
    
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_RX_CHANNEL;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // === DAC 初始化 (PA4, Channel1) ===
    __HAL_RCC_DAC_CLK_ENABLE();
    
    gpio.Pin = DAC_TX_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(DAC_TX_PORT, &gpio);
    
    hdac.Instance = DAC;
    HAL_DAC_Init(&hdac);
    
    DAC_ChannelConfTypeDef dacConfig = {0};
    dacConfig.DAC_Trigger = DAC_TRIGGER_T3_TRGO;
    dacConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&hdac, &dacConfig, DAC_CHANNEL_1);
    
    // === TIM3 初始化 (1MHz触发ADC+DAC) ===
    __HAL_RCC_TIM3_CLK_ENABLE();
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = (SYS_CLK_MHZ * 1000000 / ADC_SAMPLE_RATE_HZ) - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 0xFFFF;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim3);
    
    TIM_MasterConfigTypeDef masterConfig = {0};
    masterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
    masterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &masterConfig);
    
    // === GPIO 初始化 ===
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // 74HC138选择线
    gpio.Pin = MUX_A_PIN | MUX_B_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MUX_A_PORT, &gpio);
    
    gpio.Pin = MUX_C_PIN;
    HAL_GPIO_Init(MUX_C_PORT, &gpio);
    
    // 收发切换继电器
    gpio.Pin = TX_RX_SEL_PIN;
    HAL_GPIO_Init(TX_RX_SEL_PORT, &gpio);
    
    // LED
    gpio.Pin = LED_PIN;
    HAL_GPIO_Init(LED_PORT, &gpio);
    
    // 按键 (上拉输入)
    gpio.Pin = BTN_START_PIN | BTN_MODE_PIN | BTN_SAVE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BTN_START_PORT, &gpio);
    
    // 初始状态
    muxSelect(0);
    setMode(false);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

void Sensor::muxSelect(uint8_t ch) {
    ch &= 0x07;
    HAL_GPIO_WritePin(MUX_A_PORT, MUX_A_PIN, (ch & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_B_PORT, MUX_B_PIN, (ch & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_C_PORT, MUX_C_PIN, (ch & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Sensor::setMode(bool isTransmit) {
    // 继电器: HIGH=发射模式, LOW=接收模式
    HAL_GPIO_WritePin(TX_RX_SEL_PORT, TX_RX_SEL_PIN, 
                      isTransmit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(2);  // 继电器切换时间
}

void Sensor::selectTransmitter(uint8_t idx) {
    muxSelect(idx);
}

void Sensor::selectReceiver(uint8_t idx) {
    // 接收端通过模拟开关(CD4051)选择, 复用74HC138控制
    muxSelect(idx);
}

void Sensor::dacBurst(uint16_t freq, uint8_t cycles, uint16_t amplitude) {
    // 通过DAC查找表输出正弦波脉冲
    uint32_t pointsPerCycle = 32;
    uint32_t totalPoints = pointsPerCycle * cycles;
    
    for (uint32_t i = 0; i < totalPoints; i++) {
        uint16_t val = sineTable32[i % pointsPerCycle];
        // 缩放到指定幅度
        val = 2048 + (uint16_t)(((int32_t)val - 2048) * amplitude / 4095);
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, val);
        // 延时控制频率: 40kHz → 25μs/点 → 72MHz下约1800个时钟
        // 用简单延时近似 (实际应用中用DMA+Timer精确控制)
        for (volatile int j = 0; j < 18; j++);  // ~1μs延时 @72MHz
    }
    // 归零
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
}

void Sensor::transmit() {
    dacBurst(TX_FREQ_HZ, TX_BURST_COUNT, TX_AMPLITUDE);
}

void Sensor::receive(uint16_t* buffer, uint16_t count) {
    // 启动ADC连续采样
    for (uint16_t i = 0; i < count; i++) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 1);
        buffer[i] = HAL_ADC_GetValue(&hadc1);
        // 1μs延时 (1MHz采样率)
        for (volatile int j = 0; j < 12; j++);
    }
    HAL_ADC_Stop(&hadc1);
}

void Sensor::extractTimeOfFlight(uint16_t* data, uint16_t count,
                                  float* tof_ms, float* amp_dB) {
    // 1. 计算直流偏置 (前100个采样的平均值)
    uint32_t dcOffset = 0;
    uint16_t noiseCount = (count > 100) ? 100 : count / 4;
    for (uint16_t i = 0; i < noiseCount; i++) {
        dcOffset += data[i];
    }
    dcOffset /= noiseCount;
    
    // 2. 噪声门限 (3倍噪声标准差)
    float noiseStd = 0;
    for (uint16_t i = 0; i < noiseCount; i++) {
        float dev = (float)data[i] - dcOffset;
        noiseStd += dev * dev;
    }
    noiseStd = sqrtf(noiseStd / noiseCount);
    float threshold = dcOffset + 5.0f * noiseStd;  // 5倍信噪比门限
    
    // 3. 找首达波 (第一个超过门限的采样点)
    uint16_t tofSamples = 0;
    for (uint16_t i = noiseCount; i < count; i++) {
        if (data[i] > threshold) {
            tofSamples = i;
            break;
        }
    }
    
    // 4. 转换为时间 (采样率1MHz → 1μs/点)
    *tof_ms = (float)tofSamples / 1000.0f;  // μs → ms
    
    // 5. 计算信号幅度 (峰值)
    uint16_t maxVal = 0, minVal = 4095;
    for (uint16_t i = tofSamples; i < count && i < tofSamples + 200; i++) {
        if (data[i] > maxVal) maxVal = data[i];
        if (data[i] < minVal) minVal = data[i];
    }
    uint16_t peakToPeak = maxVal - minVal;
    
    // 6. 转换为dB (相对满量程4095)
    if (peakToPeak > 0) {
        *amp_dB = 20.0f * log10f((float)peakToPeak / 4095.0f);
    } else {
        *amp_dB = -60.0f;
    }
    
    // 7. 有效性检查
    if (tofSamples == 0 || tofSamples >= count - 10) {
        *tof_ms = 0;
        *amp_dB = -60;
    }
}

void Sensor::measureRay(uint8_t txIdx, uint8_t rxIdx, RayMeasurement* result) {
    result->valid = false;
    result->timeOfFlight_ms = 0;
    result->amplitude_dB = -60;
    
    // 1. 选择发射换能器
    selectTransmitter(txIdx);
    setMode(true);  // 发射模式
    HAL_Delay(1);
    
    // 2. 发射激励脉冲
    transmit();
    
    // 3. 快速切换到接收模式
    setMode(false);
    selectReceiver(rxIdx);
    
    // 4. 延时一小段(等继电器稳定)然后开始采样
    // 注意: 这里继电器切换的延时是关键, 需要调试优化
    HAL_Delay(1);
    
    // 5. ADC采样
    uint16_t buffer[ADC_BUFFER_SIZE];
    receive(buffer, ADC_BUFFER_SIZE);
    
    // 6. 提取穿越时间和幅度
    float tof, amp;
    extractTimeOfFlight(buffer, ADC_BUFFER_SIZE, &tof, &amp);
    
    result->timeOfFlight_ms = tof;
    result->amplitude_dB = amp;
    result->valid = (tof > 0.01f && amp > -50.0f);
    
    // 复制原始数据(调试用)
    memcpy(result->raw, buffer, ADC_BUFFER_SIZE * sizeof(uint16_t));
}

void Sensor::scanAllRays(RayMeasurement* results) {
    // 扫描全部28条射线
    int rayIdx = 0;
    for (int i = 0; i < NUM_TRANSDUCERS; i++) {
        for (int j = i + 1; j < NUM_TRANSDUCERS; j++) {
            measureRay(i, j, &results[rayIdx]);
            rayIdx++;
        }
    }
}

void Sensor::listenAcousticEmission(uint16_t* buffer, uint16_t count, uint32_t duration_ms) {
    // 声发射模式: 所有换能器当麦克风用, 连续采样
    // 用于检测虫害啮咬声(频率范围: 1-20kHz)
    setMode(false);
    selectReceiver(0);
    
    uint32_t start = HAL_GetTick();
    uint16_t idx = 0;
    
    while (HAL_GetTick() - start < duration_ms && idx < count) {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 1);
        buffer[idx % count] = HAL_ADC_GetValue(&hadc1);
        idx++;
        // 降低采样率到50kHz (适合虫害声)
        for (volatile int j = 0; j < 250; j++);  // ~20μs延时
    }
    HAL_ADC_Stop(&hadc1);
}

float Sensor::measureGrainLevel_mm() {
    // 料位测量: 顶部换能器(T3, 索引2)向下发射, 测反射时间
    selectTransmitter(2);
    setMode(true);
    HAL_Delay(1);
    transmit();
    
    setMode(false);
    selectReceiver(2);
    HAL_Delay(1);
    
    uint16_t buffer[ADC_BUFFER_SIZE];
    receive(buffer, ADC_BUFFER_SIZE);
    
    float tof, amp;
    extractTimeOfFlight(buffer, ADC_BUFFER_SIZE, &tof, &amp);
    
    // 距离 = 声速 × 时间 / 2 (往返)
    float distance_mm = SOUND_GRAIN_MS * tof * 1000.0f / 2.0f;
    
    return distance_mm;
}
