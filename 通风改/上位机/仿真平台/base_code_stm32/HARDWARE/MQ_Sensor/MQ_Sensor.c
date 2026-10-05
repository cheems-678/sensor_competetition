/**
 ******************************************************************************
 * @file    MQ_Sensor.c
 * @brief   多种 MQ 系列气体传感器数据采集与浓度换算驱动
 *
 * 支持以下传感器类型：
 * - MQ-8：氢气（H₂）
 * - MQ-135：空气质量（CO₂、NH₃、NOx、苯等）
 * - MQ-136 / MQ-137 / MQ-138：硫化氢（H₂S）、氨气（NH₃）、丙酮（PA）等
 *
 * @note    - 所有传感器共用同一 ADC 通道（通道1）
 *          - 输出值为模拟电压（V），内部计算 ppm 仅用于调试打印
 *          - 换算公式基于典型灵敏度曲线拟合，实际精度受环境温湿度、校准状态影响
 *          - 使用前需确保传感器已预热（通常 >60 秒）
 ******************************************************************************
 */

/**
 * @brief  存储最近一次 ADC 采样原始值（12位，0～4095）
 *
 * 该变量为静态局部变量，仅供本文件内函数使用。
 */
#include "MQ_Sensor.h"


/**
 * @brief  获取 MQ-8 传感器输出电压（氢气 H₂）
 * @return float 传感器分压输出电压（单位：V），范围约 0.0 ～ 3.3V
 *
 * @details
 * - 内部执行 20ms 延时以稳定读数
 * - 通过 ADC 通道1 采集原始值，并转换为电压
 * - 使用经验公式估算 H₂ 浓度（ppm），仅用于串口调试输出
 * - 实际返回值为 **电压值**，非 ppm
 *
 * @note   公式来源（典型拟合）：
 *         \f$ V_{out} = \frac{3.65 \cdot (ppm)^{0.6406}}{34.88 + (ppm)^{0.6406}} + 0.6 \f$
 *         反解得：
 *         \f$ ppm = 10^{\frac{ \log_{10}\left( \frac{34.88 \cdot (V_{out} - 0.6)}{3.65 - (V_{out} - 0.6)} \right) }{0.6406}} \f$
 *
 * @warning 返回值为电压，若需 ppm 值，应修改函数返回类型或新增接口。
 */
static unsigned int adc_value = 0; //保存ADC数值

//MQ8数据获取
float GetMQ8Out(void)
{
    double volt = 0;
    double ppm = 0;
    
    delay_ms(20);
    
    //adc数值
    adc_value = Get_Adc(1);
    printf("adcvalue = %d.\n",adc_value);

    
    //adc输出电压
    volt=(double)adc_value*3.3/4095;
    printf("volt = %f.\n",volt);
    
    //根据电压值换算成被检测气体的污染指数

    // MQ135 MQ8 H2 空气质量 通用 
    // volt = ((3.65*pow(ppm,0.3203*2))/(34.88+pow(ppm,0.3203*2))+0.6);
    ppm = pow(10, (log10(34.88*(volt-0.6)/(3.65-volt+0.6)))/(0.3203*2));

    printf("ppm = %f.\n\n",ppm);
    return volt;
}

/**
 * @brief  获取 MQ-135 传感器输出电压（空气质量）
 * @return float 传感器分压输出电压（单位：V）
 *
 * @details
 * - 功能与 \ref GetMQ8Out 类似，但针对 MQ-135 的灵敏度特性
 * - 可检测 CO₂、NH₃、苯、酒精等多种气体（混合响应）
 * - 同样返回电压，ppm 为估算值（以 CO₂ 为参考）
 *
 * @note   使用与 MQ-8 相同的经验公式，适用于通用空气质量评估。
 */
float GetMQ135Out(void)
{
    double volt = 0;
    double ppm = 0;
    
    delay_ms(20);
    
    //adc数值
    adc_value = Get_Adc(1);
    printf("adcvalue = %d.\n",adc_value);

    
    //adc输出电压
    volt=(double)adc_value*3.3/4095;
    printf("volt = %f.\n",volt);
    
    //根据电压值换算成被检测气体的污染指数

    // MQ135 MQ8 H2 空气质量 通用 
    // volt = ((3.65*pow(ppm,0.3203*2))/(34.88+pow(ppm,0.3203*2))+0.6);
    ppm = pow(10, (log10(34.88*(volt-0.6)/(3.65-volt+0.6)))/(0.3203*2));

    printf("ppm = %f.\n\n",ppm);
    return volt;
}

/**
 * @brief  获取 MQ-136 传感器输出电压（H₂S / NH₃ / 丙酮）
 * @return float 传感器分压输出电压（单位：V）
 *
 * @details
 * - 针对 MQ-136/137/138 系列优化的换算公式
 * - 公式中引入系数 4，反映其对特定气体的更高灵敏度
 *
 * @note   公式来源：
 *         \f$ V_{out} = \frac{2.3 \cdot (4 \cdot ppm)^{0.6406}}{34.88 + (4 \cdot ppm)^{0.6406}} + 0.6 \f$
 *         反解后除以 4 得到 ppm。
 */
float GetMQ136Out(void)
{
    double volt = 0;
    double ppm = 0;
    
    delay_ms(20);
    
    //adc数值
    adc_value = Get_Adc(1);
    printf("adcvalue = %d.\n",adc_value);

    
    //adc输出电压
    volt=(double)adc_value*3.3/4095;
    printf("volt = %f.\n",volt);
    
    //根据电压值换算成被检测气体的污染指数
    //MQ136~138 H2S NH3 PA 通用
    // volt = ((2.3*pow(4*ppm,0.3203*2))/(34.88+pow(4*ppm,0.3203*2))+0.6); 
    ppm = (pow(10, (log10(34.88*(volt-0.6)/(2.3-volt+0.6)))/(0.3203*2)))/4;


    printf("ppm = %f.\n\n",ppm);
    return volt;
}

/**
 * @brief  获取 MQ-137 传感器输出电压（H₂S / NH₃ / 丙酮）
 * @return float 传感器分压输出电压（单位：V）
 *
 * @copydoc GetMQ136Out
 */
float GetMQ137Out(void)
{
    double volt = 0;
    double ppm = 0;
    
    delay_ms(20);
    
    //adc数值
    adc_value = Get_Adc(1);
    printf("adcvalue = %d.\n",adc_value);

    
    //adc输出电压
    volt=(double)adc_value*3.3/4095;
    printf("volt = %f.\n",volt);
    
    //根据电压值换算成被检测气体的污染指数
    //MQ136~138 H2S NH3 PA 通用
    // volt = ((2.3*pow(4*ppm,0.3203*2))/(34.88+pow(4*ppm,0.3203*2))+0.6); 
    ppm = (pow(10, (log10(34.88*(volt-0.6)/(2.3-volt+0.6)))/(0.3203*2)))/4;


    printf("ppm = %f.\n\n",ppm);
    return volt;
}

/**
 * @brief  获取 MQ-138 传感器输出电压（H₂S / NH₃ / 丙酮）
 * @return float 传感器分压输出电压（单位：V）
 *
 * @copydoc GetMQ136Out
 */
float GetMQ138Out(void)
{
    double volt = 0;
    double ppm = 0;
    
    delay_ms(20);
    
    //adc数值
    adc_value = Get_Adc(1);
    printf("adcvalue = %d.\n",adc_value);

    
    //adc输出电压
    volt=(double)adc_value*3.3/4095;
    printf("volt = %f.\n",volt);
    
    //根据电压值换算成被检测气体的污染指数
    //MQ136~138 H2S NH3 PA 通用
    // volt = ((2.3*pow(4*ppm,0.3203*2))/(34.88+pow(4*ppm,0.3203*2))+0.6); 
    ppm = (pow(10, (log10(34.88*(volt-0.6)/(2.3-volt+0.6)))/(0.3203*2)))/4;


    printf("ppm = %f.\n\n",ppm);
    return volt;
}