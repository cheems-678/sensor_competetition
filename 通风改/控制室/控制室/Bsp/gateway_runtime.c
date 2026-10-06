#include "gateway_runtime.h"
#include "lora_protocol.h"

#include <string.h>

/* Must match PROTOCOL_V4.md and the master's lora_protocol.c. */
#define GATEWAY_HEAD_1                  LORA_PROTOCOL_HEAD_0
#define GATEWAY_HEAD_2                  LORA_PROTOCOL_HEAD_1
#define GATEWAY_VERSION                 LORA_PROTOCOL_VERSION
#define GATEWAY_MIN_FRAME_SIZE          (13U)
#define GATEWAY_MAX_FRAME_SIZE          LORA_PROTOCOL_FRAME_MAX_SIZE
#define GATEWAY_MAX_PAYLOAD_SIZE        LORA_PROTOCOL_MAX_PAYLOAD
#define GATEWAY_TELEMETRY_PAYLOAD_SIZE  LORA_PROTOCOL_TELEMETRY_BYTES

#define GATEWAY_ROLE_CONTROL_ROOM       (0x01U)
#define GATEWAY_ROLE_MASTER             (0x02U)
#define GATEWAY_FIRST_GROUP             (0x01U)
#define GATEWAY_LAST_GROUP              (0x01U)

#define GATEWAY_TYPE_READ_TELEMETRY     (0x01U)
#define GATEWAY_TYPE_TELEMETRY          (0x02U)
#define GATEWAY_TYPE_SET_FAN_SPEED      (0x10U)
#define GATEWAY_TYPE_SET_WINDOW         (0x11U)
#define GATEWAY_TYPE_ACK                (0x20U)
#define GATEWAY_TYPE_ERROR              (0x7EU)
#define GATEWAY_ERROR_STATE_NOT_ALLOWED (0x02U)
#define GATEWAY_ERROR_BUSY              (0x03U)
#define GATEWAY_ERROR_MASTER_TIMEOUT    (0x0AU)

/* 当前轮询节奏和主机从机超时相匹配，集中定义，禁止散落魔数。 */
#define GATEWAY_POLL_INTERVAL_MS        (1000UL)
#define GATEWAY_TEMP_TIMEOUT_MS         (4000UL)
#define GATEWAY_COMMAND_TIMEOUT_MS      (6000UL)

#define GATEWAY_RX_RING_SIZE            (256U)
#define GATEWAY_RX_RING_MASK            (GATEWAY_RX_RING_SIZE - 1U)
#define GATEWAY_PC_QUEUE_DEPTH          (4U)
#define GATEWAY_MAX_RX_BYTES_PER_TICK   (128U)

typedef struct
{
    volatile uint8_t data[GATEWAY_RX_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} GatewayRxRing;

typedef struct
{
    uint8_t frame[GATEWAY_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t expected_length;
} GatewayParser;

typedef struct
{
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[GATEWAY_MAX_PAYLOAD_SIZE];
} GatewayMessage;

typedef struct
{
    GatewayMessage message;
} GatewayQueuedMessage;

typedef struct
{
    uint8_t active;
    uint8_t automatic_poll;
    uint8_t group;
    uint8_t request_type;
    uint16_t flow_id;
    uint32_t deadline;
} GatewayPending;

static GatewayRxRing g_pc_rx;
static GatewayRxRing g_lora_rx;
static GatewayParser g_pc_parser;
static GatewayParser g_lora_parser;
static GatewayQueuedMessage g_pc_queue[GATEWAY_PC_QUEUE_DEPTH];
static uint8_t g_pc_queue_head;
static uint8_t g_pc_queue_tail;
static uint8_t g_pc_queue_count;
static GatewayPending g_pending;
static uint16_t g_next_auto_flow;
static uint32_t g_next_poll_tick;
static uint8_t g_next_poll_group;
static GatewaySendCallback g_send_callback;
static void *g_send_context;

static uint16_t Gateway_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc = (uint16_t)(crc ^ data[index]);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static uint8_t Gateway_IsValidGroup(uint8_t group)
{
    return ((group >= GATEWAY_FIRST_GROUP) &&
            (group <= GATEWAY_LAST_GROUP)) ? 1U : 0U;
}

static uint8_t Gateway_IsValidPayload(const GatewayMessage *message)
{
    if (message == NULL)
    {
        return 0U;
    }

    switch (message->type)
    {
        case GATEWAY_TYPE_READ_TELEMETRY:
            return ((message->payload_length == 1U) && (message->payload[0] <= 1U)) ? 1U : 0U;
        case GATEWAY_TYPE_TELEMETRY:
            if ((message->source_role != GATEWAY_ROLE_MASTER) ||
                (message->destination_role != GATEWAY_ROLE_CONTROL_ROOM))
            {
                return 0U;
            }
            if (message->payload_length == GATEWAY_TELEMETRY_PAYLOAD_SIZE)
            {
                return ((message->payload[0] == 0x0BU) ||
                        (message->payload[0] == 0x0FU)) ? 1U : 0U;
            }
            if ((message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_BYTES) ||
                (message->payload_length == LORA_PROTOCOL_ULTRASONIC_TELEMETRY_BYTES))
            {
                uint16_t value[4];
                uint8_t i;
                uint8_t flags = (message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_BYTES) ? 0x1BU : 0x3BU;
                if ((message->payload[0] != flags) && (message->payload[0] != (uint8_t)(flags | 4U)))
                { return 0U; }
                for (i = 0U; i < 4U; i++)
                {
                    uint8_t offset = (uint8_t)(26U + 2U * i);
                    value[i] = (uint16_t)((uint16_t)message->payload[offset] |
                        ((uint16_t)message->payload[offset + 1U] << 8U));
                }
                if (!((value[0] == 0xFFFFU) && (value[1] == 0xFFFFU) &&
                      (value[2] == 0xFFFFU) && (value[3] == 0xFFFFU)) &&
                    !((value[0] <= 4095U) && (value[1] <= 3600U) &&
                      (value[2] <= 7200U) && (value[3] < 2000U))) { return 0U; }
                if (message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_BYTES) { return 1U; }
                for (i = 0U; i < 4U; i++)
                {
                    uint8_t offset = (uint8_t)(34U + 2U * i);
                    value[i] = (uint16_t)((uint16_t)message->payload[offset] |
                        ((uint16_t)message->payload[offset + 1U] << 8U));
                }
                if ((value[0] == 0xFFFFU) && (value[1] == 0xFFFFU) &&
                    (value[2] == 0xFFFFU) && (value[3] == 0xFFFFU)) { return 1U; }
                return ((value[0] >= 100U) && (value[0] <= 500U) &&
                        (value[1] >= 100U) && (value[1] <= 500U) &&
                        (value[2] > 0U) && (value[2] < 10000U) && (value[3] < 2000U)) ? 1U : 0U;
            }
            if (message->payload_length == LORA_PROTOCOL_LEGACY_TELEMETRY_BYTES)
            {
                return ((message->payload[0] == 0U) || (message->payload[0] == 1U) ||
                        (message->payload[0] == 3U) || (message->payload[0] == 7U)) ? 1U : 0U;
            }
            return 0U;
        case GATEWAY_TYPE_SET_FAN_SPEED:
            return ((message->payload_length == 2U) &&
                    (message->payload[0] >= 1U) &&
                    (message->payload[0] <= 4U) &&
                    (message->payload[1] <= 100U)) ? 1U : 0U;
        case GATEWAY_TYPE_SET_WINDOW:
            return ((message->source_role == GATEWAY_ROLE_CONTROL_ROOM) &&
                    (message->destination_role == GATEWAY_ROLE_MASTER) &&
                    (message->payload_length == 2U) &&
                    (message->payload[0] >= 1U && message->payload[0] <= 4U) &&
                    (message->payload[1] <= 1U)) ? 1U : 0U;
        case GATEWAY_TYPE_ACK:
            return ((message->payload_length == 2U) &&
                    ((message->payload[0] != GATEWAY_TYPE_SET_WINDOW) ||
                     (message->payload[1] <= 3U))) ? 1U : 0U;
        case GATEWAY_TYPE_ERROR:
            return ((message->payload_length >= 1U) && (message->payload_length <= 16U)) ? 1U : 0U;
        default:
            return 0U;
    }
}

static void Gateway_ResetParser(GatewayParser *parser)
{
    parser->length = 0U;
    parser->expected_length = 0U;
}

static uint8_t Gateway_RingPop(GatewayRxRing *ring, uint8_t *byte)
{
    uint16_t tail;

    if (byte == NULL)
    {
        return 0U;
    }

    tail = ring->tail;
    if (tail == ring->head)
    {
        return 0U;
    }

    *byte = ring->data[tail];
    ring->tail = (uint16_t)((tail + 1U) & GATEWAY_RX_RING_MASK);
    return 1U;
}

static void Gateway_RingPushFromIsr(GatewayRxRing *ring, uint8_t byte)
{
    uint16_t head = ring->head;
    uint16_t next = (uint16_t)((head + 1U) & GATEWAY_RX_RING_MASK);

    if (next != ring->tail)
    {
        ring->data[head] = byte;
        ring->head = next;
    }
}

static uint8_t Gateway_Decode(const uint8_t *frame, uint16_t length,
                              GatewayMessage *message)
{
    uint8_t payload_length;
    uint16_t crc;

    if ((frame == NULL) || (message == NULL) || (length < GATEWAY_MIN_FRAME_SIZE) ||
        (frame[0] != GATEWAY_HEAD_1) || (frame[1] != GATEWAY_HEAD_2) ||
        (frame[2] != GATEWAY_VERSION))
    {
        return 0U;
    }

    payload_length = frame[10];
    if ((payload_length > GATEWAY_MAX_PAYLOAD_SIZE) ||
        (length != (uint16_t)(GATEWAY_MIN_FRAME_SIZE + payload_length)))
    {
        return 0U;
    }

    crc = Gateway_Crc16(&frame[2], (uint16_t)(9U + payload_length));
    if ((frame[length - 2U] != (uint8_t)(crc & 0x00FFU)) ||
        (frame[length - 1U] != (uint8_t)(crc >> 8U)))
    {
        return 0U;
    }

    message->type = frame[3];
    message->source_role = frame[4];
    message->source_group = frame[5];
    message->destination_role = frame[6];
    message->destination_group = frame[7];
    message->flow_id = (uint16_t)((uint16_t)frame[8] | ((uint16_t)frame[9] << 8U));
    message->payload_length = payload_length;
    if (payload_length != 0U)
    {
        memcpy(message->payload, &frame[11], payload_length);
    }

    return Gateway_IsValidPayload(message);
}

static uint8_t Gateway_ParseByte(GatewayParser *parser, uint8_t byte,
                                 GatewayMessage *message)
{
    if (parser->length == 0U)
    {
        if (byte == GATEWAY_HEAD_1)
        {
            parser->frame[0] = byte;
            parser->length = 1U;
        }
        return 0U;
    }

    if (parser->length == 1U)
    {
        if (byte == GATEWAY_HEAD_2)
        {
            parser->frame[1] = byte;
            parser->length = 2U;
        }
        else if (byte == GATEWAY_HEAD_1)
        {
            parser->frame[0] = byte;
        }
        else
        {
            Gateway_ResetParser(parser);
        }
        return 0U;
    }

    if (parser->length >= GATEWAY_MAX_FRAME_SIZE)
    {
        Gateway_ResetParser(parser);
        return 0U;
    }

    parser->frame[parser->length] = byte;
    parser->length++;
    if (parser->length == 11U)
    {
        if (parser->frame[10] > GATEWAY_MAX_PAYLOAD_SIZE)
        {
            Gateway_ResetParser(parser);
            return 0U;
        }
        parser->expected_length = (uint16_t)(GATEWAY_MIN_FRAME_SIZE + parser->frame[10]);
    }

    if ((parser->expected_length != 0U) && (parser->length == parser->expected_length))
    {
        uint8_t ready = Gateway_Decode(parser->frame, parser->length, message);
        Gateway_ResetParser(parser);
        return ready;
    }
    return 0U;
}

static uint8_t Gateway_IsPcCommand(const GatewayMessage *message)
{
    if ((message == NULL) || (message->source_group != 0U))
    {
        return 0U;
    }

    if ((message->source_role != GATEWAY_ROLE_CONTROL_ROOM) ||
        (message->destination_role != GATEWAY_ROLE_MASTER))
    {
        return 0U;
    }

    switch (message->type)
    {
        case GATEWAY_TYPE_READ_TELEMETRY:
        case GATEWAY_TYPE_SET_FAN_SPEED:
        case GATEWAY_TYPE_SET_WINDOW:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t Gateway_EncodeAndSend(const GatewayMessage *message,
                                     GatewayOutputPort port)
{
    uint8_t frame[GATEWAY_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t crc;

    if ((message == NULL) || (message->payload_length > GATEWAY_MAX_PAYLOAD_SIZE) ||
        (Gateway_IsValidPayload(message) == 0U) || (g_send_callback == NULL))
    {
        return 0U;
    }

    frame[0] = GATEWAY_HEAD_1;
    frame[1] = GATEWAY_HEAD_2;
    frame[2] = GATEWAY_VERSION;
    frame[3] = message->type;
    frame[4] = message->source_role;
    frame[5] = message->source_group;
    frame[6] = message->destination_role;
    frame[7] = message->destination_group;
    frame[8] = (uint8_t)(message->flow_id & 0x00FFU);
    frame[9] = (uint8_t)(message->flow_id >> 8U);
    frame[10] = message->payload_length;
    if (message->payload_length != 0U)
    {
        memcpy(&frame[11], message->payload, message->payload_length);
    }

    length = (uint16_t)(GATEWAY_MIN_FRAME_SIZE + message->payload_length);
    crc = Gateway_Crc16(&frame[2], (uint16_t)(9U + message->payload_length));
    frame[length - 2U] = (uint8_t)(crc & 0x00FFU);
    frame[length - 1U] = (uint8_t)(crc >> 8U);
    return g_send_callback(port, frame, length, g_send_context);
}

static void Gateway_SendLocalError(const GatewayMessage *request,
                                   uint8_t error_code)
{
    GatewayMessage error;

    if (request == NULL)
    {
        return;
    }

    (void)memset(&error, 0, sizeof(error));
    error.type = GATEWAY_TYPE_ERROR;
    error.source_role = GATEWAY_ROLE_CONTROL_ROOM;
    error.source_group = 0U;
    error.destination_role = GATEWAY_ROLE_MASTER;
    error.destination_group = request->destination_group;
    error.flow_id = request->flow_id;
    error.payload_length = 1U;
    error.payload[0] = error_code;
    (void)Gateway_EncodeAndSend(&error, GATEWAY_OUTPUT_PC);
}

static uint8_t Gateway_Start(const GatewayMessage *message, uint8_t automatic_poll,
                             uint32_t now_ms)
{
    if (Gateway_EncodeAndSend(message, GATEWAY_OUTPUT_LORA) == 0U)
    {
        return 0U;
    }

    g_pending.active = 1U;
    g_pending.automatic_poll = automatic_poll;
    g_pending.group = message->destination_group;
    g_pending.request_type = message->type;
    g_pending.flow_id = message->flow_id;
    g_pending.deadline = now_ms + ((message->type == GATEWAY_TYPE_READ_TELEMETRY) ?
                                   GATEWAY_TEMP_TIMEOUT_MS : GATEWAY_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t Gateway_QueuePcCommand(const GatewayMessage *message)
{
    if ((message == NULL) || (g_pc_queue_count >= GATEWAY_PC_QUEUE_DEPTH))
    {
        return 0U;
    }

    g_pc_queue[g_pc_queue_head].message = *message;
    g_pc_queue_head = (uint8_t)((g_pc_queue_head + 1U) % GATEWAY_PC_QUEUE_DEPTH);
    g_pc_queue_count++;
    return 1U;
}

static uint8_t Gateway_PendingComplete(const GatewayMessage *message)
{
    if ((message == NULL) || (g_pending.active == 0U) ||
        (message->source_role != GATEWAY_ROLE_MASTER) ||
        (message->source_group != g_pending.group) ||
        (message->flow_id != g_pending.flow_id))
    {
        return 0U;
    }

    if (message->type == GATEWAY_TYPE_ERROR)
    {
        return 1U;
    }
    if (g_pending.request_type == GATEWAY_TYPE_READ_TELEMETRY)
    {
        return (message->type == GATEWAY_TYPE_TELEMETRY) ? 1U : 0U;
    }
    return ((message->type == GATEWAY_TYPE_ACK) &&
            (message->payload_length == 2U) &&
            (message->payload[0] == g_pending.request_type)) ? 1U : 0U;
}

static void Gateway_HandleLoRa(const GatewayMessage *message)
{
    if ((message == NULL) || (message->source_role != GATEWAY_ROLE_MASTER) ||
        (Gateway_IsValidGroup(message->source_group) == 0U) ||
        (message->destination_role != GATEWAY_ROLE_CONTROL_ROOM) ||
        (message->destination_group != 0U))
    {
        return;
    }

    /* 上位机与 LoRa 用同一帧格式，主机结果不改变内容直接转发。 */
    (void)Gateway_EncodeAndSend(message, GATEWAY_OUTPUT_PC);
    if (Gateway_PendingComplete(message) != 0U)
    {
        g_pending.active = 0U;
    }
}

static uint8_t Gateway_DeadlineReached(uint32_t now_ms, uint32_t deadline)
{
    return ((uint32_t)(now_ms - deadline) < 0x80000000UL) ? 1U : 0U;
}

static void Gateway_SendTimeoutToPc(void)
{
    GatewayMessage error;

    if (g_pending.automatic_poll != 0U)
    {
        return;
    }

    (void)memset(&error, 0, sizeof(error));
    error.type = GATEWAY_TYPE_ERROR;
    error.source_role = GATEWAY_ROLE_CONTROL_ROOM;
    error.source_group = 0U;
    error.destination_role = GATEWAY_ROLE_MASTER;
    error.destination_group = g_pending.group;
    error.flow_id = g_pending.flow_id;
    error.payload_length = 1U;
    /* 此错误由控制室生成，仅输出上位机串口，不会发回空口。 */
    error.payload[0] = GATEWAY_ERROR_MASTER_TIMEOUT;
    (void)Gateway_EncodeAndSend(&error, GATEWAY_OUTPUT_PC);
}

void GatewayRuntime_Init(GatewaySendCallback send_callback, void *context)
{
    (void)memset(&g_pc_rx, 0, sizeof(g_pc_rx));
    (void)memset(&g_lora_rx, 0, sizeof(g_lora_rx));
    (void)memset(&g_pc_parser, 0, sizeof(g_pc_parser));
    (void)memset(&g_lora_parser, 0, sizeof(g_lora_parser));
    (void)memset(&g_pc_queue, 0, sizeof(g_pc_queue));
    (void)memset(&g_pending, 0, sizeof(g_pending));
    g_pc_queue_head = 0U;
    g_pc_queue_tail = 0U;
    g_pc_queue_count = 0U;
    g_next_auto_flow = 0x8000U;
    g_next_poll_tick = 0U;
    g_next_poll_group = GATEWAY_FIRST_GROUP;
    g_send_callback = send_callback;
    g_send_context = context;
}

void GatewayRuntime_PushPcByteFromIsr(uint8_t byte)
{
    Gateway_RingPushFromIsr(&g_pc_rx, byte);
}

void GatewayRuntime_PushLoRaByteFromIsr(uint8_t byte)
{
    Gateway_RingPushFromIsr(&g_lora_rx, byte);
}

void GatewayRuntime_Process(uint32_t now_ms)
{
    uint8_t byte;
    uint16_t count;
    GatewayMessage message;

    count = 0U;
    while ((count < GATEWAY_MAX_RX_BYTES_PER_TICK) &&
           (Gateway_RingPop(&g_pc_rx, &byte) != 0U))
    {
        if ((Gateway_ParseByte(&g_pc_parser, byte, &message) != 0U) &&
            (Gateway_IsPcCommand(&message) != 0U))
        {
            if (Gateway_IsValidGroup(message.destination_group) == 0U)
            {
                Gateway_SendLocalError(&message,
                                       GATEWAY_ERROR_STATE_NOT_ALLOWED);
            }
            else if (Gateway_QueuePcCommand(&message) == 0U)
            {
                Gateway_SendLocalError(&message, GATEWAY_ERROR_BUSY);
            }
        }
        count++;
    }

    count = 0U;
    while ((count < GATEWAY_MAX_RX_BYTES_PER_TICK) &&
           (Gateway_RingPop(&g_lora_rx, &byte) != 0U))
    {
        if (Gateway_ParseByte(&g_lora_parser, byte, &message) != 0U)
        {
            Gateway_HandleLoRa(&message);
        }
        count++;
    }

    if ((g_pending.active != 0U) && Gateway_DeadlineReached(now_ms, g_pending.deadline))
    {
        Gateway_SendTimeoutToPc();
        g_pending.active = 0U;
    }

    if (g_pending.active != 0U)
    {
        return;
    }

    if (g_pc_queue_count != 0U)
    {
        GatewayQueuedMessage queued = g_pc_queue[g_pc_queue_tail];
        if (Gateway_Start(&queued.message, 0U, now_ms) != 0U)
        {
            g_pc_queue_tail = (uint8_t)((g_pc_queue_tail + 1U) % GATEWAY_PC_QUEUE_DEPTH);
            g_pc_queue_count--;
        }
        return;
    }

    if ((GATEWAY_AUTO_POLL_ENABLED != 0U) &&
        Gateway_DeadlineReached(now_ms, g_next_poll_tick))
    {
        GatewayMessage poll;

        (void)memset(&poll, 0, sizeof(poll));
        poll.type = GATEWAY_TYPE_READ_TELEMETRY;
        poll.source_role = GATEWAY_ROLE_CONTROL_ROOM;
        poll.source_group = 0U;
        poll.destination_role = GATEWAY_ROLE_MASTER;
        poll.destination_group = g_next_poll_group;
        poll.flow_id = g_next_auto_flow;
        poll.payload_length = 1U;
        poll.payload[0] = 0U;

        if (Gateway_Start(&poll, 1U, now_ms) != 0U)
        {
            g_next_auto_flow++;
            g_next_poll_tick = now_ms + GATEWAY_POLL_INTERVAL_MS;
        }
    }
}

