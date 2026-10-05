/**
 * @file    iic_oled.c
 * @brief   基于 I²C 接口的 SSD1306 OLED 显示驱动实现
 *
 * 本文件实现了通过软件模拟 I²C 协议驱动 128×64 分辨率的 SSD1306 OLED 屏幕，
 * 支持 ASCII 字符、中文字体（16×16 / 32×32）、位图图片显示及基础控制功能。
 *
 * @note    - 使用 GPIO 模拟 I²C（非硬件 I²C）
 *          - 屏幕分辨率为 128 (宽) × 64 (高)，按 8 行（Page）组织，每行 128 字节
 *          - 中文编码采用 GB2312，每个汉字占 2 字节，UTF-8 需转换
 *          - 所有坐标系：x ∈ [0,127]（列），y ∈ [0,7]（页，每页 8 像素高）
 */
 
#include "iic_oled.h"
#include "stdlib.h"
#include "oledfont.h"
#include "delay.h"

/*------------------ I²C 底层通信函数 ------------------*/

/**
 * @brief  发送 I²C 起始信号
 * @details 时序：SCL=1 → SDA=1 → SDA=0 → SCL=0
 */
void IIC_Start()
{
    OLED_SCLK_Set();
    OLED_SDIN_Set();
    OLED_SDIN_Clr();
    OLED_SCLK_Clr();
}

/**
 * @brief  发送 I²C 停止信号
 * @details 时序：SCL=1 → SDA=0 → SDA=1
 */
void IIC_Stop()
{
    OLED_SCLK_Set();
    OLED_SDIN_Clr();
    OLED_SDIN_Set();
}

/**
 * @brief  等待从设备 ACK（此处简化为时钟脉冲）
 * @note   实际 SSD1306 不返回 ACK，此函数仅用于时序兼容
 */
void IIC_Wait_Ack()
{
    OLED_SCLK_Set();
    OLED_SCLK_Clr();
}

/**
 * @brief  通过 I²C 写入一个字节数据
 * @param IIC_Byte 要发送的 8 位数据
 * @details 从 MSB 到 LSB 逐位发送，SCL 上升沿锁存
 */
void Write_IIC_Byte(unsigned char IIC_Byte)
{
    unsigned char i;
    unsigned char m, da;
    da = IIC_Byte;
    OLED_SCLK_Clr();
    for (i = 0; i < 8; i++)
    {
        m = da;
        //	OLED_SCLK_Clr();
        m = m & 0x80;
        if (m == 0x80)
        {
            OLED_SDIN_Set();
        }
        else
            OLED_SDIN_Clr();
        da = da << 1;
        OLED_SCLK_Set();
        OLED_SCLK_Clr();
    }
}

/*------------------ OLED 命令/数据写入接口 ------------------*/

/**
 * @brief  通过 I²C 向 OLED 写入命令
 * @param IIC_Command 要写入的命令字节
 * @details 地址 0x78（SA0=0），随后发送 0x00 表示命令模式
 */
void Write_IIC_Command(unsigned char IIC_Command)
{
    IIC_Start();
    Write_IIC_Byte(0x78); // Slave address,SA0=0
    IIC_Wait_Ack();
    Write_IIC_Byte(0x00); // write command
    IIC_Wait_Ack();
    Write_IIC_Byte(IIC_Command);
    IIC_Wait_Ack();
    IIC_Stop();
}

/**
 * @brief  通过 I²C 向 OLED 写入显示数据
 * @param IIC_Data 要写入的数据字节
 * @details 地址 0x78，随后发送 0x40 表示数据模式
 */
void Write_IIC_Data(unsigned char IIC_Data)
{
    IIC_Start();
    Write_IIC_Byte(0x78); // D/C#=0; R/W#=0
    IIC_Wait_Ack();
    Write_IIC_Byte(0x40); // write data
    IIC_Wait_Ack();
    Write_IIC_Byte(IIC_Data);
    IIC_Wait_Ack();
    IIC_Stop();
}

/**
 * @brief  统一写入接口：根据 cmd 标志区分命令或数据
 * @param dat 要写入的字节
 * @param cmd 命令标志：非零为数据（OLED_DATA），0 为命令（OLED_CMD）
 * @see OLED_CMD, OLED_DATA
 */
void IIC_OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    if (cmd)
    {
        Write_IIC_Data(dat);
    }
    else
    {
        Write_IIC_Command(dat);
    }
}

/*------------------ OLED 控制与显示函数 ------------------*/

/**
 * @brief  设置显示起始坐标（页地址 + 列地址）
 * @param x 列地址（0～127）
 * @param y 页地址（0～7，每页 8 像素高）
 * @details 发送三字节命令序列：页地址、高4位列地址、低4位列地址
 */
void IIC_OLED_Set_Pos(unsigned char x, unsigned char y)
{
    IIC_OLED_WR_Byte(0xb0 + y, OLED_CMD);
    IIC_OLED_WR_Byte(((x & 0xf0) >> 4) | 0x10, OLED_CMD);
    IIC_OLED_WR_Byte((x & 0x0f), OLED_CMD);
}

/**
 * @brief  开启 OLED 显示（退出休眠）
 * @details 启用内部电荷泵并开启显示
 */
void IIC_OLED_Display_On(void)
{
    IIC_OLED_WR_Byte(0X8D, OLED_CMD); // SET DCDC
    IIC_OLED_WR_Byte(0X14, OLED_CMD); // DCDC ON
    IIC_OLED_WR_Byte(0XAF, OLED_CMD); // DISPLAY ON
}

/**
 * @brief  关闭 OLED 显示（进入超低功耗休眠）
 * @details 关闭电荷泵，功耗 <10μA
 */
void IIC_OLED_Display_Off(void)
{
    IIC_OLED_WR_Byte(0X8D, OLED_CMD); // SET DCDC
    IIC_OLED_WR_Byte(0X10, OLED_CMD); // DCDC OFF
    IIC_OLED_WR_Byte(0XAE, OLED_CMD); // DISPLAY OFF
}


/**
 * @brief  清空整个 OLED 屏幕（填充 0）
 */
void IIC_OLED_Clear(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        IIC_OLED_WR_Byte(0xb0 + i, OLED_CMD); 
        IIC_OLED_WR_Byte(0x00, OLED_CMD);    
        IIC_OLED_WR_Byte(0x10, OLED_CMD);     
        for (n = 0; n < 128; n++)
            IIC_OLED_WR_Byte(0, OLED_DATA);
    } 
}

/**
 * @brief  全屏点亮（用于测试，填充 0xFF）
 * @warning 此函数会点亮所有像素，长时间使用可能影响 OLED 寿命
 */
void IIC_OLED_On(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        IIC_OLED_WR_Byte(0xb0 + i, OLED_CMD); 
        IIC_OLED_WR_Byte(0x00, OLED_CMD);   
        IIC_OLED_WR_Byte(0x10, OLED_CMD);    
        for (n = 0; n < 128; n++)
            IIC_OLED_WR_Byte(1, OLED_DATA);
    } 
}

/*------------------ 文本与图形显示函数 ------------------*/

/**
 * @brief  显示单个 ASCII 字符（16×8 点阵）
 * @param x 起始列（0～127）
 * @param y 起始页（0～7）
 * @param chr ASCII 字符（空格 ' ' 起）
 * @note   自动处理换行：若 x > Max_Column-1，则 x=0, y+=2
 */
void IIC_OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr)
{
    uint8_t t = 0, c=0;	  
    c = chr - ' '; 
    if (x > Max_Column - 1)
    {
        x = 0;
        y = y + 2;
    }
    IIC_OLED_Set_Pos(x,y);	
    //自左到右循环输入
    for(t=0;t<8;t++)
        IIC_OLED_WR_Byte(ASCII_1608[c][t],OLED_DATA);
    IIC_OLED_Set_Pos(x,y+1);	
    //自左到右循环输入
    for(t=0;t<8;t++)
        IIC_OLED_WR_Byte(ASCII_1608[c][8+t],OLED_DATA);
}

/**
 * @brief  快速幂函数（用于数字分解）
 * @param m 底数
 * @param n 指数
 * @return m^n
 * @note   仅用于内部，不对外暴露
 */
static uint32_t oled_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--)
        result *= m;
    return result;
}

/**
 * @brief  显示无符号整数（右对齐，前导空格）
 * @param x 起始列
 * @param y 起始页
 * @param num 要显示的数字
 * @param len 总显示宽度（位数）
 * @note   例如：num=5, len=3 → 显示 "  5"
 */
void IIC_OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < len; t++)
    {
        temp = (num / oled_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                IIC_OLED_ShowChar(x + 8 * t, y, ' ');
                continue;
            }
            else
                enshow = 1;
        }
        IIC_OLED_ShowChar(x + 8*t, y, temp + '0');
    }
}

/**
 * @brief  显示 ASCII 字符串
 * @param x 起始列
 * @param y 起始页
 * @param chr 以 '\0' 结尾的字符串
 * @note   自动换行：当 x > 120 时换行
 */
void IIC_OLED_ShowString(uint8_t x, uint8_t y, char *chr)
{
    unsigned char j = 0;
    while (chr[j] != '\0')
    {
        IIC_OLED_ShowChar(x, y, chr[j]);
        x += 8;
        if (x > 120)
        {
            x = 0;
            y += 2;
        }
        j++;
    }
}

/**
 * @brief  显示预定义汉字（使用 Hzk 数组索引）
 * @param x 起始列
 * @param y 起始页
 * @param no 汉字在 Hzk[] 中的序号（0 起）
 * @note   每个汉字占 32 字节（16×16），分两页显示
 */
void IIC_OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no)
{
    uint8_t t, adder = 0;
    IIC_OLED_Set_Pos(x, y);
    for (t = 0; t < 16; t++)
    {
        IIC_OLED_WR_Byte(Hzk[2 * no][t], OLED_DATA);
        adder += 1;
    }
    IIC_OLED_Set_Pos(x, y + 1);
    for (t = 0; t < 16; t++)
    {
        IIC_OLED_WR_Byte(Hzk[2 * no + 1][t], OLED_DATA);
        adder += 1;
    }
}

/**
 * @brief  显示位图图片
 * @param x0 起始列
 * @param y0 起始页
 * @param x1 终止列（不包含）
 * @param y1 终止页（不包含）
 * @param BMP 位图数据数组（按页连续存储）
 * @note   图像数据必须与屏幕内存布局一致（每页 128 字节）
 */
void IIC_OLED_DrawBMP(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1, unsigned char BMP[])
{
    unsigned int j = 0;
    unsigned char x, y;

    if (y1 % 8 == 0)
        y = y1 / 8;
    else
        y = y1 / 8 + 1;
    for (y = y0; y < y1; y++)
    {
        IIC_OLED_Set_Pos(x0, y);
        for (x = x0; x < x1; x++)
        {
            IIC_OLED_WR_Byte(BMP[j++], OLED_DATA);
        }
    }
}

/**
 * @brief  显示单个 16×16 中文字（从 Tfont16 查找）
 * @param x 起始列
 * @param y 起始页
 * @param s 指向 GB2312 编码的汉字（2 字节）
 * @note   自动匹配 Tfont16 中的 Index，找到后立即绘制并退出
 */
void IIC_OLED_DrawFont16(uint16_t x, uint16_t y, char *s)
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
				IIC_OLED_Set_Pos(x,y+y0);
				//每行写入两个字节，自左到右
				for(x0=0;x0<16;x0++)
				{	
					//一次写入1字节
					IIC_OLED_WR_Byte(Tfont16[k].Msk[y0*16+x0],OLED_DATA);
				}
			}
		//查找到对应点阵关键字完成绘字后立即break退出for循环，防止多个汉字重复取模显示		
		break; 
		}
	}
}

/**
 * @brief  显示单个 32×32 中文字（从 Tfont32 查找）
 * @param x 起始列
 * @param y 起始页
 * @param s 指向 GB2312 编码的汉字（2 字节）
 */
void IIC_OLED_DrawFont32(uint16_t x, uint16_t y, char *s)
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
				IIC_OLED_Set_Pos(x,y+y0);
				//每行写入两个字节，自左到右
				for(x0=0;x0<32;x0++)
				{	
					//一次写入1字节
					IIC_OLED_WR_Byte(Tfont32[k].Msk[y0*32+x0],OLED_DATA);
				}
			}
		//查找到对应点阵关键字完成绘字后立即break退出for循环，防止多个汉字重复取模显示		
		break; 
		}
	}
} 

/**
 * @brief  混合显示中英文字符串（自动识别）
 * @param x 起始列
 * @param y 起始页
 * @param str 字符串指针
 * @param size 字体大小（16 或 32）
 * @note   - 遇到 0x0D 视为换行
 *         - 中文按 2 字节 GB2312 识别（首字节 > 0x80）
 */
void IIC_OLED_Show_Str(uint16_t x, uint16_t y, char *str,uint8_t size)
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
					IIC_OLED_ShowChar(x,y,*str);
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
				IIC_OLED_DrawFont32(x,y,str);	 	
			else if(size==16)		//否则为16X16大小的中文
				//显示16X16大小的中文
			IIC_OLED_DrawFont16(x,y,str);
			//由于显示为中文，需要自增3个地址	
			str+=3;	
			//显示完后右移起始显示横坐标准备下次显示
			x+=size;			
		}
	}
}

/*------------------ 初始化函数 ------------------*/

/**
 * @brief  OLED 初始化（GPIO + SSD1306 寄存器配置）
 * @details 配置 PB6(SCL)、PB7(SDA) 为推挽输出，并发送完整初始化序列
 */
void IIC_OLED_Init(void)
{

    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; 
    GPIO_Init(GPIOB, &GPIO_InitStructure); 
    GPIO_SetBits(GPIOB, GPIO_Pin_6 | GPIO_Pin_7);

    IIC_OLED_WR_Byte(0xAE, OLED_CMD); //--display off
    IIC_OLED_WR_Byte(0x00, OLED_CMD); //---set low column address
    IIC_OLED_WR_Byte(0x10, OLED_CMD); //---set high column address
    IIC_OLED_WR_Byte(0x40, OLED_CMD); //--set start line address
    IIC_OLED_WR_Byte(0xB0, OLED_CMD); //--set page address
    IIC_OLED_WR_Byte(0x81, OLED_CMD); // contract control
    IIC_OLED_WR_Byte(0xFF, OLED_CMD); //--128
    IIC_OLED_WR_Byte(0xA1, OLED_CMD); // set segment remap
    IIC_OLED_WR_Byte(0xA6, OLED_CMD); //--normal / reverse
    IIC_OLED_WR_Byte(0xA8, OLED_CMD); //--set multiplex ratio(1 to 64)
    IIC_OLED_WR_Byte(0x3F, OLED_CMD); //--1/32 duty
    IIC_OLED_WR_Byte(0xC8, OLED_CMD); // Com scan direction
    IIC_OLED_WR_Byte(0xD3, OLED_CMD); //-set display offset
    IIC_OLED_WR_Byte(0x00, OLED_CMD); //

    IIC_OLED_WR_Byte(0xD5, OLED_CMD); // set osc division
    IIC_OLED_WR_Byte(0x80, OLED_CMD); //

    IIC_OLED_WR_Byte(0xD8, OLED_CMD); // set area color mode off
    IIC_OLED_WR_Byte(0x05, OLED_CMD); //

    IIC_OLED_WR_Byte(0xD9, OLED_CMD); // Set Pre-Charge Period
    IIC_OLED_WR_Byte(0xF1, OLED_CMD); //

    IIC_OLED_WR_Byte(0xDA, OLED_CMD); // set com pin configuartion
    IIC_OLED_WR_Byte(0x12, OLED_CMD); //

    IIC_OLED_WR_Byte(0xDB, OLED_CMD); // set Vcomh
    IIC_OLED_WR_Byte(0x30, OLED_CMD); //

    IIC_OLED_WR_Byte(0x8D, OLED_CMD); // set charge pump enable
    IIC_OLED_WR_Byte(0x14, OLED_CMD); //

    IIC_OLED_WR_Byte(0xAF, OLED_CMD); //--turn on oled panel
}
