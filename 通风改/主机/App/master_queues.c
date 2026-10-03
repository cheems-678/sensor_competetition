#include "master_queues.h"

#include <string.h>
#include "stm32f1xx_hal.h"

MasterQueueDiagnostics MasterQueueDiag;

static MasterEvent g_event_queue[MASTER_EVENT_QUEUE_DEPTH];
static LoRaMessage g_lora_tx_queue[MASTER_LORA_TX_QUEUE_DEPTH];
static uint32_t g_lora_tx_enqueued_tick[MASTER_LORA_TX_QUEUE_DEPTH];
static uint8_t g_event_head;
static uint8_t g_event_tail;
static uint8_t g_event_count;
static uint8_t g_lora_tx_head;
static uint8_t g_lora_tx_tail;
static uint8_t g_lora_tx_count;

uint8_t MasterQueues_Init(void)
{
    memset(&MasterQueueDiag, 0, sizeof(MasterQueueDiag));
    g_event_head = 0U;
    g_event_tail = 0U;
    g_event_count = 0U;
    g_lora_tx_head = 0U;
    g_lora_tx_tail = 0U;
    g_lora_tx_count = 0U;
    return 1U;
}

uint8_t MasterQueues_SendEvent(const MasterEvent *event)
{
    if (event == NULL)
    {
        return 0U;
    }
    if (g_event_count >= MASTER_EVENT_QUEUE_DEPTH)
    {
        MasterQueueDiag.event_full_count++;
        return 0U;
    }
    g_event_queue[g_event_tail] = *event;
    g_event_tail = (uint8_t)((g_event_tail + 1U) % MASTER_EVENT_QUEUE_DEPTH);
    g_event_count++;
    return 1U;
}

uint8_t MasterQueues_ReceiveEvent(MasterEvent *event)
{
    if ((event == NULL) || (g_event_count == 0U))
    {
        return 0U;
    }
    *event = g_event_queue[g_event_head];
    g_event_head = (uint8_t)((g_event_head + 1U) % MASTER_EVENT_QUEUE_DEPTH);
    g_event_count--;
    return 1U;
}

uint8_t MasterQueues_SendLoRa(const LoRaMessage *message)
{
    if (message == NULL)
    {
        return 0U;
    }
    if (g_lora_tx_count >= MASTER_LORA_TX_QUEUE_DEPTH)
    {
        MasterQueueDiag.lora_tx_full_count++;
        return 0U;
    }
    g_lora_tx_queue[g_lora_tx_tail] = *message;
    g_lora_tx_enqueued_tick[g_lora_tx_tail] = HAL_GetTick();
    g_lora_tx_tail = (uint8_t)((g_lora_tx_tail + 1U) % MASTER_LORA_TX_QUEUE_DEPTH);
    g_lora_tx_count++;
    return 1U;
}

uint8_t MasterQueues_ReceiveLoRa(LoRaMessage *message)
{
    uint32_t enqueued_tick;
    return MasterQueues_ReceiveLoRaTimed(message, &enqueued_tick);
}

uint8_t MasterQueues_IsLoRaExpired(uint8_t message_type,
                                  uint32_t enqueued_tick, uint32_t now_ms)
{
    return ((message_type == LORA_MSG_TELEMETRY) &&
            ((uint32_t)(now_ms - enqueued_tick) >= MASTER_TELEMETRY_TX_LIFETIME_MS)) ? 1U : 0U;
}

uint8_t MasterQueues_ReceiveLoRaTimed(LoRaMessage *message, uint32_t *enqueued_tick)
{
    if ((message == NULL) || (enqueued_tick == NULL))
    {
        return 0U;
    }
    while (g_lora_tx_count != 0U)
    {
        *message = g_lora_tx_queue[g_lora_tx_head];
        *enqueued_tick = g_lora_tx_enqueued_tick[g_lora_tx_head];
        g_lora_tx_head = (uint8_t)((g_lora_tx_head + 1U) % MASTER_LORA_TX_QUEUE_DEPTH);
        g_lora_tx_count--;
        if (MasterQueues_IsLoRaExpired(message->type, *enqueued_tick, HAL_GetTick()) == 0U)
        {
            return 1U;
        }
        MasterQueueDiag.lora_tx_expired_count++;
    }
    return 0U;
}

uint8_t MasterQueues_EventWaiting(void)
{
    return g_event_count;
}

uint8_t MasterQueues_LoRaWaiting(void)
{
    return g_lora_tx_count;
}
