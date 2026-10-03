#ifndef MASTER_QUEUES_H
#define MASTER_QUEUES_H

#include <stdint.h>
#include "master_messages.h"

#define MASTER_EVENT_QUEUE_DEPTH   (4U)
#define MASTER_LORA_TX_QUEUE_DEPTH (4U)
#define MASTER_TELEMETRY_TX_LIFETIME_MS (1000UL)

typedef struct
{
    uint32_t event_full_count;
    uint32_t lora_tx_full_count;
    uint32_t lora_tx_expired_count;
} MasterQueueDiagnostics;

extern MasterQueueDiagnostics MasterQueueDiag;

uint8_t MasterQueues_Init(void);
uint8_t MasterQueues_SendEvent(const MasterEvent *event);
uint8_t MasterQueues_ReceiveEvent(MasterEvent *event);
uint8_t MasterQueues_SendLoRa(const LoRaMessage *message);
uint8_t MasterQueues_ReceiveLoRa(LoRaMessage *message);
/* Timestamp belongs to the local queue, not the wire message. */
uint8_t MasterQueues_ReceiveLoRaTimed(LoRaMessage *message, uint32_t *enqueued_tick);
uint8_t MasterQueues_IsLoRaExpired(uint8_t message_type,
                                  uint32_t enqueued_tick, uint32_t now_ms);
uint8_t MasterQueues_EventWaiting(void);
uint8_t MasterQueues_LoRaWaiting(void);

#endif /* MASTER_QUEUES_H */
