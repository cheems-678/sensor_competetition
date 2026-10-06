#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <stdint.h>
#include "../../monitor_status_wire.h"
#include "../../web_control_wire.h"

#define LORA_PROTOCOL_HEADER_1              (0xAAU)
#define LORA_PROTOCOL_HEADER_2              (0x55U)
#define LORA_PROTOCOL_VERSION               (0x04U)
#define LORA_PROTOCOL_MAX_PAYLOAD_SIZE      (128U)
#define LORA_PROTOCOL_MIN_FRAME_SIZE        (13U)
#define LORA_PROTOCOL_MAX_FRAME_SIZE        (141U)

#define LORA_PROTOCOL_TELEMETRY_SIZE        (26U)
#define LORA_PROTOCOL_MQ2_TELEMETRY_SIZE    (34U)
#define LORA_PROTOCOL_ULTRASONIC_TELEMETRY_SIZE (42U)
#define LORA_PROTOCOL_LEGACY_TELEMETRY_SIZE (18U)
#define LORA_PROTOCOL_SINGLE_GROUP          (1U)
#define LORA_WINDOW_SERVO_ID                (1U) /* Historical default */
#define LORA_WINDOW_SERVO_COUNT             (4U)
#define LORA_WINDOW_STATUS_OK               (0U)
#define LORA_WINDOW_STATUS_DRIVER_ERROR     (1U)
#define LORA_WINDOW_STATUS_TIMEOUT          (2U)
#define LORA_WINDOW_STATUS_BUSY             (3U)
#define LORA_PROTOCOL_TEMPERATURE_INVALID   (-32767 - 1)
#define LORA_PROTOCOL_HUMIDITY_INVALID      (0xFFFFU)
#define LORA_PROTOCOL_PRESSURE_INVALID      (0xFFFFFFFFUL)
#define LORA_PROTOCOL_SOUND_INVALID         (0xFFFFFFFFUL)

/* v4/26 bytes: flags 8=slave, B/F=dual BME + acoustic offline/online.
   Legacy v4/18 bytes remain accepted; remote BME is always 9..16. */
#define LORA_TELEMETRY_FLAG_MASTER_BME       (0x01U)
#define LORA_TELEMETRY_FLAG_DUAL_BME         (0x02U)
#define LORA_TELEMETRY_FLAG_SLAVE_ONLINE     (0x04U)
#define LORA_TELEMETRY_FLAG_ACOUSTIC         (0x08U)
#define LORA_TELEMETRY_FLAG_MQ2              (0x10U)
#define LORA_TELEMETRY_FLAG_ULTRASONIC       (0x20U)
#define LORA_TELEMETRY_ULTRASONIC_OFFSET     (34U)
#define LORA_TELEMETRY_ULTRASONIC_AGE_OFFSET (40U)
#define LORA_TELEMETRY_ULTRASONIC_MAX_AGE_MS (2000UL)
#define LORA_TELEMETRY_MQ2_OFFSET            (26U)
#define LORA_TELEMETRY_MQ2_AGE_OFFSET        (32U)
#define LORA_TELEMETRY_MQ2_MAX_AGE_MS        (2000UL)
#define LORA_TELEMETRY_REMOTE_BME_TEMP_OFFSET (9U)
#define LORA_TELEMETRY_REMOTE_BME_HUM_OFFSET  (11U)
#define LORA_TELEMETRY_REMOTE_BME_PRESSURE_OFFSET (13U)
#define LORA_TELEMETRY_FLAGS_OFFSET          (0U)
#define LORA_TELEMETRY_SLAVE_TEMP_OFFSET     (1U)
#define LORA_TELEMETRY_SLAVE_HUM_OFFSET      (3U)
#define LORA_TELEMETRY_SLAVE_PRESSURE_OFFSET (5U)
#define LORA_TELEMETRY_SOUND_1_OFFSET        (18U)
#define LORA_TELEMETRY_SOUND_2_OFFSET        (22U)
#define LORA_TELEMETRY_RAIN_OFFSET           (17U) /* 26-byte master PA11: 0/1/FF */
#define LORA_TELEMETRY_MASTER_TEMP_OFFSET    (14U)
#define LORA_TELEMETRY_MASTER_HUM_OFFSET     (16U)
#define LORA_TELEMETRY_BME_TEMP_OFFSET       LORA_TELEMETRY_SLAVE_TEMP_OFFSET
#define LORA_TELEMETRY_BME_HUM_OFFSET        LORA_TELEMETRY_SLAVE_HUM_OFFSET
#define LORA_TELEMETRY_BME_PRESSURE_OFFSET   LORA_TELEMETRY_SLAVE_PRESSURE_OFFSET

typedef enum
{
    LORA_ROLE_CONTROL_ROOM = 0x01,
    LORA_ROLE_MASTER       = 0x02,
    LORA_ROLE_SLAVE        = 0x03
} LoRaRole;

typedef enum
{
    LORA_MSG_READ_TELEMETRY = 0x01,
    LORA_MSG_TELEMETRY      = 0x02,
    LORA_MSG_MASTER_STATUS  = MONITOR_STATUS_TYPE,
    LORA_MSG_SET_FAN_SPEED  = 0x10,
    LORA_MSG_SET_WINDOW     = 0x11,
    LORA_MSG_ACK            = 0x20,
    LORA_MSG_ERROR          = 0x7E
} LoRaMessageType;

typedef enum
{
    LORA_PROTOCOL_OK = 0,
    LORA_PROTOCOL_NULL_POINTER,
    LORA_PROTOCOL_FRAME_TOO_SHORT,
    LORA_PROTOCOL_INVALID_HEADER,
    LORA_PROTOCOL_UNSUPPORTED_VERSION,
    LORA_PROTOCOL_INVALID_ROLE,
    LORA_PROTOCOL_INVALID_GROUP,
    LORA_PROTOCOL_INVALID_DIRECTION,
    LORA_PROTOCOL_UNSUPPORTED_TYPE,
    LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH,
    LORA_PROTOCOL_INVALID_PAYLOAD_VALUE,
    LORA_PROTOCOL_FRAME_LENGTH_MISMATCH,
    LORA_PROTOCOL_CRC_MISMATCH,
    LORA_PROTOCOL_OUTPUT_TOO_SMALL
} LoRaProtocolStatus;

typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[LORA_PROTOCOL_MAX_PAYLOAD_SIZE];
} LoRaMessage;

uint16_t LoRaProtocol_Crc16(const uint8_t *data, uint16_t length);
LoRaProtocolStatus LoRaProtocol_ValidateMessage(const LoRaMessage *message);
LoRaProtocolStatus LoRaProtocol_Encode(const LoRaMessage *message,
                                       uint8_t *output,
                                       uint16_t output_size,
                                       uint16_t *output_length);
LoRaProtocolStatus LoRaProtocol_Decode(const uint8_t *frame,
                                       uint16_t frame_length,
                                       LoRaMessage *message);

#endif /* LORA_PROTOCOL_H */
