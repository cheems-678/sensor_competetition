/**
 ******************************************************************************
 * @file    can.c
 * @brief   STM32 CAN 总线驱动实现
 *
 * 本文件实现了基于 STM32F10x 标准外设库的 CAN 控制器初始化、
 * 数据发送与接收功能。支持标准帧格式，可配置波特率，
 * 并可选启用 RX0 中断接收。
 *
 * @note    仅适用于 STM32F1 系列，使用 CAN1 控制器，
 *          引脚：PA11 (CAN_RX), PA12 (CAN_TX)
 ******************************************************************************
 */
#include "can.h"
#include "led.h"
#include "delay.h"
#include "usart.h"

/**
 * @brief  初始化 CAN 控制器
 * @param  tsjw: 重同步跳转宽度 (CAN_SJW_xxx)
 * @param  tbs2: 时间段 2 (CAN_BS2_xxx)
 * @param  tbs1: 时间段 1 (CAN_BS1_xxx)
 * @param  brp: 波特率预分频值（1～1024）
 * @param  mode: CAN 工作模式（如 CAN_Mode_Normal, CAN_Mode_LoopBack）
 * @retval 0 表示初始化成功（当前始终返回 0）
 *
 * 配置 GPIO（PA11/PA12）、CAN 时序参数、滤波器（接收所有 ID），
 * 并可选配置 RX0 中断（需定义 CAN_RX0_INT_ENABLE）。
 */
u8 CAN_Mode_Init(u8 tsjw, u8 tbs2, u8 tbs1, u16 brp, u8 mode)
{ 
    GPIO_InitTypeDef        GPIO_InitStructure; 
    CAN_InitTypeDef         CAN_InitStructure;
    CAN_FilterInitTypeDef   CAN_FilterInitStructure;
#if CAN_RX0_INT_ENABLE 
    NVIC_InitTypeDef        NVIC_InitStructure;
#endif

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);         
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    // 配置 CAN_TX (PA12) 为复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	
    GPIO_Init(GPIOA, &GPIO_InitStructure);			

    // 配置 CAN_RX (PA11) 为上拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;	
    GPIO_Init(GPIOA, &GPIO_InitStructure);			

    // CAN 初始化结构体配置
    CAN_InitStructure.CAN_TTCM = DISABLE;     // 关闭时间触发通信模式
    CAN_InitStructure.CAN_ABOM = DISABLE;     // 自动离线管理关闭
    CAN_InitStructure.CAN_AWUM = DISABLE;     // 自动唤醒关闭
    CAN_InitStructure.CAN_NART = ENABLE;      // 禁止自动重传（发送失败不重试）
    CAN_InitStructure.CAN_RFLM = DISABLE;     // 接收 FIFO 锁定模式关闭
    CAN_InitStructure.CAN_TXFP = DISABLE;     // 发送优先级由标识符决定
    CAN_InitStructure.CAN_Mode = mode;        

    CAN_InitStructure.CAN_SJW = tsjw;        
    CAN_InitStructure.CAN_BS1 = tbs1;        
    CAN_InitStructure.CAN_BS2 = tbs2;        
    CAN_InitStructure.CAN_Prescaler = brp;   
    CAN_Init(CAN1, &CAN_InitStructure);      

    // 配置接收滤波器：接收所有标准帧（ID=0，掩码=0）
    CAN_FilterInitStructure.CAN_FilterNumber = 0;                      
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;    
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;   
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000;                 
    CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;                  
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000;             
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;              
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;             

    CAN_FilterInit(&CAN_FilterInitStructure);                          

#if CAN_RX0_INT_ENABLE 
    // 使能 FIFO0 消息挂起中断
    CAN_ITConfig(CAN1, CAN_IT_FMP0, ENABLE);                       

    // 配置 NVIC：USB_LP_CAN1_RX0_IRQn
    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;     
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;            
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
#endif
    return 0;
}   
 
#if CAN_RX0_INT_ENABLE	
 
/**
 * @brief  CAN1 RX0 中断服务函数
 *
 * 当 CAN FIFO0 收到新消息时触发。
 * 本实现仅翻转 LED1 作为接收指示，实际数据处理可在此扩展。
 *
 * @note   若需读取数据，请取消注释 printf 调试代码，
 *         或将 RxMessage.Data 传递给应用层处理。
 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    CanRxMsg RxMessage;
    int i = 0;
    CAN_Receive(CAN1, 0, &RxMessage);
    // for(i = 0; i < 8; i++)
    //     printf("rxbuf[%d]:%d\r\n", i, RxMessage.Data[i]);
    LED1 = !LED1;  // 接收指示灯
}
#endif


/**
 * @brief  通过 CAN1 发送数据
 * @param  msg: 指向待发送数据缓冲区的指针（最多 8 字节）
 * @param  len: 发送数据长度（1～8）
 * @retval 0: 发送成功
 * @retval 1: 发送超时失败
 *
 * 使用标准帧 ID 0x12 发送数据。若发送邮箱长时间未释放（>0xFFF 循环），
 * 则判定为失败。
 */
u8 Can_Send_Msg(u8* msg, u8 len)
{	
    u8 mbox;
    u16 i = 0;
    CanTxMsg TxMessage;

    TxMessage.StdId = 0x12;           // 标准标识符
    TxMessage.ExtId = 0x12;           // 扩展标识符（未使用）
    TxMessage.IDE = CAN_Id_Standard;  // 使用标准帧
    TxMessage.RTR = CAN_RTR_Data;     // 数据帧（非远程帧）
    TxMessage.DLC = len;              // 数据长度
    for(i = 0; i < len; i++)
        TxMessage.Data[i] = msg[i];               

    mbox = CAN_Transmit(CAN1, &TxMessage);   

    // 等待发送完成或超时
    i = 0; 
    while((CAN_TransmitStatus(CAN1, mbox) == CAN_TxStatus_Failed) && (i < 0XFFF))
        i++;	

    if(i >= 0XFFF)
        return 1;  // 超时失败
    return 0;      // 成功
}

/**
 * @brief  从 CAN1 FIFO0 接收数据
 * @param  buf: 接收缓冲区（至少 8 字节）
 * @retval >0: 实际接收到的数据长度（DLC）
 * @retval 0: 无数据可接收
 *
 * 仅读取 FIFO0 中的第一条消息，最多拷贝 8 字节到 buf。
 * @note 调用前建议检查 CAN_MessagePending()，但本函数已内置检查。
 */
u8 Can_Receive_Msg(u8 *buf)
{		   		   
    u32 i;
    CanRxMsg RxMessage;

    if(CAN_MessagePending(CAN1, CAN_FIFO0) == 0)
        return 0;  // 无消息

    CAN_Receive(CAN1, CAN_FIFO0, &RxMessage);

    for(i = 0; i < 8; i++)
        buf[i] = RxMessage.Data[i];  

    return RxMessage.DLC;  // 返回数据长度
}