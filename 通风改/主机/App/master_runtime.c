#include "master_runtime.h"

#include <string.h>

#include "fan_pwm.h"
#include "lora_protocol.h"
#include "master_dht11.h"
#include "master_config.h"
#include "master_identity.h"
#include "master_ingress.h"
#include "master_light_control.h"
#include "master_messages.h"
#include "master_queues.h"
#include "stm32f1xx_hal.h"

#define MASTER_ERROR_BUSY          (1U)
#define MASTER_ERROR_SLAVE_TIMEOUT (2U)

MasterRuntimeDiagnostics MasterRuntimeDiag;

typedef struct
{
    uint16_t flow_id;
    uint32_t request_tick;
    uint8_t active;
} PendingTelemetry;

static PendingTelemetry g_pending;
static MasterEvent g_event;

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t sample_tick;
    uint32_t attempt_tick;
    uint8_t valid;
    uint8_t attempted;
} MasterDht11Cache;

static MasterDht11Cache g_dht11_cache;

static void MasterRuntime_SetAddress(LoRaMessage *message,
                                     uint8_t type,
                                     uint8_t destination_role,
                                     uint8_t destination_group,
                                     uint16_t flow_id)
{
    memset(message, 0, sizeof(*message));
    message->version = LORA_PROTOCOL_VERSION;
    message->type = type;
    message->source_role = LORA_ROLE_MASTER;
    message->source_group = LORA_PROTOCOL_SINGLE_GROUP;
    message->destination_role = destination_role;
    message->destination_group = destination_group;
    message->flow_id = flow_id;
}

static uint8_t MasterRuntime_Queue(const LoRaMessage *message)
{
    if (MasterQueues_SendLoRa(message) == 0U)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static void MasterRuntime_WriteU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void MasterRuntime_WriteU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static void MasterRuntime_FillPlaceholderTelemetry(uint8_t *payload,
                                                   uint32_t now_ms)
{
    memset(payload, 0, LORA_PROTOCOL_TELEMETRY_SIZE);
    payload[LORA_TELEMETRY_FLAGS_OFFSET] = 0U;
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_SLAVE_TEMP_OFFSET],
                           (uint16_t)LORA_PROTOCOL_TEMPERATURE_INVALID);
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_SLAVE_HUM_OFFSET],
                           LORA_PROTOCOL_HUMIDITY_INVALID);
    MasterRuntime_WriteU32(&payload[LORA_TELEMETRY_SLAVE_PRESSURE_OFFSET],
                           LORA_PROTOCOL_PRESSURE_INVALID);
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_SOUND_1_OFFSET],
                           LORA_PROTOCOL_SOUND_INVALID);
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_SOUND_2_OFFSET],
                           LORA_PROTOCOL_SOUND_INVALID);
    payload[LORA_TELEMETRY_RAIN_OFFSET] = 0xFFU;
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_MASTER_TEMP_OFFSET],
                           (uint16_t)LORA_PROTOCOL_TEMPERATURE_INVALID);
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_MASTER_HUM_OFFSET],
                           LORA_PROTOCOL_HUMIDITY_INVALID);

    if ((g_dht11_cache.attempted == 0U) ||
        ((uint32_t)(now_ms - g_dht11_cache.attempt_tick) >=
         MASTER_DHT11_CACHE_FRESH_MS))
    {
        int16_t temperature_x10;
        uint16_t humidity_x10;

        g_dht11_cache.attempted = 1U;
        g_dht11_cache.attempt_tick = now_ms;
        if (MasterDht11_Read(&temperature_x10, &humidity_x10) != 0U)
        {
            g_dht11_cache.temperature_x10 = temperature_x10;
            g_dht11_cache.humidity_x10 = humidity_x10;
            g_dht11_cache.sample_tick = now_ms;
            g_dht11_cache.valid = 1U;
        }
    }

    if (g_dht11_cache.valid != 0U)
    {
        MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_MASTER_TEMP_OFFSET],
                               (uint16_t)g_dht11_cache.temperature_x10);
        MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_MASTER_HUM_OFFSET],
                               g_dht11_cache.humidity_x10);
    }
}

static void MasterRuntime_QueueError(uint16_t flow_id, uint8_t error_code)
{
    LoRaMessage reply;
    MasterRuntime_SetAddress(&reply, LORA_MSG_ERROR, LORA_ROLE_CONTROL_ROOM,
                             0U, flow_id);
    reply.payload_length = 1U;
    reply.payload[0] = error_code;
    (void)MasterRuntime_Queue(&reply);
}

static uint8_t MasterRuntime_QueueTelemetry(uint16_t flow_id, uint32_t now_ms)
{
    LoRaMessage outbound;

    MasterRuntime_SetAddress(&outbound, LORA_MSG_TELEMETRY,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    outbound.payload_length = LORA_PROTOCOL_TELEMETRY_SIZE;
    MasterRuntime_FillPlaceholderTelemetry(outbound.payload, now_ms);
    if (MasterRuntime_Queue(&outbound) == 0U)
    {
        return 0U;
    }

    MasterRuntimeDiag.telemetry_reply_count++;
    return 1U;
}

static void MasterRuntime_HandleControl(const LoRaMessage *message,
                                        uint32_t now_ms)
{
    LoRaMessage outbound;

    if (message->type == LORA_MSG_READ_TELEMETRY)
    {
        MasterRuntimeDiag.telemetry_request_count++;
#if (MASTER_DIRECT_DHT11_TELEMETRY != 0U)
        (void)MasterRuntime_QueueTelemetry(message->flow_id, now_ms);
        return;
#else
        if (g_pending.active != 0U)
        {
            MasterRuntimeDiag.busy_reject_count++;
            MasterRuntime_QueueError(message->flow_id, MASTER_ERROR_BUSY);
            return;
        }
        MasterRuntime_SetAddress(&outbound, LORA_MSG_READ_TELEMETRY,
                                 LORA_ROLE_SLAVE,
                                 LORA_PROTOCOL_SINGLE_GROUP,
                                 message->flow_id);
        outbound.payload_length = 1U;
        outbound.payload[0] = message->payload[0];
        if (MasterRuntime_Queue(&outbound) != 0U)
        {
            g_pending.active = 1U;
            g_pending.flow_id = message->flow_id;
            g_pending.request_tick = now_ms;
            MasterRuntimeDiag.slave_request_queued_count++;
            MasterRuntimeDiag.last_request_flow_id = message->flow_id;
        }
        return;
#endif
    }

    if (message->type == LORA_MSG_SET_FAN_SPEED)
    {
        MasterRuntimeDiag.fan_command_count++;
        (void)FanPwm_SetDuty(message->payload[0], message->payload[1]);
        MasterRuntime_SetAddress(&outbound, LORA_MSG_ACK,
                                 LORA_ROLE_CONTROL_ROOM, 0U,
                                 message->flow_id);
        outbound.payload_length = 2U;
        outbound.payload[0] = LORA_MSG_SET_FAN_SPEED;
        outbound.payload[1] = 0U;
        (void)MasterRuntime_Queue(&outbound);
    }
}

static void MasterRuntime_HandleSlave(const LoRaMessage *message,
                                      uint32_t now_ms)
{
    MasterRuntimeDiag.last_slave_flow_id = message->flow_id;
    if ((message->type != LORA_MSG_TELEMETRY) ||
        (g_pending.active == 0U) ||
        (message->flow_id != g_pending.flow_id))
    {
        MasterRuntimeDiag.slave_response_unmatched_count++;
        return;
    }

    MasterRuntimeDiag.slave_response_match_count++;
    MasterRuntimeDiag.last_slave_response_ms = now_ms - g_pending.request_tick;
    if (MasterRuntimeDiag.last_slave_response_ms >
        MasterRuntimeDiag.max_slave_response_ms)
    {
        MasterRuntimeDiag.max_slave_response_ms =
            MasterRuntimeDiag.last_slave_response_ms;
    }
    if (MasterRuntime_QueueTelemetry(message->flow_id, now_ms) != 0U)
    {
        g_pending.active = 0U;
    }
}

void MasterRuntime_Init(void)
{
    memset(&MasterRuntimeDiag, 0, sizeof(MasterRuntimeDiag));
    memset(&g_pending, 0, sizeof(g_pending));
    memset(&g_dht11_cache, 0, sizeof(g_dht11_cache));
    MasterDht11_Init();
    MasterLight_Init(HAL_GetTick());
}

void MasterRuntime_ProcessOne(uint32_t now_ms)
{
    MasterIngressRoute route;

    MasterLight_Process(now_ms);

    if (MasterQueues_ReceiveEvent(&g_event) != 0U)
    {
        if (g_event.type == MASTER_EVENT_LORA_MESSAGE)
        {
            route = MasterIngress_Route(&g_event.data.lora_message,
                                        LORA_PROTOCOL_SINGLE_GROUP);
            if (route == MASTER_INGRESS_CONTROL_ROOM)
            {
                MasterRuntimeDiag.control_message_count++;
                MasterRuntime_HandleControl(&g_event.data.lora_message, now_ms);
            }
            else if (route == MASTER_INGRESS_SLAVE)
            {
                MasterRuntimeDiag.slave_message_count++;
                MasterRuntime_HandleSlave(&g_event.data.lora_message, now_ms);
            }
            else
            {
                MasterRuntimeDiag.address_drop_count++;
            }
        }
    }

    if ((g_pending.active != 0U) &&
        ((uint32_t)(now_ms - g_pending.request_tick) >=
         MASTER_SLAVE_RESPONSE_TIMEOUT_MS))
    {
        MasterRuntime_QueueError(g_pending.flow_id, MASTER_ERROR_SLAVE_TIMEOUT);
        MasterRuntimeDiag.last_timeout_flow_id = g_pending.flow_id;
        g_pending.active = 0U;
        MasterRuntimeDiag.telemetry_timeout_count++;
    }
}
