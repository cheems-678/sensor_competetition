/**
 ******************************************************************************
 * @file    can.h
 * @brief   STM32 CAN 总线驱动头文件
 *
 * 声明 CAN 初始化、发送与接收接口。
 * 支持标准帧通信，使用 CAN1 控制器（PA11/PA12）。
 * 可通过宏 \c CAN_RX0_INT_ENABLE 启用接收中断。
 ******************************************************************************
 */

#ifndef __CAN_H
#define __CAN_H

#include "sys.h"

/**
 * @brief  启用/禁用 CAN RX0 中断
 * 
 * - 设置为 1：启用 USB_LP_CAN1_RX0_IRQn 中断，自动处理接收
 * - 设置为 0：关闭中断，需轮询调用 Can_Receive_Msg()
 * 
 * @note 修改后需重新编译整个项目。
 */
#define CAN_RX0_INT_ENABLE	0									    

/**
 * @brief  初始化 CAN 控制器
 * @param  tsjw: 重同步跳转宽度，取值范围见 \c CAN_SJW_xxx（如 CAN_SJW_1tq）
 * @param  tbs2: 时间段2，取值范围见 \c CAN_BS2_xxx（如 CAN_BS2_2tq）
 * @param  tbs1: 时间段1，取值范围见 \c CAN_BS1_xxx（如 CAN_BS1_3tq）
 * @param  brp: 波特率预分频值（1～1024），实际波特率 = f<sub>PCLK</sub> / (brp × (1 + tbs1 + tbs2))
 * @param  mode: 工作模式，如 \c CAN_Mode_Normal、\c CAN_Mode_LoopBack 等
 * @retval 0 表示成功（当前实现始终返回 0）
 * 
 * @see CAN_Mode_Init() 实现在 \ref can.c
 */
u8 CAN_Mode_Init(u8 tsjw, u8 tbs2, u8 tbs1, u16 brp, u8 mode);

/**
 * @brief  通过 CAN1 发送数据帧
 * @param  msg: 指向待发送数据的缓冲区（最多 8 字节）
 * @param  len: 数据长度，有效范围 1～8
 * @retval 0: 发送成功
 * @retval 1: 发送超时失败（邮箱长时间未释放）
 * 
 * 使用标准标识符 \c 0x12 发送数据帧。
 * @note 若需自定义 ID，请修改 \ref can.c 中 \c Can_Send_Msg() 的 \c StdId。
 */
u8 Can_Send_Msg(u8* msg, u8 len);

/**
 * @brief  从 CAN FIFO0 接收数据
 * @param  buf: 接收缓冲区（调用者需确保至少 8 字节空间）
 * @retval >0: 实际接收到的数据字节数（DLC，最大 8）
 * @retval 0: FIFO 中无消息
 * 
 * 仅读取 FIFO0 的第一条消息，并拷贝全部 8 字节数据（即使 DLC < 8）。
 * @warning 调用频率应高于消息到达速率，避免 FIFO 溢出丢失数据。
 */
u8 Can_Receive_Msg(u8 *buf);

#endif /* __CAN_H */