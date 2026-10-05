#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lora.h"
#include "lora_transport.h"
#include "master_queues.h"
#include "master_runtime.h"

UART_HandleTypeDef huart2;
uint8_t Rx2Buffer[256];
uint16_t rx2_pointer;
uint8_t rx2_frame_ready, rx2_overflow;
static uint32_t tick;
static uint8_t uart_fail, can_transmit = 1U, query_current = 1U;
static unsigned attempts, successes, query_notifications;
static uint8_t last_type;
static uint8_t last_checked_request_type;
static uint16_t last_checked_request_flow, last_notified_flow;
static uint32_t last_notified_tick;
static uint8_t incoming[141];
static uint16_t incoming_length, incoming_index;
static LoRaMessage last_uart_message;

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t delay) { tick += delay; }
uint32_t __get_PRIMASK(void) { return 0U; }
void __disable_irq(void) { }
void __enable_irq(void) { }
void lora_reset_off(void) { }
void lora_reset_on(void) { }
void Usart_SendString(UART_HandleTypeDef uart, unsigned char *data, unsigned short length)
{ (void)uart; (void)data; (void)length; }
void LoraTransport_Init(void) { }
void LoraTransport_EnableApplicationMode(void) { }
uint32_t LoraTransport_GetDataLossCount(void) { return 0U; }
uint8_t LoraTransport_PopRx(uint8_t *byte)
{
    if (incoming_index >= incoming_length) { return 0U; }
    *byte = incoming[incoming_index++];
    return 1U;
}
uint8_t MasterIdentity_GetGroup(void) { return 1U; }
uint8_t MasterRuntime_CanTransmit(void) { return can_transmit; }
uint8_t MasterRuntime_IsSlaveQueryCurrent(uint16_t flow_id)
{ (void)flow_id; return query_current; }
uint8_t MasterRuntime_IsSlaveRequestCurrent(uint8_t request_type, uint16_t flow_id)
{
    last_checked_request_type = request_type;
    last_checked_request_flow = flow_id;
    return query_current;
}
void MasterRuntime_NotifySlaveRequestSent(uint16_t flow_id, uint32_t now_ms)
{ last_notified_flow = flow_id; last_notified_tick = now_ms; query_notifications++; }

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
    LoRaMessage decoded;
    (void)uart;
    assert(timeout == 50U);
    assert(LoRaProtocol_Decode(data, length, &decoded) == LORA_PROTOCOL_OK);
    last_uart_message = decoded;
    attempts++;
    last_type = decoded.type;
    if (uart_fail) { return HAL_ERROR; }
    successes++;
    return HAL_OK;
}

static LoRaMessage response(uint8_t type)
{
    LoRaMessage message;
    memset(&message, 0, sizeof(message));
    message.version = 4U; message.type = type;
    message.source_role = 2U; message.source_group = 1U;
    message.destination_role = 1U; message.destination_group = 0U;
    message.payload_length = type == LORA_MSG_TELEMETRY ? 26U : 2U;
    message.payload[0] = type == LORA_MSG_TELEMETRY ? 0x0BU : 0x10U;
    return message;
}

static void test_uart_retry_has_original_enqueue_lifetime(void)
{
    LoRaMessage message = response(LORA_MSG_TELEMETRY);
    assert(MasterQueues_Init());
    tick = 100U;
    uart_fail = 1U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == 1U && successes == 0U);
    tick = 1099U;
    LoraP2PTX();
    assert(attempts == 2U);
    tick = 1100U;
    uart_fail = 0U;
    LoraP2PTX();
    assert(attempts == 2U && successes == 0U);
    assert(MasterQueueDiag.lora_tx_expired_count == 1U);
    message = response(LORA_MSG_ACK);
    tick = 1200U;
    uart_fail = 1U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    tick = 120000U;
    uart_fail = 0U;
    LoraP2PTX();
    assert(attempts == 4U && successes == 1U && last_type == LORA_MSG_ACK);
}

static void test_half_duplex_wait_and_tick_wrap(void)
{
    LoRaMessage message = response(LORA_MSG_TELEMETRY);
    unsigned before = attempts;
    tick = 0xFFFFFF00U;
    uart_fail = 1U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == before + 1U);
    can_transmit = 0U;
    tick = 744U; /* 1000 ms since original enqueue across wrap. */
    LoraP2PTX();
    assert(attempts == before + 1U && MasterQueueDiag.lora_tx_expired_count == 2U);
    can_transmit = 1U; uart_fail = 0U;
    tick = 745U;
    LoraP2PTX();
    assert(attempts == before + 1U);

    /* RX updates the 50 ms turnaround guard; a held frame still expires. */
    message = response(LORA_MSG_ACK);
    message.destination_role = 1U;
    assert(LoRaProtocol_Encode(&message, incoming, sizeof(incoming), &incoming_length) == LORA_PROTOCOL_OK);
    incoming_index = 0U; tick = 1000U;
    LoraP2PRX();
    message = response(LORA_MSG_TELEMETRY);
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == before + 1U);
    tick = 2000U;
    LoraP2PTX();
    assert(attempts == before + 1U && MasterQueueDiag.lora_tx_expired_count == 3U);
}

static void test_slave_query_expiry_policy_unchanged(void)
{
    LoRaMessage message = response(LORA_MSG_ACK);
    unsigned before = attempts;
    message.type = LORA_MSG_READ_TELEMETRY;
    message.destination_role = 3U; message.destination_group = 1U;
    message.payload_length = 1U; message.payload[0] = 0U;
    tick = 3000U; query_current = 0U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == before && query_notifications == 0U);
    query_current = 1U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == before + 1U && query_notifications == 1U);
}

static void test_rx_preserves_local_reception_timestamp(void)
{
    LoRaMessage message = response(LORA_MSG_TELEMETRY);
    MasterEvent event;
    message.source_role = 3U; message.destination_role = 2U;
    message.destination_group = 1U; message.payload[0] = 8U;
    assert(LoRaProtocol_Encode(&message, incoming, sizeof(incoming), &incoming_length) == LORA_PROTOCOL_OK);
    incoming_index = 0U; tick = 4000U;
    LoraP2PRX();
    tick = 4400U;
    assert(MasterQueues_ReceiveEvent(&event));
    assert(event.type == MASTER_EVENT_LORA_MESSAGE);
    assert(event.received_tick == 4000U);
    assert(event.data.lora_message.payload_length == 26U);
    assert(event.data.lora_message.payload[0] == 8U);
}

static void test_window_uart_retry_cancellation_and_sent_notification(void)
{
    LoRaMessage message = response(LORA_MSG_ACK);
    unsigned before = attempts, notifications_before = query_notifications;
    assert(MasterQueues_Init());
    message.type = LORA_MSG_SET_WINDOW;
    message.destination_role = LORA_ROLE_SLAVE; message.destination_group = 1U;
    message.flow_id = 0x2345U;
    message.payload_length = 2U; message.payload[0] = 1U; message.payload[1] = 1U;
    query_current = 1U; can_transmit = 1U; uart_fail = 1U; tick = 5000U;
    assert(MasterQueues_SendLoRa(&message));
    LoraP2PTX();
    assert(attempts == before + 1U && query_notifications == notifications_before);
    assert(last_checked_request_type == LORA_MSG_SET_WINDOW && last_checked_request_flow == 0x2345U);
    tick = 5999U; uart_fail = 0U;
    LoraP2PTX();
    assert(attempts == before + 2U && last_type == LORA_MSG_SET_WINDOW);
    assert(query_notifications == notifications_before + 1U);
    assert(last_notified_flow == 0x2345U && last_notified_tick == 5999U);

    tick = 7000U; uart_fail = 1U;
    assert(MasterQueues_SendLoRa(&message)); LoraP2PTX();
    assert(attempts == before + 3U);
    /* Runtime timed out while a failed UART frame was held: never send it later. */
    tick = 8000U; query_current = 0U; uart_fail = 0U;
    LoraP2PTX();
    assert(attempts == before + 3U && query_notifications == notifications_before + 1U);

    /* The same cancellation applies before first UART attempt while waiting for RX. */
    tick = 9000U; query_current = 1U; can_transmit = 0U;
    assert(MasterQueues_SendLoRa(&message)); LoraP2PTX();
    tick = 10000U; query_current = 0U; can_transmit = 1U;
    LoraP2PTX();
    assert(attempts == before + 3U && query_notifications == notifications_before + 1U);
    assert(!MasterQueues_LoRaWaiting());
}

static void test_window_ack_rx_and_direction_filter(void)
{
    LoRaMessage message = response(LORA_MSG_ACK);
    MasterEvent event;
    assert(MasterQueues_Init());
    message.source_role = LORA_ROLE_SLAVE;
    message.destination_role = LORA_ROLE_MASTER; message.destination_group = 1U;
    message.flow_id = 0x3456U; message.payload[0] = LORA_MSG_SET_WINDOW; message.payload[1] = 1U;
    assert(LoRaProtocol_Encode(&message, incoming, sizeof(incoming), &incoming_length) == LORA_PROTOCOL_OK);
    incoming_index = 0U; tick = 11000U; LoraP2PRX();
    assert(MasterQueues_ReceiveEvent(&event));
    assert(event.received_tick == 11000U && event.data.lora_message.flow_id == 0x3456U);
    assert(event.data.lora_message.type == LORA_MSG_ACK && event.data.lora_message.payload[0] == LORA_MSG_SET_WINDOW);
    message.payload[0] = LORA_MSG_SET_FAN_SPEED;
    assert(LoRaProtocol_Encode(&message, incoming, sizeof(incoming), &incoming_length) == LORA_PROTOCOL_INVALID_DIRECTION);
}

static void test_mq2_queue_age_retry_and_expiry(void)
{
    LoRaMessage message = response(LORA_MSG_TELEMETRY);
    unsigned before = attempts;
    message.payload_length = 34U; message.payload[0] = 0x1FU;
    memset(&message.payload[26], 0, 8U);
    message.payload[32] = (uint8_t)1800U; message.payload[33] = (uint8_t)(1800U >> 8U);
    tick = 20000U; uart_fail = 1U; assert(MasterQueues_Init());
    assert(MasterQueues_SendLoRa(&message)); LoraP2PTX();
    tick = 20050U; LoraP2PTX();
    assert((uint16_t)(last_uart_message.payload[32] | ((uint16_t)last_uart_message.payload[33] << 8U)) == 1850U);
    tick = 20100U; uart_fail = 0U; LoraP2PTX();
    assert(attempts == before + 3U);
    assert((uint16_t)(last_uart_message.payload[32] | ((uint16_t)last_uart_message.payload[33] << 8U)) == 1900U);
    tick = 21000U; assert(MasterQueues_SendLoRa(&message)); can_transmit = 0U; LoraP2PTX();
    tick = 21200U; can_transmit = 1U; LoraP2PTX();
    assert(memcmp(&last_uart_message.payload[26], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8U) == 0);
    tick = 0xFFFFFFF0U; assert(MasterQueues_SendLoRa(&message));
    tick = 34U; LoraP2PTX();
    assert((uint16_t)(last_uart_message.payload[32] | ((uint16_t)last_uart_message.payload[33] << 8U)) == 1850U);
}

int main(void)
{
    test_uart_retry_has_original_enqueue_lifetime();
    test_half_duplex_wait_and_tick_wrap();
    test_slave_query_expiry_policy_unchanged();
    test_rx_preserves_local_reception_timestamp();
    test_window_uart_retry_cancellation_and_sent_notification();
    test_window_ack_rx_and_direction_filter();
    test_mq2_queue_age_retry_and_expiry();
    puts("7 real LoRa UART/MQ2/window groups passed (retry/TTL/half-duplex/wrap/ACK/query/RX tick)");
    return 0;
}
