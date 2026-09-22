/**
 * @file    fan.h
 * @brief   4路DC4010 12V风扇PWM驱动
 * @note    风扇引脚:
 *          风扇1: PA1 (TIM2_CH2)
 *          风扇2: PB1 (TIM3_CH4)
 *          风扇3: PB8 (TIM4_CH3)
 *          风扇4: PB9 (TIM4_CH4)
 *          PWM频率: 25kHz
 *          风扇一端接12V, 另一端接MOSFET漏极(IRLZ44N), MOSFET源极接GND
 */
#ifndef __FAN_H
#define __FAN_H

#include <stm32f10x.h>
#include <stdint.h>

/**
 * @brief 初始化4路风扇PWM定时器
 *        TIM2_CH2(PA1), TIM3_CH4(PB1), TIM4_CH3(PB8), TIM4_CH4(PB9)
 *        25kHz PWM, 初始占空比0%
 */
void Fan_Init(void);

/**
 * @brief 设置指定风扇转速
 * @param ch   风扇通道 0~3
 * @param pct  转速百分比 0~100
 */
void Fan_SetSpeed(uint8_t ch, uint8_t pct);

/**
 * @brief 设置全部风扇转速
 * @param pct 转速百分比 0~100
 */
void Fan_SetAll(uint8_t pct);

/**
 * @brief 设置指定风扇转速 (先全速启动 kick_ms 毫秒, 再降到 pct)
 * @param ch      风扇通道 0~3
 * @param pct     目标转速 0~100
 * @param kick_ms 全速启动持续时间(ms), 建议 200~500
 * @note  2线直流风扇在PWM低占空比下可能无法启动, 用此函数保证起转
 */
void Fan_SetSpeedKick(uint8_t ch, uint8_t pct, uint16_t kick_ms);

/**
 * @brief 设置全部风扇转速 (带全速启动)
 * @param pct     目标转速 0~100
 * @param kick_ms 全速启动持续时间(ms)
 */
void Fan_SetAllKick(uint8_t pct, uint16_t kick_ms);

/**
 * @brief 单路全速测试 (排查接线/驱动级用): 该路100%运行 ms 毫秒后停止
 * @param ch 风扇通道 0~3
 * @param ms 持续时间(ms)
 */
void Fan_TestSingle(uint8_t ch, uint16_t ms);

/* ---------------- 引脚静态电平测试 (排除PWM因素) ----------------
 * 用法: Fan_DirectMode(); Fan_DirectSet(0..3, 1 或 0); ...; Fan_PwmMode();
 * 若静态高电平能转、PWM不转 => PWM频率或极性(FAN_ACTIVE_LOW)不匹配;
 * 若静态高/低都不转 => 驱动级或接线问题。
 */

/** @brief 4路引脚切为普通推挽输出, 定时器停止 */
void Fan_DirectMode(void);

/** @brief 设定某路引脚静态电平 (需先 Fan_DirectMode): high=1高电平, 0低电平 */
void Fan_DirectSet(uint8_t ch, uint8_t high);

/** @brief 恢复正常PWM输出 */
void Fan_PwmMode(void);

/**
 * @brief 关闭全部风扇
 */
void Fan_AllOff(void);

/**
 * @brief 获取风扇转速
 * @param ch 风扇通道 0~3
 * @return 转速百分比 0~100
 */
uint8_t Fan_GetSpeed(uint8_t ch);

#endif /* __FAN_H */
