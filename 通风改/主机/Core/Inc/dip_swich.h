#ifndef __DIP_SWICH_H__
#define __DIP_SWICH_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* ===== 拨码开关引脚定义 =====
 * 2 位拨码，独立 IO 读取，接法：一端接 GND，MCU 内部上拉。
 *   bit0(最低位) = PA7
 *   bit1         = PA6
 *   PB0/PB1 保留给雨滴检测。
 * 拨 ON  = 接地 = 硬件读 0；本模块已取反，DIP_Switch_Read 返回「ON=1、OFF=0」的逻辑值。
 */
#define DIP_BIT0_Pin        GPIO_PIN_7
#define DIP_BIT0_GPIO_Port  GPIOA
#define DIP_BIT1_Pin        GPIO_PIN_6
#define DIP_BIT1_GPIO_Port  GPIOA

void DIP_Switch_Init(void);
uint8_t DIP_Switch_Read(void);

#ifdef __cplusplus
}
#endif

#endif /* __DIP_SWICH_H__ */
