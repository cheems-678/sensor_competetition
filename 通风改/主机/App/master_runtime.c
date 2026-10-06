#include "master_runtime.h"

#include <string.h>

#include "fan_pwm.h"
#include "lora_protocol.h"
#include "master_bme280.h"
#include "master_config.h"
#include "master_identity.h"
#include "master_ingress.h"
#include "master_light_control.h"
#include "master_rain.h"
#include "master_messages.h"
#include "master_queues.h"
#include "stm32f1xx_hal.h"

#define MASTER_ERROR_BUSY          (1U)
#define MASTER_SLAVE_AUDIO_MAX_AGE_MS (300UL)

MasterRuntimeDiagnostics MasterRuntimeDiag;

typedef struct
{
    uint16_t flow_id;
    uint32_t sample_generation;
    uint32_t slave_start_tick;
    uint16_t slave_flow_id;
    uint8_t slave_state; /* 0=waiting to queue, 1=queued, 2=RX window, 3=done */
    uint8_t request_type;
    uint8_t force_sample;
    uint8_t window_servo_id;
    uint8_t window_action;
    uint8_t window_status;
    uint8_t slave_online;
    uint8_t slave_data[8];
    uint8_t slave_audio[8];
    uint8_t slave_mq2[8];
    uint8_t slave_ultrasonic[8];
    uint32_t slave_audio_received_tick;
    uint8_t web_origin, web_id[8];
    uint8_t active;
} PendingSlaveRequest;

static PendingSlaveRequest g_pending;
static MasterEvent g_event;
static uint16_t g_slave_flow;
static uint16_t g_status_flow;
static uint32_t g_status_due;
static WebRecord g_web_cache[4];
static uint8_t g_web_next;
typedef struct {
    uint32_t received_tick;
    uint16_t flow;
    uint8_t payload[WEB_REQUEST_SIZE], active;
} WaitingWebCommand;
static WaitingWebCommand g_web_wait;

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
    MasterBme280Sample sample;

    memset(payload, 0, LORA_PROTOCOL_ULTRASONIC_TELEMETRY_SIZE);
    payload[LORA_TELEMETRY_FLAGS_OFFSET] = LORA_TELEMETRY_FLAG_MASTER_BME |
        LORA_TELEMETRY_FLAG_DUAL_BME | LORA_TELEMETRY_FLAG_ACOUSTIC | LORA_TELEMETRY_FLAG_MQ2 |
        LORA_TELEMETRY_FLAG_ULTRASONIC;
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_BME_TEMP_OFFSET],
                           (uint16_t)LORA_PROTOCOL_TEMPERATURE_INVALID);
    MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_BME_HUM_OFFSET],
                           LORA_PROTOCOL_HUMIDITY_INVALID);
    MasterRuntime_WriteU32(&payload[LORA_TELEMETRY_BME_PRESSURE_OFFSET],
                           LORA_PROTOCOL_PRESSURE_INVALID);
    memcpy(&payload[LORA_TELEMETRY_REMOTE_BME_TEMP_OFFSET],
           g_pending.slave_data, sizeof(g_pending.slave_data));
    payload[LORA_TELEMETRY_RAIN_OFFSET] = MasterRain_GetState(now_ms);
    memset(&payload[LORA_TELEMETRY_SOUND_1_OFFSET], 0xFF, 8U);
    memset(&payload[LORA_TELEMETRY_MQ2_OFFSET], 0xFF, 8U);
    memset(&payload[LORA_TELEMETRY_ULTRASONIC_OFFSET], 0xFF, 8U);
    if (g_pending.slave_online != 0U)
    {
        payload[0] |= LORA_TELEMETRY_FLAG_SLAVE_ONLINE;
        {
            uint16_t source_age = (uint16_t)((uint16_t)g_pending.slave_mq2[6] |
                                            ((uint16_t)g_pending.slave_mq2[7] << 8U));
            uint32_t elapsed = now_ms - g_pending.slave_audio_received_tick;
            if ((source_age < LORA_TELEMETRY_MQ2_MAX_AGE_MS) &&
                (elapsed < LORA_TELEMETRY_MQ2_MAX_AGE_MS - source_age))
            {
                memcpy(&payload[LORA_TELEMETRY_MQ2_OFFSET], g_pending.slave_mq2, 8U);
                MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_MQ2_AGE_OFFSET],
                                       (uint16_t)(source_age + elapsed));
            }
        }
        {
            uint16_t source_age = (uint16_t)((uint16_t)g_pending.slave_ultrasonic[6] |
                                           ((uint16_t)g_pending.slave_ultrasonic[7] << 8U));
            uint32_t elapsed = now_ms - g_pending.slave_audio_received_tick;
            if ((source_age < LORA_TELEMETRY_ULTRASONIC_MAX_AGE_MS) &&
                (elapsed < LORA_TELEMETRY_ULTRASONIC_MAX_AGE_MS - source_age))
            {
                memcpy(&payload[LORA_TELEMETRY_ULTRASONIC_OFFSET], g_pending.slave_ultrasonic, 8U);
                MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_ULTRASONIC_AGE_OFFSET],
                                      (uint16_t)(source_age + elapsed));
            }
        }
        if ((uint32_t)(now_ms - g_pending.slave_audio_received_tick) <
            MASTER_SLAVE_AUDIO_MAX_AGE_MS)
        {
            memcpy(&payload[LORA_TELEMETRY_SOUND_1_OFFSET],
                   g_pending.slave_audio, sizeof(g_pending.slave_audio));
        }
    }

    if (MasterBme280_GetSample(now_ms, &sample) != 0U)
    {
        MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_BME_TEMP_OFFSET],
                               (uint16_t)sample.temperature_x10);
        MasterRuntime_WriteU16(&payload[LORA_TELEMETRY_BME_HUM_OFFSET],
                               sample.humidity_x10);
        MasterRuntime_WriteU32(&payload[LORA_TELEMETRY_BME_PRESSURE_OFFSET],
                               sample.pressure_pa);
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
    outbound.payload_length = LORA_PROTOCOL_ULTRASONIC_TELEMETRY_SIZE;
    MasterRuntime_FillPlaceholderTelemetry(outbound.payload, now_ms);
    if (MasterRuntime_Queue(&outbound) == 0U)
    {
        return 0U;
    }

    MasterRuntimeDiag.telemetry_reply_count++;
    return 1U;
}

static uint8_t QueueWebResult(uint16_t flow, const uint8_t *id, uint8_t state)
{
    LoRaMessage m; MasterRuntime_SetAddress(&m,WEB_RESULT,LORA_ROLE_SLAVE,1U,flow);
    m.payload_length=WEB_RESULT_SIZE;memcpy(m.payload,id,8U);m.payload[8]=state;return MasterRuntime_Queue(&m);
}
static void RememberWeb(const uint8_t *p,uint8_t state,uint32_t now)
{
    WebRecord *r=Web_Find(g_web_cache,p+3,now);if(r)return;
    r=g_web_cache+g_web_next;g_web_next=(uint8_t)((g_web_next+1U)%4U);
    memcpy(r->id,p+3,8U);r->command=p[0];r->channel=p[1];r->value=p[2];r->state=state;r->tick=now;
}
static uint8_t MasterRuntime_QueueWindowAck(uint16_t flow_id, uint8_t status)
{
    LoRaMessage outbound;

    MasterRuntime_SetAddress(&outbound, LORA_MSG_ACK,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    outbound.payload_length = 2U;
    outbound.payload[0] = LORA_MSG_SET_WINDOW;
    outbound.payload[1] = status;
    if (MasterRuntime_Queue(&outbound) == 0U)
    {
        return 0U;
    }
    MasterRuntimeDiag.window_reply_count++;
    return 1U;
}
static uint8_t MasterRuntime_QueueWindowResult(uint32_t now_ms)
{
    uint8_t p[WEB_REQUEST_SIZE];
    if(!g_pending.web_origin)
        return MasterRuntime_QueueWindowAck(g_pending.flow_id,g_pending.window_status);
    p[0]=LORA_MSG_SET_WINDOW;p[1]=g_pending.window_servo_id;p[2]=g_pending.window_action;
    memcpy(p+3,g_pending.web_id,8U);
    RememberWeb(p,g_pending.window_status,now_ms);
    return QueueWebResult(g_pending.flow_id,g_pending.web_id,g_pending.window_status);
}
/* Scalar transaction setup is shared by both command origins. No message copies
 * or response builders may be nested through this function. */
static void MasterRuntime_StartWindow(uint16_t flow,uint8_t servo_id,
                                      uint8_t action,uint32_t now_ms)
{
    memset(&g_pending,0,sizeof(g_pending));
    g_pending.active=1U;g_pending.request_type=LORA_MSG_SET_WINDOW;
    g_pending.flow_id=flow;g_pending.slave_flow_id=++g_slave_flow;
    g_pending.slave_start_tick=now_ms;g_pending.window_servo_id=servo_id;
    g_pending.window_action=action;g_pending.window_status=LORA_WINDOW_STATUS_TIMEOUT;
}

static void MasterRuntime_HandleControl(const LoRaMessage *message,
                                        uint32_t now_ms)
{
    LoRaMessage outbound;

    if (message->type == LORA_MSG_READ_TELEMETRY)
    {
        MasterRuntimeDiag.telemetry_request_count++;
        if (g_pending.active != 0U || g_web_wait.active != 0U)
        {
            MasterRuntimeDiag.busy_reject_count++;
            MasterRuntime_QueueError(message->flow_id, MASTER_ERROR_BUSY);
            return;
        }
        memset(&g_pending, 0, sizeof(g_pending));
        g_pending.active = 1U;
        g_pending.request_type = LORA_MSG_READ_TELEMETRY;
        g_pending.flow_id = message->flow_id;
        g_pending.slave_flow_id = ++g_slave_flow;
        g_pending.force_sample = message->payload[0];
        g_pending.slave_start_tick = now_ms;
        MasterRuntime_WriteU16(&g_pending.slave_data[0], 0x8000U);
        MasterRuntime_WriteU16(&g_pending.slave_data[2], 0xFFFFU);
        MasterRuntime_WriteU32(&g_pending.slave_data[4], 0xFFFFFFFFUL);
        memset(g_pending.slave_audio, 0xFF, sizeof(g_pending.slave_audio));
        memset(g_pending.slave_mq2, 0xFF, sizeof(g_pending.slave_mq2));
        memset(g_pending.slave_ultrasonic, 0xFF, sizeof(g_pending.slave_ultrasonic));
        g_pending.sample_generation = MasterBme280Diag.completed_count;
        MasterRuntimeDiag.last_request_flow_id = message->flow_id;
        if (g_pending.force_sample != 0U) { MasterBme280_RequestSample(now_ms); }
        return;
    }

    if (message->type == LORA_MSG_SET_WINDOW)
    {
        /* The wire decoder validates this too; never act on a malformed event. */
        if ((message->payload_length != 2U) ||
            (message->payload[0] < 1U) || (message->payload[0] > LORA_WINDOW_SERVO_COUNT) ||
            (message->payload[1] > 1U))
        {
            return;
        }
        MasterRuntimeDiag.window_command_count++;
        if (g_pending.active != 0U)
        {
            MasterRuntimeDiag.window_busy_count++;
            (void)MasterRuntime_QueueWindowAck(message->flow_id,
                                              LORA_WINDOW_STATUS_BUSY);
            return;
        }
        MasterRuntime_StartWindow(message->flow_id,message->payload[0],
                                  message->payload[1],now_ms);
        /* Opening (action=1) must not trigger a forced BME conversion. */
        return;
    }

    if (message->type == LORA_MSG_SET_FAN_SPEED)
    {
        uint8_t applied;

        if(g_pending.active){MasterRuntimeDiag.busy_reject_count++;MasterRuntime_QueueError(message->flow_id,MASTER_ERROR_BUSY);return;}
        MasterRuntimeDiag.fan_command_count++;
        applied = FanPwm_SetDuty(message->payload[0], message->payload[1]);
        MasterRuntime_SetAddress(&outbound, LORA_MSG_ACK,
                                 LORA_ROLE_CONTROL_ROOM, 0U,
                                 message->flow_id);
        outbound.payload_length = 2U;
        outbound.payload[0] = LORA_MSG_SET_FAN_SPEED;
        outbound.payload[1] = (applied != 0U) ? 0U : 1U;
        (void)MasterRuntime_Queue(&outbound);
    }
}

static void ExecuteWeb(const uint8_t *p,uint16_t flow,uint32_t now)
{
    uint8_t state;
    if(p[0]==LORA_MSG_SET_FAN_SPEED)
    {
        state=FanPwm_SetDuty(p[1],p[2])?WEB_OK:WEB_FAILED;
        if(state==WEB_OK)MasterRuntimeDiag.web_applied++;else MasterRuntimeDiag.web_failed++;
        RememberWeb(p,state,now);(void)QueueWebResult(flow,p+3,state);
    }
    else
    {
        MasterRuntimeDiag.window_command_count++;
        MasterRuntime_StartWindow(flow,p[1],p[2],now);
        g_pending.web_origin=1U;memcpy(g_pending.web_id,p+3,8U);
    }
}
static void HandleWeb(const LoRaMessage *m,uint32_t now,uint32_t received)
{
    const uint8_t *p=m->payload;WebRecord *cached;uint8_t state;
    if(!Web_Valid(p,m->payload_length))return;
    cached=Web_Find(g_web_cache,p+3,now);
    if(cached){(void)QueueWebResult(m->flow_id,p+3,Web_Same(cached,p)?cached->state:WEB_FAILED);return;}
    if(g_pending.web_origin&&g_pending.active&&!memcmp(g_pending.web_id,p+3,8U))
    {if(p[0]!=0x11U||p[1]!=g_pending.window_servo_id||p[2]!=g_pending.window_action)(void)QueueWebResult(m->flow_id,p+3,WEB_FAILED);return;}
    if(g_web_wait.active)
    {
        if(!memcmp(g_web_wait.payload+3,p+3,8U))
        {if(memcmp(g_web_wait.payload,p,3U))(void)QueueWebResult(m->flow_id,p+3,WEB_FAILED);}
        else (void)QueueWebResult(m->flow_id,p+3,WEB_BUSY);
        return;
    }
    if((uint32_t)(now-received)>=WEB_QUEUE_MS)state=WEB_UNKNOWN;
    else if(g_pending.active && g_pending.request_type==LORA_MSG_READ_TELEMETRY)
    {
        memcpy(g_web_wait.payload,p,WEB_REQUEST_SIZE);g_web_wait.flow=m->flow_id;
        g_web_wait.received_tick=received;g_web_wait.active=1U;MasterRuntimeDiag.web_deferred++;return;
    }
    else if(g_pending.active)state=WEB_BUSY;
    else
    {
        ExecuteWeb(p,m->flow_id,now);return;
    }
    RememberWeb(p,state,now);(void)QueueWebResult(m->flow_id,p+3,state);
}
static void ProcessWaitingWeb(uint32_t now)
{
    if(!g_web_wait.active)return;
    if((uint32_t)(now-g_web_wait.received_tick)>=WEB_TELEMETRY_WAIT_MS)
    {
        MasterRuntimeDiag.web_wait_timeout++;
        RememberWeb(g_web_wait.payload,WEB_BUSY,now);
        (void)QueueWebResult(g_web_wait.flow,g_web_wait.payload+3,WEB_BUSY);
        g_web_wait.active=0U;return;
    }
    if(g_pending.active)return;
    g_web_wait.active=0U;
    ExecuteWeb(g_web_wait.payload,g_web_wait.flow,now);
}
static void MasterRuntime_HandleSlave(const LoRaMessage *slave,
                                      uint32_t now_ms, uint32_t received_tick)
{
    uint8_t in_window = ((g_pending.active != 0U) &&
        (g_pending.slave_state == 2U) &&
        ((uint32_t)(now_ms - g_pending.slave_start_tick) <
         MASTER_SLAVE_RESPONSE_TIMEOUT_MS) &&
        (slave->flow_id == g_pending.slave_flow_id)) ? 1U : 0U;

    if (g_pending.request_type == LORA_MSG_SET_WINDOW)
    {
        if ((in_window != 0U) && (slave->type == LORA_MSG_ACK) &&
            (slave->payload_length == 2U) &&
            (slave->payload[0] == LORA_MSG_SET_WINDOW) &&
            (slave->payload[1] <= LORA_WINDOW_STATUS_BUSY))
        {
            g_pending.window_status = slave->payload[1];
            if(g_pending.web_origin)
            {if(slave->payload[1]==WEB_OK)MasterRuntimeDiag.web_applied++;else if(slave->payload[1]==WEB_FAILED)MasterRuntimeDiag.web_failed++;}
            g_pending.slave_state = 3U;
            MasterRuntimeDiag.window_response_match_count++;
        }
        else
        {
            MasterRuntimeDiag.window_response_unmatched_count++;
        }
        return;
    }

    if ((in_window != 0U) &&
        (g_pending.request_type == LORA_MSG_READ_TELEMETRY) &&
        (slave->type == LORA_MSG_TELEMETRY) &&
        (((slave->payload_length == LORA_PROTOCOL_LEGACY_TELEMETRY_SIZE) &&
          (slave->payload[0] == 0U)) ||
         ((slave->payload_length == LORA_PROTOCOL_TELEMETRY_SIZE) &&
          (slave->payload[0] == LORA_TELEMETRY_FLAG_ACOUSTIC)) ||
         ((slave->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_SIZE) &&
          (slave->payload[0] == (LORA_TELEMETRY_FLAG_ACOUSTIC | LORA_TELEMETRY_FLAG_MQ2))) ||
         ((slave->payload_length == LORA_PROTOCOL_ULTRASONIC_TELEMETRY_SIZE) &&
          (slave->payload[0] == (LORA_TELEMETRY_FLAG_ACOUSTIC | LORA_TELEMETRY_FLAG_MQ2 |
                                LORA_TELEMETRY_FLAG_ULTRASONIC)))))
    {
        uint32_t elapsed = now_ms - g_pending.slave_start_tick;
        memcpy(g_pending.slave_data, &slave->payload[1], 8U);
        if (slave->payload_length >= LORA_PROTOCOL_TELEMETRY_SIZE)
        {
            memcpy(g_pending.slave_audio,
                   &slave->payload[LORA_TELEMETRY_SOUND_1_OFFSET], 8U);
        }
        if (slave->payload_length >= LORA_PROTOCOL_MQ2_TELEMETRY_SIZE)
        { memcpy(g_pending.slave_mq2, &slave->payload[LORA_TELEMETRY_MQ2_OFFSET], 8U); }
        if (slave->payload_length == LORA_PROTOCOL_ULTRASONIC_TELEMETRY_SIZE)
        { memcpy(g_pending.slave_ultrasonic, &slave->payload[LORA_TELEMETRY_ULTRASONIC_OFFSET], 8U); }
        g_pending.slave_audio_received_tick = received_tick;
        g_pending.slave_online = 1U;
        g_pending.slave_state = 3U;
        MasterRuntimeDiag.slave_response_match_count++;
        MasterRuntimeDiag.last_slave_flow_id = slave->flow_id;
        MasterRuntimeDiag.last_slave_response_ms = elapsed;
        if (elapsed > MasterRuntimeDiag.max_slave_response_ms)
        { MasterRuntimeDiag.max_slave_response_ms = elapsed; }
    }
    else { MasterRuntimeDiag.slave_response_unmatched_count++; }
}

void MasterRuntime_Init(void)
{
    memset(&MasterRuntimeDiag, 0, sizeof(MasterRuntimeDiag));
    memset(&g_pending, 0, sizeof(g_pending));
    memset(g_web_cache,0,sizeof(g_web_cache));g_web_next=0U;
    memset(&g_web_wait,0,sizeof(g_web_wait));
    g_slave_flow = 0U;
    g_status_flow = 0U;
    g_status_due = HAL_GetTick() + MONITOR_STATUS_PERIOD_MS;
    MasterBme280_Init(HAL_GetTick());
    MasterLight_Init(HAL_GetTick());
    MasterRain_Init(HAL_GetTick());
}

void MasterRuntime_ProcessOne(uint32_t now_ms)
{
    MasterIngressRoute route;

    MasterLight_Process(now_ms);
    MasterBme280_Process(HAL_GetTick());
    now_ms = HAL_GetTick();
    MasterRain_Process(now_ms);
    ProcessWaitingWeb(now_ms);

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
                if(g_event.data.lora_message.type==WEB_REQUEST)
                {MasterRuntimeDiag.web_received++;HandleWeb(&g_event.data.lora_message,now_ms,g_event.received_tick);}
                else MasterRuntime_HandleSlave(&g_event.data.lora_message,
                                           now_ms, g_event.received_tick);
            }
            else
            {
                MasterRuntimeDiag.address_drop_count++;
            }
        }
    }

    if (g_pending.active != 0U)
    {
        if (g_pending.slave_state == 0U)
        {
            LoRaMessage query;
            MasterRuntime_SetAddress(&query, g_pending.request_type,
                LORA_ROLE_SLAVE, LORA_PROTOCOL_SINGLE_GROUP, g_pending.slave_flow_id);
            if (g_pending.request_type == LORA_MSG_SET_WINDOW)
            {
                query.payload_length = 2U;
                query.payload[0] = g_pending.window_servo_id;
                query.payload[1] = g_pending.window_action;
            }
            else
            {
                query.payload_length = 1U;
                query.payload[0] = g_pending.force_sample;
            }
            if (MasterRuntime_Queue(&query) != 0U)
            {
                g_pending.slave_state = 1U;
                if (g_pending.request_type == LORA_MSG_SET_WINDOW)
                { MasterRuntimeDiag.window_request_queued_count++; }
                else { MasterRuntimeDiag.slave_request_queued_count++; }
            }
        }
        if (((g_pending.slave_state <= 1U) &&
             ((uint32_t)(now_ms - g_pending.slave_start_tick) >= MASTER_SLAVE_QUEUE_TIMEOUT_MS)) ||
            ((g_pending.slave_state == 2U) &&
             ((uint32_t)(now_ms - g_pending.slave_start_tick) >= MASTER_SLAVE_RESPONSE_TIMEOUT_MS)))
        {
            g_pending.slave_state = 3U;
            if (g_pending.request_type == LORA_MSG_SET_WINDOW)
            {
                g_pending.window_status = LORA_WINDOW_STATUS_TIMEOUT;
                MasterRuntimeDiag.window_timeout_count++;
            }
            else
            {
                MasterRuntimeDiag.telemetry_timeout_count++;
                MasterRuntimeDiag.last_timeout_flow_id = g_pending.flow_id;
            }
        }
        if ((g_pending.slave_state == 3U) &&
            (g_pending.request_type == LORA_MSG_SET_WINDOW))
        {
            if (MasterRuntime_QueueWindowResult(now_ms) != 0U)
            { g_pending.active = 0U; }
        }
        else if ((g_pending.slave_state == 3U) &&
            (g_pending.request_type == LORA_MSG_READ_TELEMETRY) &&
            ((g_pending.force_sample == 0U) ||
             (MasterBme280Diag.completed_count != g_pending.sample_generation)))
        {
            if (MasterRuntime_QueueTelemetry(g_pending.flow_id, now_ms) != 0U)
            { g_pending.active = 0U; }
        }
    }
    ProcessWaitingWeb(HAL_GetTick());
}

void MasterRuntime_NotifySlaveRequestSent(uint16_t flow_id, uint32_t now_ms)
{
    if ((g_pending.active != 0U) && (g_pending.slave_state == 1U) &&
        (flow_id == g_pending.slave_flow_id))
    {
        g_pending.slave_start_tick = now_ms;
        g_pending.slave_state = 2U;
    }
}

uint8_t MasterRuntime_CanTransmit(void)
{
    return ((g_pending.active != 0U) && (g_pending.slave_state == 2U)) ? 0U : 1U;
}

uint8_t MasterRuntime_IsSlaveQueryCurrent(uint16_t flow_id)
{
    return MasterRuntime_IsSlaveRequestCurrent(LORA_MSG_READ_TELEMETRY, flow_id);
}

uint8_t MasterRuntime_IsSlaveRequestCurrent(uint8_t request_type, uint16_t flow_id)
{
    return ((g_pending.active != 0U) && (g_pending.slave_state == 1U) &&
            (g_pending.request_type == request_type) &&
            (g_pending.slave_flow_id == flow_id)) ? 1U : 0U;
}

uint8_t MasterRuntime_PrepareStatus(LoRaMessage *message, uint32_t now_ms)
{
    MasterBme280Sample sample;
    uint8_t i;
    if (message == NULL || g_pending.active != 0U ||
        MasterQueues_EventWaiting() != 0U || MasterQueues_LoRaWaiting() != 0U ||
        (int32_t)(now_ms - g_status_due) < 0)
    { return 0U; }
    g_status_due = now_ms + MONITOR_STATUS_PERIOD_MS;
    MasterRuntimeDiag.status_attempt_count++;
    MasterRuntime_SetAddress(message, LORA_MSG_MASTER_STATUS, LORA_ROLE_SLAVE,
                             LORA_PROTOCOL_SINGLE_GROUP, ++g_status_flow);
    message->payload_length = MONITOR_STATUS_BYTES;
    memset(message->payload, 0xFF, MONITOR_STATUS_BYTES);
    message->payload[0] = MONITOR_STATUS_LAYOUT;
    MasterRuntime_WriteU16(message->payload + MONITOR_BME_TEMP, 0x8000U);
    if (MasterBme280_GetSample(now_ms, &sample) != 0U)
    {
        MasterRuntime_WriteU16(message->payload + MONITOR_BME_TEMP,
                               (uint16_t)sample.temperature_x10);
        MasterRuntime_WriteU16(message->payload + MONITOR_BME_HUM, sample.humidity_x10);
        MasterRuntime_WriteU32(message->payload + MONITOR_BME_PRESSURE, sample.pressure_pa);
        MasterRuntime_WriteU16(message->payload + MONITOR_BME_AGE,
                               (uint16_t)(now_ms - MasterBme280Diag.sample_tick));
    }
    message->payload[MONITOR_RAIN] = MasterRain_GetState(now_ms);
    message->payload[MONITOR_DARK] = (MasterLightDiag.input_valid != 0U) ?
                                    (uint8_t)MasterLightDiag.stable_dark : 0xFFU;
    message->payload[MONITOR_LIGHT] = (MasterLightDiag.output_valid != 0U) ?
                                     (uint8_t)MasterLightDiag.light_on : 0xFFU;
    for (i = 0U; i < FAN_PWM_CHANNEL_COUNT; i++)
    { message->payload[MONITOR_FANS + i] = FanPwm_GetDuty((uint8_t)(i + 1U)); }
    MasterRuntime_WriteU32(message->payload + MONITOR_UPTIME, now_ms);
    MasterRuntime_WriteU32(message->payload + MONITOR_BME_SEQUENCE,
                           MasterBme280Diag.sample_success_count);
    return 1U;
}
