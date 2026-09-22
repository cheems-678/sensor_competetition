/**
 * @file    bme280.c
 * @brief   BME280 温湿度/气压传感器驱动实现 (软件 I2C)
 * @note    SCL = PB8, SDA = PB9, 7 位地址 BME280_I2C_ADDR。
 *          校准数据与温度补偿算法与 Bosch 官方驱动一致。
 */
#include "bme280.h"

/*==================================================================*
 *                  寄存器地址定义                                    *
 *==================================================================*/
#define BME280_REG_CHIP_ID       0xD0u
#define BME280_REG_RESET         0xE0u
#define BME280_REG_CTRL_HUM      0xF2u
#define BME280_REG_STATUS        0xF3u
#define BME280_REG_CTRL_MEAS     0xF4u
#define BME280_REG_CONFIG        0xF5u
#define BME280_REG_DATA          0xF7u
#define BME280_REG_CALIB_T_P     0x88u   /* 温度/气压校准: 24 字节 */
#define BME280_REG_CALIB_H       0xE1u   /* 湿度校准: 7 字节      */
#define BME280_CHIP_ID_VAL       0x60u   /* BME280 芯片 ID (带湿度)  */
#define BMP280_CHIP_ID_VAL       0x58u   /* BMP280 芯片 ID (仅温压)  */
#define BME280_SOFT_RESET_VAL    0xB6u

/*==================================================================*
 *                  校准数据 (静态变量)                               *
 *==================================================================*/
static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2;
static int16_t  dig_P3;
static int16_t  dig_P4;
static int16_t  dig_P5;
static int16_t  dig_P6;
static int16_t  dig_P7;
static int16_t  dig_P8;
static int16_t  dig_P9;

static uint8_t  dig_H1;
static int16_t  dig_H2;
static uint8_t  dig_H3;
static int16_t  dig_H4;
static int16_t  dig_H5;
static int8_t   dig_H6;

static int32_t  t_fine;

/* 芯片型号: 0x60=BME280(含湿度)  0x58=BMP280(仅温压) */
static uint8_t  bme_chip_id = 0;

/*==================================================================*
 *                      软件 I2C 底层                                *
 *==================================================================*/

static void bme280_delay_us(uint32_t us)
{
    volatile uint32_t i;

    for (i = 0; i < us * 12; i++) {
        __NOP();
    }
}

#define SCL_HIGH()  GPIO_SetBits(BME280_SCL_PORT, BME280_SCL_PIN)
#define SCL_LOW()   GPIO_ResetBits(BME280_SCL_PORT, BME280_SCL_PIN)
#define SDA_HIGH()  GPIO_SetBits(BME280_SDA_PORT, BME280_SDA_PIN)
#define SDA_LOW()   GPIO_ResetBits(BME280_SDA_PORT, BME280_SDA_PIN)

static uint8_t bme280_sda_read(void)
{
    SDA_HIGH();                         /* 释放 SDA, 依靠外部上拉 */
    bme280_delay_us(1);
    return (GPIO_ReadInputDataBit(BME280_SDA_PORT, BME280_SDA_PIN) != Bit_RESET);
}

static void bme280_i2c_start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    bme280_delay_us(2);
    SDA_LOW();                          /* SCL 高电平期间 SDA 下降沿 */
    bme280_delay_us(2);
    SCL_LOW();
    bme280_delay_us(2);
}

static void bme280_i2c_stop(void)
{
    SCL_LOW();
    SDA_LOW();
    bme280_delay_us(2);
    SCL_HIGH();
    bme280_delay_us(2);
    SDA_HIGH();                         /* SCL 高电平期间 SDA 上升沿 */
    bme280_delay_us(2);
}

/**
 * @brief   发送一个字节 (MSB 在前)
 * @return  0: 收到从机应答(ACK)  1: 未收到应答(NACK)
 */
static uint8_t bme280_i2c_write_byte(uint8_t dat)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++) {
        if (dat & 0x80u) {
            SDA_HIGH();
        } else {
            SDA_LOW();
        }
        dat <<= 1;
        bme280_delay_us(1);
        SCL_HIGH();
        bme280_delay_us(1);
        SCL_LOW();
        bme280_delay_us(1);
    }

    /* 释放 SDA, 读取从机 ACK */
    ack = bme280_sda_read();
    SCL_HIGH();
    bme280_delay_us(1);
    SCL_LOW();
    bme280_delay_us(1);

    return (ack == 0u) ? 0u : 1u;       /* 0: ACK, 1: NACK */
}

/**
 * @brief   读取一个字节
 * @param   ack  读完本字节后主机应答: 1=ACK, 0=NACK
 * @return  读到的字节
 */
static uint8_t bme280_i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t dat = 0;

    SDA_HIGH();                         /* 释放 SDA, 由从机驱动 */

    for (i = 0; i < 8; i++) {
        SCL_HIGH();
        bme280_delay_us(1);
        dat = (uint8_t)((dat << 1) | bme280_sda_read());
        SCL_LOW();
        bme280_delay_us(1);
    }

    /* 主机应答: ACK 拉低 / NACK 拉高 */
    if (ack) {
        SDA_LOW();
    } else {
        SDA_HIGH();
    }
    SCL_HIGH();
    bme280_delay_us(1);
    SCL_LOW();
    bme280_delay_us(1);
    SDA_HIGH();

    return dat;
}

/**
 * @brief   写单个寄存器
 * @return  0: 成功  1: 失败
 */
static uint8_t bme280_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t err = 0;

    bme280_i2c_start();
    err |= bme280_i2c_write_byte((uint8_t)(BME280_I2C_ADDR << 1));
    err |= bme280_i2c_write_byte(reg);
    err |= bme280_i2c_write_byte(val);
    bme280_i2c_stop();

    return err;
}

/**
 * @brief   连续读取 len 个寄存器
 * @return  0: 成功  1: 失败
 */
static uint8_t bme280_read_regs(uint8_t reg, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint8_t err = 0;

    if (!buf || !len) {
        return 1;
    }

    /* 写寄存器地址 */
    bme280_i2c_start();
    err |= bme280_i2c_write_byte((uint8_t)(BME280_I2C_ADDR << 1));
    err |= bme280_i2c_write_byte(reg);
    if (err) {
        bme280_i2c_stop();
        return 1;
    }

    /* 重复起始信号, 切换到读 */
    bme280_i2c_start();
    err |= bme280_i2c_write_byte((uint8_t)((BME280_I2C_ADDR << 1) | 0x01u));
    if (err) {
        bme280_i2c_stop();
        return 1;
    }

    /* 读 len 个字节, 最后一字节回 NACK */
    for (i = 0; i < len; i++) {
        buf[i] = bme280_i2c_read_byte((i == len - 1) ? 0u : 1u);
    }

    bme280_i2c_stop();
    return 0;
}

/*==================================================================*
 *                      补偿算法                                      *
 *==================================================================*/

/**
 * @brief   温度补偿 (返回 0.01℃ 单位的有符号整数)
 */
static int32_t bme280_comp_temp(int32_t adc_t)
{
    int32_t var1, var2;

    var1 = ((((adc_t >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_t >> 4) - ((int32_t)dig_T1)) *
              ((adc_t >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;

    return (int32_t)((t_fine * 5 + 128) >> 8);
}

/**
 * @brief   气压补偿 (返回 Pa 单位)
 */
static int32_t bme280_comp_press(int32_t adc_p)
{
    int32_t  var1, var2;
    uint32_t p;

    var1 = (((int32_t)t_fine) >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)dig_P6);
    var2 = var2 + ((var1 * ((int32_t)dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)dig_P4) << 16);
    var1 = (((dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            ((((int32_t)dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)dig_P1)) >> 15);
    if (var1 == 0) {
        return 0;                       /* 防止除零 */
    }

    p = (((uint32_t)(((int32_t)1048576) - adc_p) - (var2 >> 12))) * 3125u;
    if (p < 0x80000000u) {
        p = (p << 1) / ((uint32_t)var1);
    } else {
        p = (p / (uint32_t)var1) * 2u;
    }

    var1 = (((int32_t)dig_P9) *
            ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)dig_P8)) >> 13;

    return (int32_t)((int32_t)p + var1 + var2);
}

/**
 * @brief   湿度补偿 (返回 0.001%RH 单位)
 */
static uint32_t bme280_comp_hum(int32_t adc_h)
{
    int32_t v;

    v = (t_fine - ((int32_t)76800));
    v = (((((adc_h << 14) - (((int32_t)dig_H4) << 20) -
            (((int32_t)dig_H5) * v)) + ((int32_t)16384)) >> 15) *
         (((((((v * ((int32_t)dig_H6)) >> 10) *
              (((v * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4));
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;

    return (uint32_t)(v >> 12);
}

/*==================================================================*
 *                      初始化                                        *
 *==================================================================*/

uint8_t BME280_Init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t calib[26];
    uint8_t hcalib[7];
    uint8_t chip_id = 0;
    uint8_t i;

    /* 1. 配置软件 I2C 引脚: SCL/SDA 均开漏输出 (外部需上拉) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin   = BME280_SCL_PIN | BME280_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BME280_SCL_PORT, &gpio);
    GPIO_Init(BME280_SDA_PORT, &gpio);

    SCL_HIGH();
    SDA_HIGH();

    /* 2. 读取芯片 ID 校验 */
    if (bme280_read_regs(BME280_REG_CHIP_ID, &chip_id, 1)) {
        return 1;                       /* I2C 通信失败 */
    }
    if (chip_id != BME280_CHIP_ID_VAL && chip_id != BMP280_CHIP_ID_VAL) {
        return 2;                       /* 芯片 ID 不匹配 (不是BME/BMP280) */
    }
    bme_chip_id = chip_id;              /* 0x60=BME280, 0x58=BMP280 */

    /* 3. 软复位 */
    bme280_write_reg(BME280_REG_RESET, BME280_SOFT_RESET_VAL);
    bme280_delay_us(60000);             /* 等待复位完成约 10ms */

    /* 4. 读取校准数据 */
    if (bme280_read_regs(BME280_REG_CALIB_T_P, calib, 26)) {
        return 1;
    }
    if (bme280_read_regs(BME280_REG_CALIB_H, hcalib, 7)) {
        return 1;
    }

    dig_T1 = (uint16_t)((uint16_t)calib[1]  << 8) | (uint16_t)calib[0];
    dig_T2 = (int16_t)(((uint16_t)calib[3]  << 8) | (uint16_t)calib[2]);
    dig_T3 = (int16_t)(((uint16_t)calib[5]  << 8) | (uint16_t)calib[4]);

    dig_P1 = (uint16_t)((uint16_t)calib[7]  << 8) | (uint16_t)calib[6];
    dig_P2 = (int16_t)(((uint16_t)calib[9]  << 8) | (uint16_t)calib[8]);
    dig_P3 = (int16_t)(((uint16_t)calib[11] << 8) | (uint16_t)calib[10]);
    dig_P4 = (int16_t)(((uint16_t)calib[13] << 8) | (uint16_t)calib[12]);
    dig_P5 = (int16_t)(((uint16_t)calib[15] << 8) | (uint16_t)calib[14]);
    dig_P6 = (int16_t)(((uint16_t)calib[17] << 8) | (uint16_t)calib[16]);
    dig_P7 = (int16_t)(((uint16_t)calib[19] << 8) | (uint16_t)calib[18]);
    dig_P8 = (int16_t)(((uint16_t)calib[21] << 8) | (uint16_t)calib[20]);
    dig_P9 = (int16_t)(((uint16_t)calib[23] << 8) | (uint16_t)calib[22]);

    dig_H1 = calib[25];
    dig_H2 = (int16_t)(((uint16_t)hcalib[1] << 8) | (uint16_t)hcalib[0]);
    dig_H3 = hcalib[2];
    dig_H4 = (int16_t)(((int16_t)hcalib[3] << 4) | (hcalib[4] & 0x0Fu));
    dig_H5 = (int16_t)(((int16_t)hcalib[5] << 4) | (hcalib[4] >> 4));
    dig_H6 = (int8_t)hcalib[6];

    /* 5. 配置为正常模式 (normal mode):
     *    ctrl_hum = 1x 过采样
     *    ctrl_meas: osrs_t=1, osrs_p=1, mode=3 → 持续测量
     *    config: standby 默认, 滤波默认关闭
     */
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01u);
    bme280_write_reg(BME280_REG_CONFIG, 0x00u);
    bme280_write_reg(BME280_REG_CTRL_MEAS, 0x27u);

    /* 6. 丢弃首次测量结果 (传感器刚上电/复位后输出未稳定) */
    {
        BME280_Data dummy;

        for (i = 0; i < 10; i++) {
            (void)BME280_Read(&dummy);
        }
    }

    return 0;
}

/*==================================================================*
 *                      数据读取                                      *
 *==================================================================*/

uint8_t BME280_Read(BME280_Data *data)
{
    uint8_t raw[8];
    int32_t adc_p = 0, adc_t = 0, adc_h = 0;

    if (!data) {
        return 1;
    }

    if (bme280_read_regs(BME280_REG_DATA, raw, 8)) {
        return 1;
    }

    adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_h = ((int32_t)raw[6] << 8) | (int32_t)raw[7];

    /* 温度必须最先补偿 (更新 t_fine) */
    data->temp_c    = bme280_comp_temp(adc_t) * 0.01f;
    data->press_hpa = bme280_comp_press(adc_p) / 100.0f;
    if (bme_chip_id == BMP280_CHIP_ID_VAL) {
        data->hum_pct = -1.0f;          /* BMP280 无湿度通道 */
    } else {
        data->hum_pct = bme280_comp_hum(adc_h) * 0.001f;
    }

    return 0;
}

/** @brief 返回芯片ID: 0x60=BME280(含湿度) 0x58=BMP280(仅温压) 0=未初始化 */
uint8_t BME280_GetChipType(void)
{
    return bme_chip_id;
}