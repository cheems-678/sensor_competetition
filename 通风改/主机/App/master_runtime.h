#ifndef MASTER_RUNTIME_H
#define MASTER_RUNTIME_H

#include <stdint.h>

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
    uint32_t slave_request_queued_count;
    uint32_t slave_response_match_count;
    uint32_t slave_response_unmatched_count;
    uint32_t last_request_flow_id;
    uint32_t last_slave_flow_id;
    uint32_t last_timeout_flow_id;
    uint32_t last_slave_response_ms;
    uint32_t max_slave_response_ms;
} MasterRuntimeDiagnostics;

extern MasterRuntimeDiagnostics MasterRuntimeDiag;

void MasterRuntime_Init(void);
void MasterRuntime_ProcessOne(uint32_t now_ms);

#endif /* MASTER_RUNTIME_H */
