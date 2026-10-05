/**
 * @file    adc.c
 * @brief   STM32F10x ADC 驱动实现（支持单通道查询 + 多通道 DMA 循环采集）
 *
 * 本文件提供两种 ADC 使用模式：
 * 1. **单通道软件触发模式**（Adc_Init + Get_Adc）  
 *    - 适用于低频、按需采样场景（如电池电压检测）
 *    - 默认使用 PA6（ADC_Channel_6）
 *
 * 2. **5 通道 DMA 循环采集模式**（ADC1_DMA_Config）  
 *    - 适用于多路传感器同步采集（如温度、光敏、电位器等）
 *    - 使用 PA0～PA4（ADC_Channel_0～4），DMA 自动填充缓冲区
 *
 * @note
 * - ADC 时钟 ≤ 14 MHz（PCLK2=72MHz 时，分频 ≥ 6）
 * - 单通道模式采样时间建议 ≥ 7.5 cycles（当前为 1.5 cycles，仅适用于低阻抗源）
 * - DMA 模式使用循环缓冲（Circular Mode），数据持续更新
 *
 * @warning 若同时使用两种模式，请确保只初始化一种，避免资源冲突。
 */
 #include "adc.h"
 #include "delay.h"
	   

/** @brief 存放 ADC 5 通道 DMA 采集结果（PA0～PA4） */
volatile unsigned short int ADC_ConvertedValue[5] ;

/*------------------ 中断配置 ------------------*/

/**
 * @brief 配置 ADC1/2 中断优先级
 * @details 用于 ADC 转换完成中断（EOC），当前未启用中断处理
 */
void Adc_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure; 
    
    NVIC_InitStructure.NVIC_IRQChannel = ADC1_2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    
    NVIC_Init(&NVIC_InitStructure);
}

/*------------------ 单通道 ADC 初始化（软件触发） ------------------*/

/**
 * @brief 初始化 ADC1 为单通道连续转换模式（默认通道：PA6 / ADC_Channel_6）
 * @details
 * - 工作模式：独立模式、单通道、连续转换、软件启动
 * - 数据右对齐，无外部触发
 * - 自动执行校准
 * @note 此模式不启用 DMA 或中断，需调用 Get_Adc() 主动读取
 */																   
void  Adc_Init(void)
{ 	
	ADC_InitTypeDef ADC_InitStructure; 
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |RCC_APB2Periph_ADC1	, ENABLE );	  //使能ADC1通道时钟
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);   //设置ADC分频因子6 72M/6=12,ADC最大时间不能超过14M

	//PA6 作为模拟通道输入引脚                       
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;		//模拟输入引脚
	GPIO_Init(GPIOA, &GPIO_InitStructure);	

	ADC_DeInit(ADC1);  //复位ADC1 

	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;//ADC工作模式
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;	//模数转换工作在单通道模式
	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;	//模数转换工作在多次转换模式
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	//转换由软件而不是外部触发启动
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	//ADC数据右对齐
	ADC_InitStructure.ADC_NbrOfChannel = 1;	//顺序进行规则转换的ADC通道的数目
	ADC_Init(ADC1, &ADC_InitStructure);	//根据ADC_InitStruct中指定的参数初始化外设ADCx的寄存器

  
	ADC_Cmd(ADC1, ENABLE);//使能指定的ADC1
	
	ADC_ResetCalibration(ADC1);	//使能复位校准
	 
	while(ADC_GetResetCalibrationStatus(ADC1));	//等待复位校准结束
	
	ADC_StartCalibration(ADC1);	//开启AD校准
 
	while(ADC_GetCalibrationStatus(ADC1));	//等待校准结束
	
	
	//ADC_ITConfig(ADC1, ADC_IT_EOC, ENABLE);
	//ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 1, ADC_SampleTime_1Cycles5);
    //ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}				  

/**
 * @brief 获取指定 ADC 通道的单次转换值
 * @param ch ADC 通道号（如 ADC_Channel_6）
 * @return 12 位 ADC 转换结果（0～4095）
 * @note
 * - 每次调用会重新配置通道并启动一次转换
 * - 采样时间设为 1.5 cycles（仅适用于低输出阻抗信号源）
 * - 若信号源阻抗高，建议增加采样时间（如 ADC_SampleTime_55Cycles5）
 */
u16 Get_Adc(u8 ch)   
{
//设置指定ADC的规则组通道，一个序列，采样时间
	ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_1Cycles5 );//ADC1,ADC通道,采样时间为239.5周期		    
  
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);		//使能指定的ADC1的软件转换启动功能
	 
	while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC ));//等待转换结束

	return ADC_GetConversionValue(ADC1);	//返回最近一次ADC1规则组的转换结果
}

/**
 * @brief 对指定通道进行多次采样并返回平均值
 * @param ch ADC 通道号
 * @param times 采样次数
 * @return 平均 ADC 值
 * @note 每次采样间隔 1ms，适用于消除随机噪声
 */
u16 Get_Adc_Average(u8 ch,u8 times)
{
	u32 temp_val=0;
	u8 t;
	for(t=0;t<times;t++)
	{
		temp_val+=Get_Adc(ch);
		delay_ms(1);
	}
	return temp_val/times;
} 	 

/*------------------ ADC 中断服务函数 ------------------*/

/**
 * @brief ADC1/2 共用中断服务函数
 * @details 当前仅清除 EOC 标志，无实际处理逻辑
 * @note 若需在转换完成时执行操作，请在此添加代码
 */
void ADC1_2_IRQHandler(void)
{
    if (ADC_GetITStatus(ADC1, ADC_IT_EOC) == SET)
    {

    }
	ADC_ClearITPendingBit(ADC1, ADC_IT_EOC);
}


/*------------------ 多通道 DMA 循环采集模式 ------------------*/

/**
 * @brief 初始化 ADC1 + DMA1 实现 5 通道（PA0～PA4）循环采集
 * @details
 * - 通道：ADC_Channel_0 ～ Channel_4（对应 PA0～PA4）
 * - 扫描模式 + 连续转换 + DMA 循环模式
 * - 采样时间：1.5 cycles（可根据信号调整）
 * - 数据自动存入 ADC_ConvertedValue[5] 数组
 * @note
 * - 调用后 ADC 持续工作，无需手动触发
 * - 主程序可随时读取 ADC_ConvertedValue 数组获取最新值
 * - ADC 时钟设为 PCLK2/8 = 9MHz
 */
void ADC1_DMA_Config(void)
{
	DMA_InitTypeDef  DMA_InitStructure;
	ADC_InitTypeDef  ADC_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// 开启 DMA 时钟
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
	// 开启 ADC 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
	// 开启 GPIO 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	/* * * * * * ADC 引脚配置 * * * * * */
	

	// PA0/1/2/3/4 作为模拟通道输入引脚                         
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;		//模拟输入引脚
	GPIO_Init(GPIOA, &GPIO_InitStructure);	

		/* * * * * * DMA 模式配置 * * * * * */
	// 复位 DMA 控制器
	DMA_DeInit(DMA1_Channel1);
	
	// 源数据地址
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(ADC1->DR);
	// 目标地址
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)ADC_ConvertedValue;
	// 方向：外设到存储器
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	// 传输大小
	DMA_InitStructure.DMA_BufferSize = 5;
	// 外设地址递增
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	// 内存地址递增
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	// 外设数据单位
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	// 内存数据单位
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	// DMA模式，循环
	// DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
	// 优先级
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	// 关闭内存到内存的传输
	DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
	// 初始化DMA
	DMA_Init(DMA1_Channel1, &DMA_InitStructure);
	// 使能DMA
	DMA_Cmd(DMA1_Channel1, ENABLE);
	
	/* * * * * * ADC 模式配置 * * * * * */
	
	// ADC_DeInit(ADC1);  //将外设 ADC1 的全部寄存器重设为缺省值

	// 只使用一个ADC，属于独立模式
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;	
	// 扫描模式
	ADC_InitStructure.ADC_ScanConvMode = ENABLE;	
	// 连续转换模式
	ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;	
	// 转换由软件开启
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;	
	// ADC数据右对齐
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;	
	// 顺序进行规则转换的ADC通道的数目
	ADC_InitStructure.ADC_NbrOfChannel = 5;	
	// 根据ADC_InitStruct中指定的参数初始化外设ADCx的寄存器   
	ADC_Init(ADC1, &ADC_InitStructure);	

	// 配置ADC时钟Ｎ狿CLK2的8分频，即9MHz
	RCC_ADCCLKConfig(RCC_PCLK2_Div8); 
	
	ADC_RegularChannelConfig( ADC1, ADC_Channel_0, 1, ADC_SampleTime_1Cycles5);
	ADC_RegularChannelConfig( ADC1, ADC_Channel_1, 2, ADC_SampleTime_1Cycles5);
	ADC_RegularChannelConfig( ADC1, ADC_Channel_2, 3, ADC_SampleTime_1Cycles5);
	ADC_RegularChannelConfig( ADC1, ADC_Channel_3, 4, ADC_SampleTime_1Cycles5);
	ADC_RegularChannelConfig( ADC1, ADC_Channel_4, 5, ADC_SampleTime_1Cycles5);


	// 使能 ADC1 DMA 请求
	ADC_DMACmd(ADC1, ENABLE);	
	// 使能 ADC1， 开始转换
	ADC_Cmd(ADC1, ENABLE);
	
	ADC_ResetCalibration(ADC1);	//重置指定的ADC1的校准寄存器
	 
	while(ADC_GetResetCalibrationStatus(ADC1));	//获取ADC1重置校准寄存器的状态,设置状态则等待
	
	ADC_StartCalibration(ADC1);		//开始指定ADC1的校准状态
 
	while(ADC_GetCalibrationStatus(ADC1));		//获取指定ADC1的校准程序,设置状态则等待
 
	
	// ADC1的软件转换启动
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);		
}
