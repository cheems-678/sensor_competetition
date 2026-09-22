#ifndef MASTER_QUEUES_H
#define MASTER_QUEUES_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "master_messages.h"

#define MASTER_EVENT_QUEUE_DEPTH   (4U)
#define MASTER_LORA_TX_QUEUE_DEPTH (4U)

typedef struct
{
    uint32_t event_full_count;
    uint32_t lora_tx_full_count;
} MasterQueueDiagnostics;

extern MasterQueueDiagnostics MasterQueueDiag;

uint8_t MasterQueues_Init(void);
BaseType_t MasterQueues_SendEvent(const MasterEvent *event, TickType_t wait_ticks);
BaseType_t MasterQueues_ReceiveEvent(MasterEvent *event, TickType_t wait_ticks);
BaseType_t MasterQueues_SendLoRa(const LoRaMessage *message, TickType_t wait_ticks);
BaseType_t MasterQueues_ReceiveLoRa(LoRaMessage *message, TickType_t wait_ticks);
UBaseType_t MasterQueues_EventWaiting(void);
UBaseType_t MasterQueues_LoRaWaiting(void);

#endif /* MASTER_QUEUES_H */
