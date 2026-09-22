#ifndef MASTER_QUEUES_H
#define MASTER_QUEUES_H

#include <stdint.h>
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
uint8_t MasterQueues_SendEvent(const MasterEvent *event);
uint8_t MasterQueues_ReceiveEvent(MasterEvent *event);
uint8_t MasterQueues_SendLoRa(const LoRaMessage *message);
uint8_t MasterQueues_ReceiveLoRa(LoRaMessage *message);
uint8_t MasterQueues_EventWaiting(void);
uint8_t MasterQueues_LoRaWaiting(void);

#endif /* MASTER_QUEUES_H */
