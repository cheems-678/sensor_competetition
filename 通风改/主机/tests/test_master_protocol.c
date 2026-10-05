#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lora_protocol.h"
#include "lora_stream_parser.h"
#include "master_queues.h"

static uint32_t tick;
uint32_t HAL_GetTick(void) { return tick; }

static LoRaMessage telemetry(uint8_t role, uint8_t length, uint8_t flags)
{
    LoRaMessage message;
    memset(&message, 0, sizeof(message));
    message.version = 4U;
    message.type = LORA_MSG_TELEMETRY;
    message.source_role = role;
    message.source_group = 1U;
    message.destination_role = role == LORA_ROLE_SLAVE ? LORA_ROLE_MASTER : LORA_ROLE_CONTROL_ROOM;
    message.destination_group = role == LORA_ROLE_SLAVE ? 1U : 0U;
    message.flow_id = 0x1234U;
    message.payload_length = length;
    memset(message.payload, 0xFF, sizeof(message.payload));
    message.payload[0] = flags;
    return message;
}

static void test_known_layouts(void)
{
    unsigned flags;
    uint8_t role;
    uint8_t length;
    for (role = 2U; role <= 3U; role++)
    {
        for (length = 18U; length <= 34U; length += 8U)
        {
            for (flags = 0U; flags < 256U; flags++)
            {
                LoRaMessage message = telemetry(role, length, (uint8_t)flags);
                unsigned accepted = (role == 3U) ?
                    ((length == 18U && flags == 0U) || (length == 26U && flags == 8U) || (length == 34U && flags == 0x18U)) :
                    ((length == 18U && (flags == 0U || flags == 1U || flags == 3U || flags == 7U)) ||
                     (length == 26U && (flags == 0x0BU || flags == 0x0FU)) ||
                     (length == 34U && (flags == 0x1BU || flags == 0x1FU)));
                assert((LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK) == accepted);
            }
        }
    }
    for (length = 0U; length < 40U; length++)
    {
        LoRaMessage message = telemetry(2U, length, 0x0BU);
        if (length != 18U && length != 26U && length != 34U)
        { assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH); }
    }
}

static void test_encode_decode_and_stream(void)
{
    static const uint32_t sounds[][2] = {{0U, 131071U}, {65536U, 70000U}, {0xFFFFFFFFU, 0xFFFFFFFFU}};
    LoRaStreamParser parser;
    LoRaMessage decoded;
    uint8_t frame[141];
    uint16_t length;
    unsigned i, j, k;
    LoRaStreamParser_Init(&parser);
    for (i = 0U; i < sizeof(sounds) / sizeof(sounds[0]); i++)
    {
        LoRaMessage message = telemetry(2U, 26U, 0x0FU);
        for (j = 0U; j < 4U; j++)
        {
            message.payload[18U + j] = (uint8_t)(sounds[i][0] >> (8U * j));
            message.payload[22U + j] = (uint8_t)(sounds[i][1] >> (8U * j));
        }
        assert(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        assert(length == 39U && frame[10] == 26U);
        assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_OK);
        assert(decoded.flow_id == message.flow_id);
        assert(memcmp(message.payload, decoded.payload, 26U) == 0);
        assert(LoRaProtocol_Encode(&message, frame, 38U, &length) == LORA_PROTOCOL_OUTPUT_TOO_SMALL);
        /* Feed arbitrary chunk sizes through the real one-byte parser. */
        for (j = 0U; j < length; j++)
        {
            LoRaStreamResult result = LoRaStreamParser_PushByte(&parser, frame[j], &decoded);
            assert(result == (j + 1U == length ? LORA_STREAM_FRAME_READY : LORA_STREAM_WAITING));
        }
        frame[25] ^= 1U;
        assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_CRC_MISMATCH);
        for (k = 0U; k < length; k++)
        { (void)LoRaStreamParser_PushByte(&parser, frame[k], &decoded); }
    }
    for (i = 0U; i < 2U; i++)
    {
        LoRaMessage message = telemetry(2U, 18U, 7U);
        assert(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        assert(length == 31U);
        for (j = 0U; j < length; j++)
        { (void)LoRaStreamParser_PushByte(&parser, frame[j], &decoded); }
    }
    assert(parser.accepted_frame_count == 5U && parser.rejected_frame_count == 3U);
    assert(decoded.payload_length == 18U && decoded.payload[0] == 7U);
    frame[10] = 129U;
    assert(LoRaProtocol_Decode(frame, sizeof(frame), &decoded) == LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);
}

static void test_rain_slot_encode_decode_and_crc(void)
{
    static const uint8_t flags[] = {0x0BU, 0x0FU};
    static const uint8_t rain_states[] = {0U, 1U, 0xFFU};
    LoRaMessage decoded;
    uint8_t frame[141];
    uint16_t length;
    unsigned i, j, k;

    for (i = 0U; i < sizeof(flags) / sizeof(flags[0]); i++)
    {
        for (j = 0U; j < sizeof(rain_states) / sizeof(rain_states[0]); j++)
        {
            LoRaMessage message = telemetry(2U, 26U, flags[i]);
            /* Distinct neighboring fields expose accidental offset changes. */
            for (k = 1U; k < 26U; k++)
            { message.payload[k] = (uint8_t)(0x20U + k); }
            message.payload[17] = rain_states[j];
            assert(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
            assert(length == 39U && frame[10] == 26U);
            assert(frame[11] == flags[i] && frame[28] == rain_states[j]);
            assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_OK);
            assert(decoded.flow_id == message.flow_id && decoded.payload_length == 26U);
            assert(memcmp(decoded.payload, message.payload, 26U) == 0);

            frame[28] ^= 1U;
            assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_CRC_MISMATCH);
        }
    }
}

static void test_local_queue_lifetime(void)
{
    LoRaMessage message = telemetry(2U, 26U, 0x0BU), output;
    uint32_t enqueued;
    MasterEvent event, output_event;
    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.received_tick = 12U;
    event.data.lora_message = message;
    assert(MasterQueues_Init());
    assert(MasterQueues_SendEvent(&event));
    assert(MasterQueues_ReceiveEvent(&output_event));
    assert(output_event.received_tick == 12U);
    tick = 100U;
    assert(MasterQueues_SendLoRa(&message));
    tick = 1099U;
    assert(MasterQueues_ReceiveLoRaTimed(&output, &enqueued));
    assert(enqueued == 100U && !MasterQueues_IsLoRaExpired(output.type, enqueued, tick));
    tick = 1100U;
    assert(MasterQueues_IsLoRaExpired(output.type, enqueued, tick));
    /* A stale telemetry ahead of an ACK must not block or expire the ACK. */
    tick = 2000U;
    assert(MasterQueues_SendLoRa(&message));
    message.type = LORA_MSG_ACK;
    assert(MasterQueues_SendLoRa(&message));
    tick = 9000U;
    assert(MasterQueues_ReceiveLoRaTimed(&output, &enqueued));
    assert(output.type == LORA_MSG_ACK && enqueued == 2000U);
    assert(MasterQueueDiag.lora_tx_expired_count == 1U);
    assert(!MasterQueues_IsLoRaExpired(output.type, enqueued, tick));
    assert(!MasterQueues_ReceiveLoRa(&output));
    message.type = LORA_MSG_TELEMETRY;
    tick = 0xFFFFFF00U;
    assert(MasterQueues_SendLoRa(&message));
    tick = 743U; /* age=999 */
    assert(MasterQueues_ReceiveLoRaTimed(&output, &enqueued));
    assert(enqueued == 0xFFFFFF00U);
    assert(!MasterQueues_IsLoRaExpired(output.type, enqueued, tick));
    assert(MasterQueues_IsLoRaExpired(output.type, enqueued, 744U));
    tick = 0xFFFFFF00U;
    assert(MasterQueues_SendLoRa(&message));
    tick = 744U;
    assert(!MasterQueues_ReceiveLoRaTimed(&output, &enqueued));
    assert(MasterQueueDiag.lora_tx_expired_count == 2U);
    assert(!MasterQueues_ReceiveLoRaTimed(NULL, &enqueued));
    assert(!MasterQueues_ReceiveLoRaTimed(&output, NULL));
    message.type = LORA_MSG_READ_TELEMETRY;
    tick = 0U;
    assert(MasterQueues_SendLoRa(&message));
    tick = 100000U;
    assert(MasterQueues_ReceiveLoRa(&output));
    assert(output.type == LORA_MSG_READ_TELEMETRY);
}

static LoRaMessage window_command(uint8_t action)
{
    LoRaMessage message;
    memset(&message, 0, sizeof(message));
    message.version = 4U; message.type = LORA_MSG_SET_WINDOW;
    message.source_role = LORA_ROLE_CONTROL_ROOM; message.source_group = 0U;
    message.destination_role = LORA_ROLE_MASTER; message.destination_group = 1U;
    message.flow_id = 0x0300U;
    message.payload_length = 2U; message.payload[0] = 1U; message.payload[1] = action;
    return message;
}

static void test_window_shape_directions_and_ack(void)
{
    LoRaMessage message = window_command(0U);
    uint8_t source, destination;
    unsigned slot, action, status;

    for (source = 1U; source <= 3U; source++)
    {
        for (destination = 1U; destination <= 3U; destination++)
        {
            message.source_role = source; message.source_group = source == 1U ? 0U : 1U;
            message.destination_role = destination;
            message.destination_group = destination == 1U ? 0U : 1U;
            assert((LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK) ==
                   ((source == 1U && destination == 2U) || (source == 2U && destination == 3U)));
        }
    }
    message = window_command(0U);
    for (slot = 0U; slot < 256U; slot++)
    {
        for (action = 0U; action < 256U; action++)
        {
            message.payload[0] = (uint8_t)slot; message.payload[1] = (uint8_t)action;
            assert((LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK) ==
                   (slot >= 1U && slot <= 4U && action <= 1U));
        }
    }
    message = window_command(0U);
    message.payload_length = 1U;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);
    message.payload_length = 3U;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);
    message = window_command(0U);
    message.type = LORA_MSG_ACK;
    message.source_role = LORA_ROLE_SLAVE; message.source_group = 1U;
    message.destination_role = LORA_ROLE_MASTER;
    message.payload[0] = LORA_MSG_SET_WINDOW;
    for (status = 0U; status < 256U; status++)
    {
        message.payload[1] = (uint8_t)status;
        assert((LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK) == (status <= 3U));
    }
    message.payload[1] = 0U; message.payload[0] = LORA_MSG_SET_FAN_SPEED;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_DIRECTION);
    message.payload[0] = LORA_MSG_READ_TELEMETRY;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_DIRECTION);
    message.payload[0] = LORA_MSG_SET_WINDOW;
    message.destination_role = LORA_ROLE_CONTROL_ROOM; message.destination_group = 0U;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_DIRECTION);
    message.source_role = LORA_ROLE_MASTER;
    for (status = 0U; status < 256U; status++)
    {
        message.payload[1] = (uint8_t)status;
        assert((LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK) == (status <= 3U));
    }
    message.payload[1] = 0U; message.payload_length = 1U;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);
}

static void test_window_wire_crc_stream_and_queue(void)
{
    LoRaMessage message = window_command(1U), decoded;
    LoRaStreamParser parser;
    uint8_t frame[141];
    uint16_t length;
    unsigned phase, index;
    LoRaStreamParser_Init(&parser);
    for (phase = 0U; phase < 3U; phase++)
    {
        if (phase == 1U)
        {
            message.source_role = LORA_ROLE_MASTER; message.source_group = 1U;
            message.destination_role = LORA_ROLE_SLAVE; message.destination_group = 1U;
        }
        if (phase == 2U)
        {
            message.type = LORA_MSG_ACK;
            message.source_role = LORA_ROLE_SLAVE; message.destination_role = LORA_ROLE_MASTER;
            message.payload[0] = LORA_MSG_SET_WINDOW; message.payload[1] = 0U;
        }
        assert(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK);
        assert(length == 15U && frame[10] == 2U);
        assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_OK);
        assert(decoded.flow_id == 0x0300U && decoded.type == message.type);
        assert(memcmp(decoded.payload, message.payload, 2U) == 0);
        for (index = 0U; index < length; index++)
        {
            LoRaStreamResult result = LoRaStreamParser_PushByte(&parser, frame[index], &decoded);
            assert(result == (index + 1U == length ? LORA_STREAM_FRAME_READY : LORA_STREAM_WAITING));
        }
        frame[12] ^= 1U;
        assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_CRC_MISMATCH);
    }
    assert(parser.accepted_frame_count == 3U);
    assert(MasterQueues_Init());
    message = window_command(1U); tick = 10U;
    assert(MasterQueues_SendLoRa(&message));
    tick = 100000U;
    assert(!MasterQueues_IsLoRaExpired(LORA_MSG_SET_WINDOW, 10U, tick));
    assert(MasterQueues_ReceiveLoRa(&decoded) && decoded.type == LORA_MSG_SET_WINDOW);
    /* Runtime/type+flow cancellation, not the telemetry TTL, governs this request. */
}

static void test_mq2_fields_bounds_crc_and_stream(void)
{
    const uint16_t maximum[] = {4095U, 3600U, 7200U, 1999U};
    LoRaMessage message = telemetry(2U, 34U, 0x1FU), decoded;
    LoRaStreamParser parser;
    uint8_t frame[141]; uint16_t length; unsigned i;
    for (i = 0U; i < 4U; i++)
    { message.payload[26U + i * 2U] = (uint8_t)maximum[i]; message.payload[27U + i * 2U] = (uint8_t)(maximum[i] >> 8U); }
    assert(LoRaProtocol_Encode(&message, frame, sizeof(frame), &length) == LORA_PROTOCOL_OK && length == 47U);
    LoRaStreamParser_Init(&parser);
    for (i = 0U; i < length; i++)
    { assert(LoRaStreamParser_PushByte(&parser, frame[i], &decoded) == (i + 1U == length ? LORA_STREAM_FRAME_READY : LORA_STREAM_WAITING)); }
    assert(memcmp(message.payload, decoded.payload, 34U) == 0);
    frame[37] ^= 1U;
    assert(LoRaProtocol_Decode(frame, length, &decoded) == LORA_PROTOCOL_CRC_MISMATCH);
    for (i = 0U; i < 4U; i++)
    {
        uint16_t invalid = (uint16_t)(maximum[i] + 1U);
        message.payload[26U + 2U * i] = (uint8_t)invalid;
        message.payload[27U + 2U * i] = (uint8_t)(invalid >> 8U);
        assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);
        message.payload[26U + 2U * i] = (uint8_t)maximum[i];
        message.payload[27U + 2U * i] = (uint8_t)(maximum[i] >> 8U);
    }
    memset(&message.payload[26], 0, 8U);
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK);
    message.payload[26] = message.payload[27] = 0xFFU;
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);
    memset(&message.payload[26], 0xFF, 8U);
    assert(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK);
}

int main(void)
{
    test_mq2_fields_bounds_crc_and_stream();
    test_known_layouts();
    test_encode_decode_and_stream();
    test_rain_slot_encode_decode_and_crc();
    test_local_queue_lifetime();
    test_window_shape_directions_and_ack();
    test_window_wire_crc_stream_and_queue();
    puts("7 master protocol/parser/MQ2/queue/window groups passed (1536 layouts, 6 rain cases, 65536 window payloads)");
    return 0;
}
