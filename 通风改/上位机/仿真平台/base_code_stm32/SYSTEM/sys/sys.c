/**
 * @file    sys.c
 * @brief   STM32F10x 系统核心初始化函数
 *
 * 本文件提供系统级初始化功能，当前主要实现 NVIC 中断优先级分组配置。
 * - 使用 **NVIC_PriorityGroup_2**：2 位抢占优先级 + 2 位子优先级
 * - 支持最多 4 个抢占级别（0～3），每个级别内支持 4 个子优先级（0～3）
 *
 * @note
 * - 中断优先级分组 **只能设置一次**，通常在 main() 开头调用；
 * - 分组选择需根据项目中断复杂度权衡：
 *   - Group_0：无抢占，仅子优先级（不推荐）
 *   - Group_1：1 抢占位 + 3 子优先级位
 *   - Group_2：2+2（常用，平衡性好）
 *   - Group_3：3+1
 *   - Group_4：4 抢占位，无子优先级（高实时性场景）
 *
 * @warning
 * - 若使用 RTOS（如 FreeRTOS），其内核可能已设置分组，请勿重复配置；
 * - 修改分组后，所有已配置的中断优先级需重新评估是否符合新规则。
 */
#include "sys.h"

/*------------------ 系统中断分组配置 ------------------*/

/**
 * @brief 配置 NVIC 中断优先级分组
 * @details
 * 设置为 **NVIC_PriorityGroup_2**，即：
 * - 抢占优先级：2 位（取值 0～3）
 * - 子优先级（响应优先级）：2 位（取值 0～3）
 *
 * 示例：
 * - EXTI0_IRQn: Preemption=1, Sub=0 → 可被 Preemption=0 的中断打断
 * - USART1_IRQn: Preemption=1, Sub=1 → 与上同级，但响应稍晚
 *
 * @note
 * - 此函数应在系统启动早期（如 main() 开始处）调用一次；
 * - 不依赖任何外设时钟，可安全在 RCC 初始化前调用。
 */
void NVIC_Configuration(void)
{

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	//设置NVIC中断分组2:2位抢占优先级，2位响应优先级

}
