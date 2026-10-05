#include "lora_protocol.h"

#include <stddef.h>
#include <string.h>

#define LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE (9U)

static uint8_t ValidateMq2(const uint8_t *payload)
{
    uint16_t value[4];
    uint8_t i;
    for (i = 0U; i < 4U; i++)
    {
        uint8_t offset = (uint8_t)(LORA_TELEMETRY_MQ2_OFFSET + 2U * i);
        value[i] = (uint16_t)((uint16_t)payload[offset] |
                             ((uint16_t)payload[offset + 1U] << 8U));
    }
    if ((value[0] == 0xFFFFU) && (value[1] == 0xFFFFU) &&
        (value[2] == 0xFFFFU) && (value[3] == 0xFFFFU)) { return 1U; }
    return ((value[0] <= 4095U) && (value[1] <= 3600U) &&
            (value[2] <= 7200U) && (value[3] < LORA_TELEMETRY_MQ2_MAX_AGE_MS)) ? 1U : 0U;
}

static LoRaProtocolStatus ValidateAddress(uint8_t role, uint8_t group)
{
    if ((role < (uint8_t)LORA_ROLE_CONTROL_ROOM) ||
        (role > (uint8_t)LORA_ROLE_SLAVE))
    {
        return LORA_PROTOCOL_INVALID_ROLE;
    }
    if (role == (uint8_t)LORA_ROLE_CONTROL_ROOM)
    {
        return (group == 0U) ? LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_GROUP;
    }
    return (group == LORA_PROTOCOL_SINGLE_GROUP) ?
           LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_GROUP;
}

static LoRaProtocolStatus ValidateShape(const LoRaMessage *message)
{
    switch (message->type)
    {
        case (uint8_t)LORA_MSG_READ_TELEMETRY:
            if (message->payload_length != 1U)
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
            }
            if (message->payload[0] > 1U)
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            if (((message->source_role == (uint8_t)LORA_ROLE_CONTROL_ROOM) &&
                 (message->destination_role == (uint8_t)LORA_ROLE_MASTER)) ||
                ((message->source_role == (uint8_t)LORA_ROLE_MASTER) &&
                 (message->destination_role == (uint8_t)LORA_ROLE_SLAVE)))
            {
                return LORA_PROTOCOL_OK;
            }
            return LORA_PROTOCOL_INVALID_DIRECTION;

        case (uint8_t)LORA_MSG_TELEMETRY:
            if ((message->payload_length != LORA_PROTOCOL_TELEMETRY_SIZE) &&
                (message->payload_length != LORA_PROTOCOL_MQ2_TELEMETRY_SIZE) &&
                (message->payload_length != LORA_PROTOCOL_LEGACY_TELEMETRY_SIZE))
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
            }
            if ((message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_SIZE) &&
                (ValidateMq2(message->payload) == 0U))
            { return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE; }
            if ((message->source_role == (uint8_t)LORA_ROLE_SLAVE) &&
                (message->destination_role == (uint8_t)LORA_ROLE_MASTER))
            {
                uint8_t expected_flags = (message->payload_length == LORA_PROTOCOL_TELEMETRY_SIZE) ?
                    LORA_TELEMETRY_FLAG_ACOUSTIC : 0U;
                if (message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_SIZE)
                { expected_flags = LORA_TELEMETRY_FLAG_ACOUSTIC | LORA_TELEMETRY_FLAG_MQ2; }
                return (message->payload[0] == expected_flags) ?
                    LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            if ((message->source_role == (uint8_t)LORA_ROLE_MASTER) &&
                (message->destination_role == (uint8_t)LORA_ROLE_CONTROL_ROOM))
            {
                uint8_t flags = message->payload[0];
                if (message->payload_length == LORA_PROTOCOL_MQ2_TELEMETRY_SIZE)
                {
                    return ((flags == 0x1BU) || (flags == 0x1FU)) ?
                        LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
                }
                if (message->payload_length == LORA_PROTOCOL_TELEMETRY_SIZE)
                {
                    return ((flags == 0x0BU) || (flags == 0x0FU)) ?
                        LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
                }
                return ((flags == 0U) || (flags == 1U) || (flags == 3U) || (flags == 7U)) ?
                    LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            return LORA_PROTOCOL_INVALID_DIRECTION;

        case (uint8_t)LORA_MSG_SET_FAN_SPEED:
            if (message->payload_length != 2U)
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
            }
            if ((message->payload[0] < 1U) || (message->payload[0] > 4U) ||
                (message->payload[1] > 100U))
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            return ((message->source_role == (uint8_t)LORA_ROLE_CONTROL_ROOM) &&
                    (message->destination_role == (uint8_t)LORA_ROLE_MASTER)) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_DIRECTION;

        case (uint8_t)LORA_MSG_SET_WINDOW:
            if (message->payload_length != 2U)
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
            }
            if ((message->payload[0] < 1U) || (message->payload[0] > LORA_WINDOW_SERVO_COUNT) ||
                (message->payload[1] > 1U))
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            return (((message->source_role == (uint8_t)LORA_ROLE_CONTROL_ROOM) &&
                     (message->destination_role == (uint8_t)LORA_ROLE_MASTER)) ||
                    ((message->source_role == (uint8_t)LORA_ROLE_MASTER) &&
                     (message->destination_role == (uint8_t)LORA_ROLE_SLAVE))) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_DIRECTION;

        case (uint8_t)LORA_MSG_ACK:
            if (message->payload_length != 2U)
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
            }
            if ((message->payload[0] == LORA_MSG_SET_WINDOW) &&
                (message->payload[1] > LORA_WINDOW_STATUS_BUSY))
            {
                return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;
            }
            if ((message->source_role == (uint8_t)LORA_ROLE_MASTER) &&
                (message->destination_role == (uint8_t)LORA_ROLE_CONTROL_ROOM))
            {
                return LORA_PROTOCOL_OK;
            }
            return ((message->source_role == (uint8_t)LORA_ROLE_SLAVE) &&
                    (message->destination_role == (uint8_t)LORA_ROLE_MASTER) &&
                    (message->payload[0] == LORA_MSG_SET_WINDOW)) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_DIRECTION;

        case (uint8_t)LORA_MSG_ERROR:
            return ((message->payload_length >= 1U) &&
                    (message->payload_length <= 16U)) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        default:
            return LORA_PROTOCOL_UNSUPPORTED_TYPE;
    }
}

uint16_t LoRaProtocol_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }
    for (index = 0U; index < length; index++)
    {
        crc = (uint16_t)(crc ^ data[index]);
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

LoRaProtocolStatus LoRaProtocol_ValidateMessage(const LoRaMessage *message)
{
    LoRaProtocolStatus status;

    if (message == NULL)
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }
    if (message->version != LORA_PROTOCOL_VERSION)
    {
        return LORA_PROTOCOL_UNSUPPORTED_VERSION;
    }
    status = ValidateAddress(message->source_role, message->source_group);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }
    status = ValidateAddress(message->destination_role,
                             message->destination_group);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }
    if (message->payload_length > LORA_PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
    }
    return ValidateShape(message);
}

LoRaProtocolStatus LoRaProtocol_Encode(const LoRaMessage *message,
                                       uint8_t *output,
                                       uint16_t output_size,
                                       uint16_t *output_length)
{
    LoRaProtocolStatus status;
    uint16_t frame_length;
    uint16_t crc;

    if ((message == NULL) || (output == NULL) || (output_length == NULL))
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }
    status = LoRaProtocol_ValidateMessage(message);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }
    frame_length = (uint16_t)(LORA_PROTOCOL_MIN_FRAME_SIZE +
                              message->payload_length);
    if (output_size < frame_length)
    {
        return LORA_PROTOCOL_OUTPUT_TOO_SMALL;
    }

    output[0] = LORA_PROTOCOL_HEADER_1;
    output[1] = LORA_PROTOCOL_HEADER_2;
    output[2] = message->version;
    output[3] = message->type;
    output[4] = message->source_role;
    output[5] = message->source_group;
    output[6] = message->destination_role;
    output[7] = message->destination_group;
    output[8] = (uint8_t)(message->flow_id & 0xFFU);
    output[9] = (uint8_t)(message->flow_id >> 8U);
    output[10] = message->payload_length;
    if (message->payload_length != 0U)
    {
        memcpy(&output[11], message->payload, message->payload_length);
    }
    crc = LoRaProtocol_Crc16(&output[2],
        (uint16_t)(LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE + message->payload_length));
    output[frame_length - 2U] = (uint8_t)(crc & 0xFFU);
    output[frame_length - 1U] = (uint8_t)(crc >> 8U);
    *output_length = frame_length;
    return LORA_PROTOCOL_OK;
}

LoRaProtocolStatus LoRaProtocol_Decode(const uint8_t *frame,
                                       uint16_t frame_length,
                                       LoRaMessage *message)
{
    uint8_t payload_length;
    uint16_t expected_length;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if ((frame == NULL) || (message == NULL))
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }
    if (frame_length < LORA_PROTOCOL_MIN_FRAME_SIZE)
    {
        return LORA_PROTOCOL_FRAME_TOO_SHORT;
    }
    if ((frame[0] != LORA_PROTOCOL_HEADER_1) ||
        (frame[1] != LORA_PROTOCOL_HEADER_2))
    {
        return LORA_PROTOCOL_INVALID_HEADER;
    }
    payload_length = frame[10];
    if (payload_length > LORA_PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
    }
    expected_length = (uint16_t)(LORA_PROTOCOL_MIN_FRAME_SIZE + payload_length);
    if (frame_length != expected_length)
    {
        return LORA_PROTOCOL_FRAME_LENGTH_MISMATCH;
    }
    expected_crc = LoRaProtocol_Crc16(&frame[2],
        (uint16_t)(LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE + payload_length));
    actual_crc = (uint16_t)((uint16_t)frame[frame_length - 2U] |
                 ((uint16_t)frame[frame_length - 1U] << 8U));
    if (actual_crc != expected_crc)
    {
        return LORA_PROTOCOL_CRC_MISMATCH;
    }

    memset(message, 0, sizeof(*message));
    message->version = frame[2];
    message->type = frame[3];
    message->source_role = frame[4];
    message->source_group = frame[5];
    message->destination_role = frame[6];
    message->destination_group = frame[7];
    message->flow_id = (uint16_t)((uint16_t)frame[8] |
                       ((uint16_t)frame[9] << 8U));
    message->payload_length = payload_length;
    if (payload_length != 0U)
    {
        memcpy(message->payload, &frame[11], payload_length);
    }
    return LoRaProtocol_ValidateMessage(message);
}
