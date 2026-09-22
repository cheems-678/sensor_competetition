#include "master_queues.h"

#include <string.h>

MasterQueueDiagnostics MasterQueueDiag;

static StaticQueue_t g_event_queue_control;
static StaticQueue_t g_lora_tx_queue_control;
static uint8_t g_event_queue_storage[MASTER_EVENT_QUEUE_DEPTH * sizeof(MasterEvent)];
static uint8_t g_lora_tx_queue_storage[MASTER_LORA_TX_QUEUE_DEPTH * sizeof(LoRaMessage)];
static QueueHandle_t g_event_queue;
static QueueHandle_t g_lora_tx_queue;

uint8_t MasterQueues_Init(void)
{
    memset(&MasterQueueDiag, 0, sizeof(MasterQueueDiag));
    g_event_queue = xQueueCreateStatic(MASTER_EVENT_QUEUE_DEPTH,
                                       sizeof(MasterEvent),
                                       g_event_queue_storage,
                                       &g_event_queue_control);
    g_lora_tx_queue = xQueueCreateStatic(MASTER_LORA_TX_QUEUE_DEPTH,
                                         sizeof(LoRaMessage),
                                         g_lora_tx_queue_storage,
                                         &g_lora_tx_queue_control);
    return ((g_event_queue != NULL) && (g_lora_tx_queue != NULL)) ? 1U : 0U;
}

BaseType_t MasterQueues_SendEvent(const MasterEvent *event, TickType_t wait_ticks)
{
    BaseType_t result;
    if ((event == NULL) || (g_event_queue == NULL))
    {
        return pdFAIL;
    }
    result = xQueueSendToBack(g_event_queue, event, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.event_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_ReceiveEvent(MasterEvent *event, TickType_t wait_ticks)
{
    if ((event == NULL) || (g_event_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueueReceive(g_event_queue, event, wait_ticks);
}

BaseType_t MasterQueues_SendLoRa(const LoRaMessage *message, TickType_t wait_ticks)
{
    BaseType_t result;
    if ((message == NULL) || (g_lora_tx_queue == NULL))
    {
        return pdFAIL;
    }
    result = xQueueSendToBack(g_lora_tx_queue, message, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.lora_tx_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_ReceiveLoRa(LoRaMessage *message, TickType_t wait_ticks)
{
    if ((message == NULL) || (g_lora_tx_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueueReceive(g_lora_tx_queue, message, wait_ticks);
}

UBaseType_t MasterQueues_EventWaiting(void)
{
    return (g_event_queue != NULL) ? uxQueueMessagesWaiting(g_event_queue) : 0U;
}

UBaseType_t MasterQueues_LoRaWaiting(void)
{
    return (g_lora_tx_queue != NULL) ? uxQueueMessagesWaiting(g_lora_tx_queue) : 0U;
}
