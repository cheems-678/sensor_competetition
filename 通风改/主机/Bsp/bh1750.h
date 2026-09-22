#ifndef BH1750_H
#define BH1750_H

#include <stdint.h>

typedef struct
{
    uint32_t init_attempt_count;
    uint32_t init_success_count;
    uint32_t read_attempt_count;
    uint32_t read_success_count;
    uint32_t failure_count;
    uint32_t last_hal_status;
    uint32_t last_hal_error;
    uint32_t last_success_ms;
    uint32_t lux_x10;
    uint32_t address_7bit;
    uint16_t raw;
    uint16_t reserved;
} Bh1750Diagnostics;

extern volatile Bh1750Diagnostics Bh1750Diag;

/* PB10=SCL, PB11=SDA, I2C2 at 100 kHz, ADDR tied low (7-bit 0x23). */
uint8_t Bh1750_Init(void);
uint8_t Bh1750_ReadLuxX10(uint32_t *lux_x10);

#endif /* BH1750_H */
