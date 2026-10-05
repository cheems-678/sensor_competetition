/**
 * @file    usart.c
 * @brief   STM32F10x 串口驱动：USART1（主调试口）与 USART2（辅助通信口）
 *
 * 本文件实现：
 * - **USART1**：用于调试输出（重定向 printf）和中断接收；
 * - **USART2**：通用串口通信（仅初始化，中断服务需用户扩展）；
 * - 支持标准库 `printf` 通过 USART1 输出（禁用半主机模式）。
 *
 * @note
 * - USART1 引脚：TX=PA9, RX=PA10；
 * - USART2 引脚：TX=PA2, RX=PA3；
 * - 接收使用全局缓冲区 `USART_RX_BUF` 和状态标记 `USART_RX_STA`（仅 USART1）；
 * - 当前 USART1 中断仅清除标志，**未处理数据接收逻辑**（需用户补充）。
 *
 * @warning
 * - 若使用 `printf`，确保主频 ≥ 8 MHz，否则高速波特率可能失真；
 * - 接收缓冲区无溢出保护，请确保上位机发送长度 ≤ `USART_REC_LEN - 1`；
 * - USART2 初始化未配置 NVIC，若需中断，需额外调用 NVIC 配置。
 */
#include "sys.h"
#include "usart.h"

/*------------------ 调试输出支持（重定向 printf） ------------------*/

#if defined (__GNUC__) || defined (__clang__)
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

// 禁用 ARM 半主机模式（Keil/ARMCC）
#if defined (__CC_ARM)
#pragma import(__use_no_semihosting)
#endif         

/**
 * @brief 标准库所需的 FILE 结构（用于 printf 重定向）
 */              
struct __FILE 
{ 
	int handle; 

}; 

/** 标准输出流 */
FILE __stdout;       

/**
 * @brief 系统退出函数（避免半主机调用）
 */
void _sys_exit(int x) 
{ 
	x = x; 
} 

/**
 * @brief 重定向 fputc 到 USART1，支持 printf
 * @param ch 要发送的字符
 * @param f  文件指针（忽略）
 * @return 发送的字符
 * @note 此函数被 printf 自动调用，阻塞直到发送完成。
 */
int fputc(int ch, FILE *f)
{      
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
    USART1->DR = (u8) ch;      
	return ch;
}

/**
 * @brief GCC 环境下重写_write()函数实现printf()函数的底层操作代码
 * @param ch 要发送的字符
 * @param f  文件指针（忽略）
 * @return 发送的字符
 * @note 此函数被 printf 自动调用，阻塞直到发送完成。
 */
int _write (int fd, char *pBuffer, int size)  
{  
    int i = 0;
	for (i = 0; i < size; i++)  
    {	// 等待上一个字符发送完成
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
		// 发送字符
        USART_SendData(USART1, (uint8_t) *pBuffer++);
    }  
    return size;  
}

/*------------------ 全局接收缓冲区（仅 USART1） ------------------*/

/** 接收缓冲区，最大 USART_REC_LEN 字节 */
u8 USART_RX_BUF[USART_REC_LEN];

/**
 * @brief 接收状态标记（位域定义）
 * - bit15: 接收完成标志（1=完成）
 * - bit14: 接收到 0x0D（回车）
 * - bit13～0: 已接收有效字节数
 */
u16 USART_RX_STA = 0; //接收状态标记

/*------------------ USART1 初始化 ------------------*/

/**
 * @brief 初始化 USART1（PA9-TX, PA10-RX）
 * @param bound 波特率（如 115200）
 * @details
 * - 配置 GPIO 为复用推挽（TX）和浮空输入（RX）；
 * - 使能接收中断（RXNE）；
 * - NVIC 配置：抢占优先级 3，子优先级 3；
 * - 支持 printf 重定向（通过 fputc）。
 * @note
 * - 接收中断服务函数需用户完善数据处理逻辑；
 * - 当前 ISR 仅清除中断标志，防止死循环。
 */
void uart_init(u32 bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE); //使能USART1，GPIOA时钟

	//USART1_TX   GPIOA.9
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; //复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);			//初始化GPIOA.9

	//USART1_RX	  GPIOA.10初始化
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;			  //PA10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; //浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);				  //初始化GPIOA.10

	//Usart1 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3; //抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		  //子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			  //IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);							  //根据指定的参数初始化VIC寄存器

	//USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;										//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;						//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;							//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;								//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					//收发模式

	USART_Init(USART1, &USART_InitStructure);	   //初始化串口1
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); //开启串口接受中断
	USART_Cmd(USART1, ENABLE);					   //使能串口1
}

/*------------------ USART2 初始化 ------------------*/

/**
 * @brief 初始化 USART2（PA2-TX, PA3-RX）
 * @param BaudRatePrescaler 波特率（如 9600）
 * @details
 * - 仅配置 GPIO 和 USART 寄存器；
 * - **未配置 NVIC 中断**（若需中断，需用户自行添加）；
 * - 默认无流控、8N1 格式。
 * @note
 * - USART2 时钟来自 APB1，确保系统配置正确；
 * - 如需接收中断，请在调用后配置 NVIC 并实现 USART2_IRQHandler。
 */
void Usart_Int2(uint32_t BaudRatePrescaler)
{
	GPIO_InitTypeDef GPIO_usartx;
	USART_InitTypeDef Usart_X;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	//USART1_TX   PA.2
	GPIO_usartx.GPIO_Pin = GPIO_Pin_2;
	GPIO_usartx.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_usartx.GPIO_Mode = GPIO_Mode_AF_PP; //复用推挽输出
	GPIO_Init(GPIOA, &GPIO_usartx);
	//USART1_RX	  PA.3
	GPIO_usartx.GPIO_Pin = GPIO_Pin_3;
	GPIO_usartx.GPIO_Mode = GPIO_Mode_IN_FLOATING; //浮空输入
	GPIO_Init(GPIOA, &GPIO_usartx);

	Usart_X.USART_BaudRate = BaudRatePrescaler;
	Usart_X.USART_WordLength = USART_WordLength_8b; //8位数据格式
	Usart_X.USART_StopBits = USART_StopBits_1;		//1位停止位
	Usart_X.USART_Parity = USART_Parity_No;
	Usart_X.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	Usart_X.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART2, &Usart_X);
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE); //开启接收中断
	USART_Cmd(USART2, ENABLE);
}

/*------------------ USART1 中断服务函数 ------------------*/

/**
 * @brief USART1 全局中断服务函数
 * @details
 * - 仅处理 RXNE（接收非空中断）；
 * - **当前仅清除中断标志，未读取 DR 或更新缓冲区**；
 * - 用户应在此处添加接收逻辑（如存入 USART_RX_BUF）。
 * @warning
 * - 必须读取 DR 或清除标志，否则会重复进入中断；
 * - 建议在 ISR 中读取 `USART_ReceiveData(USART1)` 避免数据丢失。
 */
void USART1_IRQHandler(void) //串口1中断服务程序
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) //接收中断
	{
	    USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
