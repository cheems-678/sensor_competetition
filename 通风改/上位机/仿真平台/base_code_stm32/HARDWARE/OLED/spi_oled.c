/**
 * @file spi_oled.c
 * @brief SPI 接口 OLED 显示屏驱动实现
 *
 * 本模块基于硬件 SPI1 驱动 OLED 显示屏（SSD1306/1106），提供初始化、显示控制、
 * 字符/字符串/汉字/位图显示等功能。
 *
 * 硬件连接说明：
 * - PA5（SCL/CLK）：SPI1 时钟
 * - PA7（SDA/MOSI）：SPI1 数据
 * - PB2（RES）：复位信号
 * - PB1（CS）：片选信号
 * - PB0（DC）：数据/命令选择
 *
 * 字体取模格式：阴码、列行式、逆向
 */

#include "spi_oled.h"
#include "oledfont.h"
#include "stdio.h"
#include "stdlib.h"
#include "delay.h"

/**
 * @brief 初始化 OLED 的 SPI 硬件接口
 *
 * 配置 SPI1 的 GPIO、模式、时钟分频等参数，使其适合驱动 OLED
 */
void SPI_OLED_Hardware_Init(void)
{
	SPI_InitTypeDef SPI_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	GPIO_SetBits(GPIOA, GPIO_Pin_5 | GPIO_Pin_7); // PA5/6/7上拉

	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SPI1, &SPI_InitStructure);
	// Enable SPIx
	SPI_Cmd(SPI1, ENABLE);
}

/**
 * @brief 设置 SPI1 波特率分频系数
 * @param SPI_BaudRatePrescaler 分频值（SPI_BaudRatePrescaler_2/8/16/256 等）
 *
 * 支持的分频系数及对应频率（SYSCLK=72MHz）：
 * - 2：36MHz、8：9MHz、16：4.5MHz、256：281.25kHz
 */
void SPI1_SetSpeed(uint8_t SPI_BaudRatePrescaler)
{
	assert_param(IS_SPI_BAUDRATE_PRESCALER(SPI_BaudRatePrescaler));
	SPI1->CR1 &= 0XFFC7;
	SPI1->CR1 |= SPI_BaudRatePrescaler; //设置SPI1速度
	SPI_Cmd(SPI1, ENABLE);
}

/**
 * @brief 通过 SPI1 发送一字节并接收返回值
 * @param TxData 要发送的字节
 * @return 接收到的字节，超时返回 0
 *
 * 函数阻塞等待发送缓存空及接收缓存非空，超时上限 200 次重试
 */
uint8_t SPI1_WriteByte(uint8_t TxData)
{
	uint8_t retry = 0;
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) //检查指定的SPI标志位设置与否:发送缓存空标志位
	{
		retry++;
		if (retry > 200)
			return 0;
	}
	SPI_I2S_SendData(SPI1, TxData); //通过外设SPIx发送一个数据
	retry = 0;

	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) //检查指定的SPI标志位设置与否:接受缓存非空标志位
	{
		retry++;
		if (retry > 200)
			return 0;
	}
	return SPI_I2S_ReceiveData(SPI1); //返回通过SPIx最近接收的数据
}

/**
 * @brief 向 SSD1306 写入一字节（命令或数据）
 * @param dat 数据/命令字节
 * @param cmd 模式标志：OLED_CMD（0 命令）或 OLED_DATA（1 数据）
 */
void SPI_OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
	//	uint8_t i;
	if (cmd)
		SPI_OLED_DC_Set();
	else
		SPI_OLED_DC_Clr();
	SPI_OLED_CS_Clr();
	SPI1_WriteByte(dat);

	SPI_OLED_CS_Set();
	SPI_OLED_DC_Set();
}

/**
 * @brief 设置 OLED 显示光标位置（字节寻址）
 * @param x 列地址（0-127）
 * @param y 页地址（0-7，每页对应 8 行像素）
 */
void SPI_OLED_Set_Pos(unsigned char x, unsigned char y)
{
	SPI_OLED_WR_Byte(0xb0 + y, OLED_CMD);
	SPI_OLED_WR_Byte((((x)&0xf0) >> 4) | 0x10, OLED_CMD);
	SPI_OLED_WR_Byte(((x)&0x0f), OLED_CMD);
}

/**
 * @brief 开启 OLED 显示（启用 DCDC 及显示）
 */
void SPI_OLED_Display_On(void)
{
	SPI_OLED_WR_Byte(0X8D, OLED_CMD); // SET DCDC命令
	SPI_OLED_WR_Byte(0X14, OLED_CMD); // DCDC ON
	SPI_OLED_WR_Byte(0XAF, OLED_CMD); // DISPLAY ON
}

/**
 * @brief 关闭 OLED 显示（禁用 DCDC 及显示）
 */
void SPI_OLED_Display_Off(void)
{
	SPI_OLED_WR_Byte(0X8D, OLED_CMD); // SET DCDC命令
	SPI_OLED_WR_Byte(0X10, OLED_CMD); // DCDC OFF
	SPI_OLED_WR_Byte(0XAE, OLED_CMD); // DISPLAY OFF
}

/**
 * @brief 清屏（全黑，填充 0x00）
 */
void SPI_OLED_Clear(void)
{
	uint8_t i, n;
	for (i = 0; i < 8; i++)
	{
		SPI_OLED_WR_Byte(0xb0 + i, OLED_CMD); //设置页地址（0~7）
		SPI_OLED_WR_Byte(0x00, OLED_CMD);	  //设置显示位置—列低地址
		SPI_OLED_WR_Byte(0x10, OLED_CMD);	  //设置显示位置—列高地址
		for (n = 0; n < 128; n++)
			SPI_OLED_WR_Byte(0, OLED_DATA);
	} //更新显示
}

/**
 * @brief 显示汉字
 * @param x 列地址（0-127）
 * @param y 页地址（0-7）
 * @param no 汉字在字库中的索引号
 */
void SPI_OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no)
{
	uint8_t t, adder = 0;
	SPI_OLED_Set_Pos(x, y);
	for (t = 0; t < 16; t++)
	{
		SPI_OLED_WR_Byte(Hzk[2 * no][t], OLED_DATA);
		adder += 1;
	}
	SPI_OLED_Set_Pos(x, y + 1);
	for (t = 0; t < 16; t++)
	{
		SPI_OLED_WR_Byte(Hzk[2 * no + 1][t], OLED_DATA);
		adder += 1;
	}
}

/**
 * @brief 绘制 BMP 位图（128×64 像素）
 * @param x0 起始列（0-127）
 * @param y0 起始页（0-7）
 * @param x1 结束列
 * @param y1 结束页
 * @param BMP[] 位图数据指针
 */
void SPI_OLED_DrawBMP(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char BMP[])
{
	unsigned int j = 0;
	unsigned char x, y;

	if (y1 % 8 == 0)
		y = y1 / 8;
	else
		y = y1 / 8 + 1;
	for (y = y0; y < y1; y++)
	{
		SPI_OLED_Set_Pos(x0, y);
		for (x = x0; x < x1; x++)
		{
			SPI_OLED_WR_Byte(BMP[j++], OLED_DATA);
		}
	}
}

/**
 * @brief 初始化 OLED 驱动（SPI、GPIO、显示参数）
 *
 * 完成复位、参数初始化、清屏等工作
 */
void SPI_OLED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	// PB0作为数据/指令信号引脚,PB1作为片选信号,PB2作为复位信号引脚
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); //使能A端口时钟
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  //推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; //速度50MHz
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2); // PB0,PB1,PB2输出高

	SPI_OLED_Hardware_Init();

	SPI_OLED_RST_Set();
	delay_ms(10);
	SPI_OLED_RST_Clr();
	delay_ms(20);
	SPI_OLED_RST_Set();

	SPI_OLED_WR_Byte(0xAE, OLED_CMD); //--turn off oled panel
	SPI_OLED_WR_Byte(0x02, OLED_CMD); //---set low column address
	SPI_OLED_WR_Byte(0x10, OLED_CMD); //---set high column address
	SPI_OLED_WR_Byte(0x40, OLED_CMD); //--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
	SPI_OLED_WR_Byte(0x81, OLED_CMD); //--set contrast control register
	SPI_OLED_WR_Byte(0xCF, OLED_CMD); // Set SEG Output Current Brightness
	SPI_OLED_WR_Byte(0xA1, OLED_CMD); //--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
	SPI_OLED_WR_Byte(0xC8, OLED_CMD); // Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
	SPI_OLED_WR_Byte(0xA6, OLED_CMD); //--set normal display
	SPI_OLED_WR_Byte(0xA8, OLED_CMD); //--set multiplex ratio(1 to 64)
	SPI_OLED_WR_Byte(0x3f, OLED_CMD); //--1/64 duty
	SPI_OLED_WR_Byte(0xD3, OLED_CMD); //-set display offset	Shift Mapping RAM Counter (0x00~0x3F)
	SPI_OLED_WR_Byte(0x00, OLED_CMD); //-not offset
	SPI_OLED_WR_Byte(0xd5, OLED_CMD); //--set display clock divide ratio/oscillator frequency
	SPI_OLED_WR_Byte(0x80, OLED_CMD); //--set divide ratio, Set Clock as 100 Frames/Sec
	SPI_OLED_WR_Byte(0xD9, OLED_CMD); //--set pre-charge period
	SPI_OLED_WR_Byte(0xF1, OLED_CMD); // Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
	SPI_OLED_WR_Byte(0xDA, OLED_CMD); //--set com pins hardware configuration
	SPI_OLED_WR_Byte(0x12, OLED_CMD);
	SPI_OLED_WR_Byte(0xDB, OLED_CMD); //--set vcomh
	SPI_OLED_WR_Byte(0x40, OLED_CMD); // Set VCOM Deselect Level
	SPI_OLED_WR_Byte(0x20, OLED_CMD); //-Set Page Addressing Mode (0x00/0x01/0x02)
	SPI_OLED_WR_Byte(0x02, OLED_CMD); //
	SPI_OLED_WR_Byte(0x8D, OLED_CMD); //--set Charge Pump enable/disable
	SPI_OLED_WR_Byte(0x14, OLED_CMD); //--set(0x10) disable
	SPI_OLED_WR_Byte(0xA4, OLED_CMD); // Disable Entire Display On (0xa4/0xa5)
	SPI_OLED_WR_Byte(0xA6, OLED_CMD); // Disable Inverse Display On (0xa6/a7)
	SPI_OLED_WR_Byte(0xAF, OLED_CMD); //--turn on oled panel

	SPI_OLED_WR_Byte(0xAF, OLED_CMD); /*display ON*/
	SPI_OLED_Clear();
	SPI_OLED_Set_Pos(0, 0);
}


/**
 * @brief 显示单个 ASCII 字符
 * @param x 列地址（0-127）
 * @param y 页地址（0-7，需为 8 的倍数）
 * @param num 字符 ASCII 码或索引值（0-94）
 *
 * 使用 ASCII_1608 字体库，每个字符 8×16 像素
 */
void SPI_OLED_ShowChar(uint16_t x,uint16_t y, uint8_t num)
{  
	uint8_t pos = 0 , t = 0;	  
	
	num=num-' ';					//得到字符偏移后的值

	//自上到下循环输入
	for(pos=0;pos<2;pos++)
	{
		SPI_OLED_Set_Pos(x,y+pos);	
		//自左到右循环输入
		for(t=0;t<8;t++)
		{
			SPI_OLED_WR_Byte(ASCII_1608[num][pos*8+t],OLED_DATA);
		}
	}	   	 	  
}

/**
 * @brief 显示 ASCII 字符串
 * @param x 起始列（0-127）
 * @param y 起始页（0-7）
 * @param p 字符串指针（以 '\0' 结尾）
 *
 * 自动换行：当超出屏幕宽度则移至下一行显示
 */
void SPI_OLED_ShowString(uint16_t x,uint16_t y,char *p)
{	 
	//判断是不是非法字符
	while((*p<='~')&&(*p>=' '))	
	{   
		//如果x，y坐标超出预设lcd屏大小则换行显示
		if(x>(128-8)) 
		{
			//显示靠前
			x=0;
			//显示换行
			y=(y+2)%8;	
		}
		if(y>(8-2))
		{
			y=0;
		}
		//循环显示字串符，坐标（x，y）,使用画笔色和背景色，尺寸和模式由参数决定
		SPI_OLED_ShowChar(x,y,*p);
		//写完一个字符，横坐标加size/2
		x+=8;
		//地址自增
		p++;
	}  
} 

/**
 * @brief 显示 32 位有符号整数
 * @param x 起始列
 * @param y 起始页
 * @param num 整数值（-2147483648 ~ 2147483647）
 * @param len 显示最少位数，不足用 0 补齐
 */
void SPI_OLED_ShowInt32Num(uint16_t x,uint16_t y, int32_t num, uint8_t len)
{		 	
	char show_len[8]={0},show_num[12]={0};
	uint8_t t = 0;
	
	if(len>32)						//len值过大则退出
		return;
	//len设置最少显示位数，例：输出 %6d 
	sprintf(show_len,"%%%dd",len);
	//代入字串符换算，最终完成num2char转换
	sprintf(show_num,show_len,num);
	
	while(*(show_num+t)!=0)			//循环判断是否为结束符
	{
		//显示字串符的数值
		SPI_OLED_ShowChar(x+(8)*t,y,show_num[t]);
		//指针偏移地址值自增
		t++;
	}
} 

/**
 * @brief 显示单个 16×16 点阵汉字
 * @param x 起始列
 * @param y 起始页
 * @param s 汉字指针（通常指向 GB2312 编码的汉字首字节）
 */
void SPI_OLED_DrawFont16(uint16_t x, uint16_t y, char *s)
{
	uint8_t x0 = 0, y0 = 0;
	uint16_t k = 0;
	uint16_t HZnum = 0;
	
	//自动统计汉字数目
	HZnum=sizeof(Tfont16)/sizeof(typFNT_GB16);	
	
	//循环寻找匹配的Index[2]成员值
	for (k=0;k<HZnum;k++)
	{
		//对应成员值匹配
		if((Tfont16[k].Index[0]==*(s))&&(Tfont16[k].Index[1]==*(s+1)))
		{ 	
			//x方向循环执行写16行，逐行式输入
			for(y0=0;y0<2;y0++)
			{
				SPI_OLED_Set_Pos(x,y+y0);
				//每行写入两个字节，自左到右
				for(x0=0;x0<16;x0++)
				{	
					//一次写入1字节
					SPI_OLED_WR_Byte(Tfont16[k].Msk[y0*16+x0],OLED_DATA);
				}
			}
		//查找到对应点阵关键字完成绘字后立即break退出for循环，防止多个汉字重复取模显示		
		break; 
		}
	}
}

/**
 * @brief 显示单个 32×32 点阵汉字
 * @param x 起始列
 * @param y 起始页
 * @param s 汉字指针
 */
void SPI_OLED_DrawFont32(uint16_t x, uint16_t y, char *s)
{
	uint8_t x0 = 0, y0 = 0;
	uint16_t k = 0;
	uint16_t HZnum = 0;
	
	//自动统计汉字数目
	HZnum=sizeof(Tfont32)/sizeof(typFNT_GB32);	
	
	//循环寻找匹配的Index[2]成员值
	for (k=0;k<HZnum;k++)
	{
		//对应成员值匹配
		if((Tfont16[k].Index[0]==*(s))&&(Tfont16[k].Index[1]==*(s+1)))
		{ 	
			//x方向循环执行写16行，逐行式输入
			for(y0=0;y0<4;y0++)
			{
				SPI_OLED_Set_Pos(x,y+y0);
				//每行写入两个字节，自左到右
				for(x0=0;x0<32;x0++)
				{	
					//一次写入1字节
					SPI_OLED_WR_Byte(Tfont32[k].Msk[y0*32+x0],OLED_DATA);
				}
			}
		//查找到对应点阵关键字完成绘字后立即break退出for循环，防止多个汉字重复取模显示		
		break; 
		}
	}
} 

/**
 * @brief 显示字符串（支持中英文混显）
 * @param x 起始列
 * @param y 起始页
 * @param str 字符串指针
 * @param size 字体大小（16 或 32）
 *
 * 自动识别英文字符（单字节）与汉字（双字节），选用相应字体显示
 */
void SPI_OLED_Show_Str(uint16_t x, uint16_t y, char *str,uint8_t size)
{					
	uint16_t x0 = x;
  	uint8_t bHz = 0;				//字符或者中文，首先默认是字符
	
	if(size!=32)
		size=16;					//默认1608
	while(*str!=0)					//判断为否为结束符
	{
		if(!bHz)					//判断是字符
		{
			//如果x，y坐标超出预设lcd屏大小则换行显示
			if(x>(128-size/2)) 
			{
				//显示靠前
				x=0;
				//显示换行
				y=(y+size/8)%8;	
			}
			if(y>(8-size/8))
			{
				y=0;
			}
			if((uint8_t)*str>0x80)	//对显示的字符检查，判断是否为中文
			{
				bHz=1;				//判断为中文，则跳过显示字符改为显示中文
			}
			else			  		//确定为字符
			{		  
				if(*str==0x0D)		//判断是换行符号
				{
					y+=size;		//下一个显示的坐标换行
					x=x0;			//显示靠前
					str++;			//准备下一个字符
				}
				else				//判断不是换行符
				{
					//显示对应尺寸字符
					SPI_OLED_ShowChar(x,y,*str);
					//显示完后右移起始显示横坐标准备下次显示
					x+=size/2;
				}
				//显示地址自增，准备下一个字符
				str++; 
			}
		}
		else						//判断是中文
		{   
			//如果x，y坐标超出预设lcd屏大小则换行显示
			if(x>(128-size)) 
			{
				//显示靠前
				x=0;
				//显示换行
				y=(y+size/8)%8;	
			}
			if(y>(8-size/8))
			{
				y=0;
			}
			bHz=0;					//改为默认字符用于下次字符判断 
			if(size==32)			//判断是否为32X32大小的中文
				//显示32X32大小的中文
				SPI_OLED_DrawFont32(x,y,str);	 	
			else if(size==16)		//否则为16X16大小的中文
				//显示16X16大小的中文
				SPI_OLED_DrawFont16(x,y,str);
			//由于显示为中文，需要自增3个地址	
			str+=3;	
			//显示完后右移起始显示横坐标准备下次显示
			x+=size;			
		}						 
	}   
}

