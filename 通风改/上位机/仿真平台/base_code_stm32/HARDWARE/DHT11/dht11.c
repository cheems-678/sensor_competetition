/**
 ******************************************************************************
 * @file    dht11.c
 * @brief   DHT11 温湿度传感器驱动实现
 *
 * 实现 DHT11 单总线通信协议，支持读取温度与湿度数据。
 * 使用 GPIO 模拟单总线时序，适用于 STM32F1 系列 MCU。
 *
 * @note    - DHT11 数据格式：5 字节 = [湿度整数][湿度小数][温度整数][温度小数][校验和]
 *          - 本驱动将湿度/温度 ×10 后以整数形式返回（如 25.3°C → 253）
 *          - 温度负值通过最高位（bit7）标识（DHT11 实际不支持负温，但部分模块或仿真可能扩展）
 *          - 默认使用 PA0 作为 DQ 引脚（可在 \c dht11.h 中修改）
 ******************************************************************************
 */
#include "dht11.h"
#include "delay.h"

      
/**
 * @brief  发送复位信号给 DHT11
 *
 * 主机拉低总线 ≥18ms，然后释放（拉高）20～40μs，触发 DHT11 响应。
 *
 * @note 此函数会临时将 DQ 配置为推挽输出模式。
 */
void DHT11_Rst(void)	   
{                 
	DHT11_IO_OUT(); 	//SET OUTPUT
    DHT11_DQ_OUT=0; 	//拉低DQ
    delay_ms(20);    	//拉低至少18ms
    DHT11_DQ_OUT=1; 	//DQ=1 
	delay_us(20);     	//主机拉高20~40us
}

/**
 * @brief  等待 DHT11 的响应信号
 * @retval 0: 检测到 DHT11 存在（收到正确 ACK）
 * @retval 1: 未检测到 DHT11（超时）
 *
 * DHT11 在收到复位信号后，会拉低总线 80μs，再拉高 80μs 作为响应。
 * 本函数检测这两个电平变化。
 */
u8 DHT11_Check(void) 	   
{   
	u8 retry=0;
	DHT11_IO_IN();//SET INPUT	 
    while (DHT11_DQ_IN&&retry<100)//DHT11会拉低40~80us
	{
		retry++;
		delay_us(1);
	};	 
	if(retry>=100)return 1;
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11拉低后会再次拉高40~80us
	{
		retry++;
		delay_us(1);
	};
	if(retry>=100)return 1;	    
	return 0;
}

/**
 * @brief  从 DHT11 读取 1 位数据
 * @retval 0 或 1: 读取到的数据位
 *
 * DHT11 通过高电平持续时间表示数据位：
 * - 26～28μs 高电平 → 0
 * - 70μs 高电平 → 1
 *
 * 本函数在跳变沿后延迟 40μs 采样，可区分两者。
 */
u8 DHT11_Read_Bit(void) 			 
{
 	u8 retry=0;
	while(DHT11_DQ_IN&&retry<100)//等待变为低电平
	{
		retry++;
		delay_us(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//等待变高电平
	{
		retry++;
		delay_us(1);
	}
	delay_us(40);//等待40us
	if(DHT11_DQ_IN)return 1;
	else return 0;		   
}

/**
 * @brief  从 DHT11 读取 1 字节数据
 * @retval 读取到的 8 位数据（MSB 先传）
 *
 * 连续调用 \ref DHT11_Read_Bit() 8 次，高位在前。
 */
u8 DHT11_Read_Byte(void)    
{        
    u8 i,dat;
    dat=0;
	for (i=0;i<8;i++) 
	{
   		dat<<=1; 
	    dat|=DHT11_Read_Bit();
    }						    
    return dat;
}

/**
 * @brief  从 DHT11 读取完整温湿度数据
 * @param[out] temp: 指向温度值的指针（单位：0.1°C，如 253 表示 25.3°C）
 * @param[out] humi: 指向湿度值的指针（单位：0.1%RH，如 602 表示 60.2%）
 * @retval 0: 读取成功且校验通过
 * @retval 1: 通信失败或校验错误
 *
 * @note   - DHT11 官方范围：湿度 20～90%RH，温度 0～50°C
 *         - 本实现支持负温扩展（若温度字节 bit7=1，则取反）
 *         - 数据格式：[湿整][湿小][温整][温小][校验和]
 */
u8 DHT11_Read_Data(int *temp,int *humi)    
{        
 	u8 buf[5];
	u8 i;
	DHT11_Rst();
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//读取5位数据
		{
			buf[i]=DHT11_Read_Byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])
		{
			*humi=buf[0]*10 + buf[1];
			if(buf[3] < 0x80) //温度大于0
				*temp=buf[2]*10 + buf[3];
			else
			{
				buf[3] -= 0x80;
				*temp=buf[2]*10 + buf[3];
				*temp = -*temp;
			}
		}
	}else return 1;
	return 0;	    
}

/**
 * @brief  初始化 DHT11 的 GPIO 并检测设备是否存在
 * @retval 0: DHT11 初始化成功且存在
 * @retval 1: 未检测到 DHT11
 *
 * 配置 DQ 引脚（默认 PA0）为推挽输出，并发送复位序列进行探测。
 */  	 
u8 DHT11_Init(void)
{	 
 	GPIO_InitTypeDef  GPIO_InitStructure;
 	
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	 //使能PA端口时钟
	
 	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;				 //PA0端口配置
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 		 //推挽输出
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOA, &GPIO_InitStructure);				 //初始化IO口
 	GPIO_SetBits(GPIOA,GPIO_Pin_0);						 		 //PA0 输出高
			    
	DHT11_Rst();  //复位DHT11
	return DHT11_Check();//等待DHT11的回应
} 







