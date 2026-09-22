#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <stdint.h>

#define LORA_PROTOCOL_HEAD_0               0xAAU
#define LORA_PROTOCOL_HEAD_1               0x55U
#define LORA_PROTOCOL_VERSION              0x04U
#define LORA_PROTOCOL_HEADER_SIZE          11U
#define LORA_PROTOCOL_CRC_SIZE             2U
#define LORA_PROTOCOL_MAX_PAYLOAD          128U
#define LORA_PROTOCOL_FRAME_MAX_SIZE       (LORA_PROTOCOL_HEADER_SIZE + LORA_PROTOCOL_MAX_PAYLOAD + LORA_PROTOCOL_CRC_SIZE)
#define LORA_PROTOCOL_TELEMETRY_BYTES      18U
#define LORA_PROTOCOL_TEMPERATURE_INVALID  (-32767 - 1)
#define LORA_PROTOCOL_HUMIDITY_INVALID     0xFFFFU
#define LORA_PROTOCOL_PRESSURE_INVALID     0xFFFFFFFFUL
#define LORA_PROTOCOL_SOUND_INVALID        0xFFFFU
#define LORA_PROTOCOL_RAIN_UNAVAILABLE     0xFFU

typedef enum
{
    LORA_ROLE_CONTROL = 0x01U,
    LORA_ROLE_MASTER  = 0x02U,
    LORA_ROLE_SLAVE   = 0x03U
} LoraProtocolRole;

typedef enum
{
    LORA_TYPE_READ_TELEMETRY = 0x01U,
    LORA_TYPE_TELEMETRY      = 0x02U,
    LORA_TYPE_SET_FAN_SPEED  = 0x10U,
    LORA_TYPE_ACK       = 0x20U,
    LORA_TYPE_ERROR     = 0x7EU
} LoraProtocolType;

typedef struct
{
    uint8_t type;
    uint8_t src_role;
    uint8_t src_group;
    uint8_t dst_role;
    uint8_t dst_group;
    uint16_t flow_id;
    uint8_t data_len;
    uint8_t data[LORA_PROTOCOL_MAX_PAYLOAD];
} LoraProtocolFrame;

#endif /* LORA_PROTOCOL_H */

