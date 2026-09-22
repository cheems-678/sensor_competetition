/**
 * @file    dwt_us.h
 * @brief   Cortex-M3 DWT 周期计数器 —— 与 CMSIS 头文件版本无关的访问宏
 *
 * 为什么需要这个文件:
 *   本工程使用的 core_cm3.h 只导出了 NVIC / SCB / SysTick / ITM / CoreDebug
 *   五个内核外设结构体, 既没有 DWT_Type / DWT / DWT_BASE, 也没有不带 _Msk
 *   后缀的 CoreDebug_DEMCR_TRCENA 宏。因此直接书写
 *       DWT->CYCCNT = 0;
 *       CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA;
 *   会报 "use of undeclared identifier 'DWT'" 以及
 *      "...'CoreDebug_DEMCR_TRCENA'" 的编译错误。
 *   这里改用芯片手册规定的固定绝对地址自行访问, 不依赖 CMSIS 是否导出这些
 *   符号, 也不需要改动 stdlib 目录(标准外设库保持原样)。
 *
 * 寄存器 (Cortex-M3 技术参考手册, 地址固定不变):
 *   DEMCR       0xE000EDFC   bit24  TRCENA      使能跟踪与调试模块
 *   DWT_CTRL    0xE0001000   bit0   CYCCNTENA   使能周期计数器
 *   DWT_CYCCNT  0xE0001004   32位周期计数, 溢出自动回绕
 *
 * 用法:
 *   DWT_US_ENABLE();                        // 使能(清零后开始计数)
 *   uint32_t t0 = DWT_US_CYCLES();          // 取时间戳
 *   ...
 *   uint32_t dt = DWT_US_CYCLES() - t0;     // 差值, 无符号回绕安全
 *   微秒数 = dt / (SYS_CLK / 1000000UL)
 *
 * 说明: DWT 需要调试单元的 TRCENA 置位。Cortex-M3 允许软件直接写 DEMCR,
 *       因此不接调试器时置位同样有效。若芯片/配置下确实不可用, 上层的
 *       自检(空转前后比较计数)会把 dwt_ok 置 0, 驱动自动改用其他计时方式。
 */

#ifndef DWT_US_H
#define DWT_US_H

#include <stdint.h>

#define DWT_US_DEMCR     (*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_US_CTRL      (*(volatile uint32_t *)0xE0001000UL)
#define DWT_US_CYCCNT    (*(volatile uint32_t *)0xE0001004UL)

#define DWT_US_DEMCR_TRCENA     (1UL << 24)
#define DWT_US_CTRL_CYCCNTENA   (1UL << 0)

/** @brief 使能 DWT 周期计数器 (先清零再启动) */
#define DWT_US_ENABLE()   do { DWT_US_DEMCR |= DWT_US_DEMCR_TRCENA; DWT_US_CYCCNT = 0UL; DWT_US_CTRL |= DWT_US_CTRL_CYCCNTENA; } while (0)

/** @brief 读当前周期计数 (32位无符号, 溢出自动回绕, 差值比较安全) */
#define DWT_US_CYCLES()   (DWT_US_CYCCNT)

#endif /* DWT_US_H */
