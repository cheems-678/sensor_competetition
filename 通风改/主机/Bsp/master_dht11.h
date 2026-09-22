#ifndef MASTER_DHT11_H
#define MASTER_DHT11_H

#include <stdint.h>

/*
 * PA5 单总线 DHT11 驱动。
 * 温度和湿度均按 0.1 单位返回，例如 25.3℃ 返回 253。
 */
void MasterDht11_Init(void);
uint8_t MasterDht11_Read(int16_t *temperature_x10,
                         uint16_t *humidity_x10);

#endif /* MASTER_DHT11_H */
