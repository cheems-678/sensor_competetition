/**
 ******************************************************************************
 * @file    GP2Y1014.c
 * @brief   GP2Y1014AU0F 粉尘传感器驱动实现
 *
 * 实现 GP2Y1014AU0F 光学粉尘传感器的同步采样与 PM2.5 浓度计算。
 * - 控制引脚：PA0（LED 驱动使能）
 * - 模拟输出：连接至 ADC 通道 1（如 PA1）
 *
 * @note    - 传感器要求 LED 以 10ms 周期脉冲驱动（高电平 9.68ms，低电平 0.32ms）
 *          - 有效采样窗口：LED 导通后 280μs 开始，持续约 100μs
 *          - 输出电压 Vout 与粉尘浓度呈线性关系：0.5V ～ 4.0V 对应 0 ～ 500 μg/m³
 *          - 本驱动使用简化公式：PM2.5 (mg/m³) = (Vout × 5 - 3) / 34
 *            （其中 Vout 单位为 mV，基于官方灵敏度 0.5V/(0.1mg/m³) 推导）
 ******************************************************************************
 */
#include "GP2Y1014.h"

/**
 * @brief  LED 控制宏定义（PA0）
 * @{
 */
#define GP2Y_High GPIO_SetBits(GPIOA, GPIO_Pin_0);      ///< 拉高 PA0，关闭 LED（注意：模块低电平点

#define GP2Y_Low  GPIO_ResetBits(GPIOA, GPIO_Pin_0);    ///< 拉低 PA0，点亮内部 LED

/** @} */

/**
 * @brief  存储最近一次 ADC 采样值（原始 12 位数值）
 */
static unsigned int adc_value = 0; //保存ADC数值

/**
 * @brief  存储计算得到的 PM2.5 浓度（单位：mg/m³）
 */
double PM2_5 = 0;   //保存PM2.5浓度值

/**
 * @brief  执行一次 GP2Y1014 同步采样并计算 PM2.5 浓度
 *
 * 严格按照数据手册时序操作：
 * 1. 拉低 LED 引脚（点亮 LED）
 * 2. 延迟 280μs（等待光路稳定）
 * 3. 读取 ADC 值（此时为有效信号）
 * 4. 拉高 LED 引脚（关闭 LED）
 * 5. 延迟剩余时间，确保总周期 ≈10ms
 *
 * @note   - ADC 通道需预先配置为 PA1（或其他对应引脚）
 *         - 使用 \c Get_Adc(1) 获取通道 1 的 12 位结果（0～4095）
 *         - 计算公式基于 3.3V 参考电压和官方灵敏度推导
 *         - 通过 \c printf 输出调试信息（实际部署时可移除）
 *
 * @warning 必须保证 LED 周期为 10ms，否则影响精度或损坏传感器。
 */
void GetGP2Y ( void ) 
{
    
    // Step 1: 点亮 LED（低电平有效）
    GP2Y_Low;

    // Step 2: 等待 280μs（LED 开启后光路稳定所需时间）
    delay_ms(1);        // 先延时 1ms（覆盖启动抖动，保守设计）
    delay_us(280);      // 精确等待至采样点

    // Step 3: 读取 ADC 值（通道 1）
    adc_value = Get_Adc(1);  

    // Step 4: 关闭 LED（拉高）
    delay_us (40);     // 保持采样窗口
    GP2Y_High;

    // Step 5: 补足 10ms 周期（已用 ≈1ms+280μs+40μs ≈ 1320μs，剩余 ≈8680μs）
    // 此处使用多次 delay_us 避免单次大延时不准（部分 delay 实现有上限）
    delay_us ( 1500 );
    delay_us ( 1500 );
    delay_us ( 1500 );
    delay_us ( 1500 );
    delay_us ( 1500 );
    delay_us ( 1500 );
    delay_us ( 680 );   // 总计：1500*6 + 680 = 9680μs → 总周期 ≈10ms

    printf("AD value: %d\n", adc_value);

    // Step 6: 将 ADC 值转换为 PM2.5 浓度（mg/m³）
    // 公式推导：
    //   Vout (mV) = adc_value * (3300 mV) / 4096
    //   根据手册：Vout = 0.5V + Sensitivity * Concentration
    //   灵敏度 ≈ 0.5V per 0.1 mg/m³ → 5V/(mg/m³)
    //   故 Concentration (mg/m³) = (Vout - 0.5) / 5
    //   代入得：PM2_5 = (adc_value * 3300 / 4096 - 500) / 5000
    //   简化后常用工程公式：PM2_5 = (adc_value * 3.3 * 1000 / 4096 * 5 - 3) / 34
    //   （此公式为经验校准式，适用于多数 DIY 场景）
    PM2_5 = (adc_value * 3.3 * 1000.0 / 4096.0 * 5.0 - 3.0) / 34.0;

    printf ( "PM2.5: %f mg/m3\r\n", PM2_5);
}