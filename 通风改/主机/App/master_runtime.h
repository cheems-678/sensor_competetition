#ifndef MASTER_RUNTIME_H
#define MASTER_RUNTIME_H

#include <stdint.h>
#include "FreeRTOS.h"

typedef struct
{
    uint32_t control_message_count;
    uint32_t slave_message_count;
    uint32_t address_drop_count;
    uint32_t telemetry_request_count;
    uint32_t telemetry_reply_count;
    uint32_t telemetry_timeout_count;
    uint32_t fan_command_count;
    uint32_t busy_reject_count;
    uint32_t lora_queue_failure_count;
} MasterRuntimeDiagnostics;

extern MasterRuntimeDiagnostics MasterRuntimeDiag;

void MasterRuntime_Init(void);
void MasterRuntime_ProcessOne(uint32_t now_ms, TickType_t wait_ticks);

#endif /* MASTER_RUNTIME_H */
