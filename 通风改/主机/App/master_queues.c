#include "master_queues.h"

#include <string.h>

MasterQueueDiagnostics MasterQueueDiag;

static MasterEvent g_event_queue[MASTER_EVENT_QUEUE_DEPTH];
static LoRaMessage g_lora_tx_queue[MASTER_LORA_TX_QUEUE_DEPTH];
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
    g_lora_tx_tail = (uint8_t)((g_lora_tx_tail + 1U) % MASTER_LORA_TX_QUEUE_DEPTH);
    g_lora_tx_count++;
    return 1U;
}

uint8_t MasterQueues_ReceiveLoRa(LoRaMessage *message)
{
    if ((message == NULL) || (g_lora_tx_count == 0U))
    {
        return 0U;
    }
    *message = g_lora_tx_queue[g_lora_tx_head];
    g_lora_tx_head = (uint8_t)((g_lora_tx_head + 1U) % MASTER_LORA_TX_QUEUE_DEPTH);
    g_lora_tx_count--;
    return 1U;
}

uint8_t MasterQueues_EventWaiting(void)
{
    return g_event_count;
}

uint8_t MasterQueues_LoRaWaiting(void)
{
    return g_lora_tx_count;
}
