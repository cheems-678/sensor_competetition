#ifndef SLAVE_PROTOCOL_RUNTIME_H
#define SLAVE_PROTOCOL_RUNTIME_H

#include <stdint.h>

#define SLAVE_TEMPERATURE_INVALID_X10        (-32767 - 1)
#define SLAVE_HUMIDITY_INVALID_X10           (0xFFFFU)
#define SLAVE_PRESSURE_INVALID_PA            (0xFFFFFFFFUL)
#define SLAVE_SOUND_RMS_INVALID              (0xFFFFFFFFUL)

typedef struct
{
    volatile uint32_t rx_overflow_count;
    volatile uint32_t invalid_frame_count;
    volatile uint32_t ignored_message_count;
    volatile uint32_t duplicate_request_count;
    volatile uint32_t tx_failure_count;
    volatile uint32_t request_count;
    volatile uint32_t reply_count;
    volatile uint32_t last_flow_id;
} SlaveRuntimeDiagnostics;

extern SlaveRuntimeDiagnostics SlaveRuntimeDiag;

/* USART2中断只写入字节；解析、采样请求和发送均在主循环完成。 */
void SlaveRuntime_Init(uint8_t local_group);
uint8_t SlaveRuntime_IsApplicationMode(void);
void SlaveRuntime_PushRxByteFromIsr(uint8_t byte);
void SlaveRuntime_Process(uint32_t now_ms);

#endif /* SLAVE_PROTOCOL_RUNTIME_H */
