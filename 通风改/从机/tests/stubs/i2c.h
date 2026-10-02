#include "stm32f1xx_hal.h"
extern I2C_HandleTypeDef hi2c1;
void MX_I2C1_Init(void);
HAL_StatusTypeDef SlaveI2c_InitAndRecover(void);
