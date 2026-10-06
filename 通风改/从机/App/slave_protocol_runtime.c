#include "slave_web_control.h"
#include "slave_protocol_runtime.h"

#include "lora.h"
#include "slave_bme280.h"
#include "slave_acoustic.h"
#include "slave_mq2.h"
#include "slave_hcsr04.h"
#include "slave_servo_test.h"
#include "slave_master_status.h"
#include "sg90_test_pwm.h"

#include <string.h>

#define FRAME_HEAD_1                 (0xAAU)
#define FRAME_HEAD_2                 (0x55U)
#define FRAME_VERSION                (0x04U)
#define FRAME_MAX_PAYLOAD            (128U)
#define FRAME_MIN_SIZE               (13U)
#define FRAME_MAX_SIZE               (141U)
#define FRAME_TELEMETRY_PAYLOAD_SIZE (42U)
#define FRAME_ACOUSTIC_LAYOUT        (0x38U)
#define FRAME_ROLE_MASTER            (0x02U)
#define FRAME_ROLE_SLAVE             (0x03U)
#define FRAME_TYPE_READ_TELEMETRY    (0x01U)
#define FRAME_TYPE_TELEMETRY         (0x02U)
#define FRAME_TYPE_SET_WINDOW        (0x11U)
#define FRAME_TYPE_ACK               (0x20U)
#define FRAME_WINDOW_ACK_SIZE        (15U)
#define RX_RING_SIZE                 (256U)
#define RX_RING_MASK                 (RX_RING_SIZE - 1U)
#define TX_RETRY_COUNT               (3U)
#define SLAVE_REPLY_DELAY_MS         (50UL)

typedef struct
{
    volatile uint8_t data[RX_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
} SlaveRxRing;

typedef struct
{
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} SlaveMessage;

static SlaveRxRing g_rx_ring;
static uint8_t g_frame[FRAME_MAX_SIZE];
static uint16_t g_frame_length;
static uint16_t g_expected_length;
static uint8_t g_local_group;
static uint8_t g_application_mode;
static uint8_t g_tx_pending;
static uint8_t g_tx_frame[FRAME_MAX_SIZE];
static uint16_t g_tx_frame_length;
static uint8_t g_tx_attempt_count;
static uint32_t g_tx_not_before_tick;
static uint32_t g_current_tick;
static uint8_t g_last_temp_frame[FRAME_MAX_SIZE];
static uint16_t g_last_temp_frame_length;
static uint16_t g_last_temp_flow_id;
static uint8_t g_last_temp_valid;
static uint32_t g_last_temp_tick;
static uint8_t g_last_force;
static uint8_t g_force_pending;
static uint16_t g_force_flow;
static uint32_t g_force_generation;
static volatile uint32_t g_last_rx_tick;
static uint32_t g_seen_overflow;
static uint32_t g_tx_sample_tick, g_last_sample_tick;
static uint8_t g_tx_sample_valid, g_last_sample_valid;
static SlaveAcousticSnapshot g_tx_acoustic, g_last_acoustic;
static uint8_t g_tx_acoustic_valid, g_last_acoustic_valid;
static uint32_t g_tx_mq2_tick, g_last_mq2_tick;
static uint32_t g_tx_mq2_errors, g_last_mq2_errors;
static uint8_t g_tx_mq2_valid, g_last_mq2_valid;
static uint32_t g_tx_ultrasonic_tick, g_last_ultrasonic_tick;
static uint32_t g_tx_ultrasonic_errors, g_last_ultrasonic_errors;
static uint8_t g_tx_ultrasonic_valid, g_last_ultrasonic_valid;
/* Window ACK never borrows the telemetry frame, cache or forced sample slot. */
static uint8_t g_window_ack_pending;
static uint8_t g_window_ack_frame[FRAME_WINDOW_ACK_SIZE];
static uint8_t g_window_ack_attempt_count;
static uint8_t g_window_action;
static uint8_t g_window_servo_id;
static uint16_t g_window_flow_id;
static uint32_t g_window_ack_not_before_tick;

SlaveRuntimeDiagnostics SlaveRuntimeDiag;

static uint16_t SlaveRuntime_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++)
    {
        crc = (uint16_t)(crc ^ data[i]);
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) :
                                      (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}
static uint8_t SlaveRuntime_DeadlineReached(uint32_t now_ms,
                                            uint32_t deadline)
{
    return ((uint32_t)(now_ms - deadline) < 0x80000000UL) ? 1U : 0U;
}

static uint8_t SlaveRuntime_PopRx(uint8_t *byte)
{
    uint16_t tail;

    if (byte == NULL)
    {
        return 0U;
    }
    tail = g_rx_ring.tail;
    if (tail == g_rx_ring.head)
    {
        return 0U;
    }
    *byte = g_rx_ring.data[tail];
    g_rx_ring.tail = (uint16_t)((tail + 1U) & RX_RING_MASK);
    return 1U;
}

static void SlaveRuntime_ResetParser(void)
{
    g_frame_length = 0U;
    g_expected_length = 0U;
}

static uint8_t SlaveRuntime_Decode(const uint8_t *frame, uint16_t length,
                                   SlaveMessage *message)
{
    uint8_t payload_length;
    uint16_t crc;

    if ((frame == NULL) || (message == NULL) || (length < FRAME_MIN_SIZE) ||
        (frame[0] != FRAME_HEAD_1) || (frame[1] != FRAME_HEAD_2) ||
        (frame[2] != FRAME_VERSION))
    {
        return 0U;
    }
    payload_length = frame[10];
    if ((payload_length > FRAME_MAX_PAYLOAD) ||
        (length != (uint16_t)(FRAME_MIN_SIZE + payload_length)))
    {
        return 0U;
    }
    crc = SlaveRuntime_Crc16(&frame[2], (uint16_t)(9U + payload_length));
    if ((frame[length - 2U] != (uint8_t)(crc & 0xFFU)) ||
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
    return 1U;
}

static void SlaveRuntime_WriteU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void SlaveRuntime_WriteU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static void SlaveRuntime_UpdateTxCrc(void)
{
    uint16_t crc = SlaveRuntime_Crc16(&g_tx_frame[2],
                                     (uint16_t)(9U + g_tx_frame[10]));
    g_tx_frame[g_tx_frame_length - 2U] = (uint8_t)crc;
    g_tx_frame[g_tx_frame_length - 1U] = (uint8_t)(crc >> 8U);
}

static void SlaveRuntime_BuildResponse(uint8_t request_type, uint16_t flow_id)
{
    uint8_t payload_length = FRAME_TELEMETRY_PAYLOAD_SIZE;
    SlaveBme280Sample sample;
    SlaveMq2Sample mq2;
    SlaveHcsr04Sample ultrasonic;

    g_tx_frame[0] = FRAME_HEAD_1;
    g_tx_frame[1] = FRAME_HEAD_2;
    g_tx_frame[2] = FRAME_VERSION;
    g_tx_frame[3] = FRAME_TYPE_TELEMETRY;
    g_tx_frame[4] = FRAME_ROLE_SLAVE;
    g_tx_frame[5] = g_local_group;
    g_tx_frame[6] = FRAME_ROLE_MASTER;
    g_tx_frame[7] = g_local_group;
    g_tx_frame[8] = (uint8_t)(flow_id & 0xFFU);
    g_tx_frame[9] = (uint8_t)(flow_id >> 8U);
    g_tx_frame[10] = payload_length;

    /* Slave response uses the original first BME slots; master aggregates it. */
    memset(&g_tx_frame[11], 0xFF, payload_length);
    g_tx_frame[11] = FRAME_ACOUSTIC_LAYOUT;
    SlaveRuntime_WriteU16(&g_tx_frame[12],
                          (uint16_t)SLAVE_TEMPERATURE_INVALID_X10);
    SlaveRuntime_WriteU16(&g_tx_frame[14], SLAVE_HUMIDITY_INVALID_X10);
    SlaveRuntime_WriteU32(&g_tx_frame[16], SLAVE_PRESSURE_INVALID_PA);
    g_tx_sample_valid = 0U;
    if (SlaveBme280_GetSample(g_current_tick, &sample) != 0U)
    {
        g_tx_sample_tick = SlaveBme280Diag.sample_tick;
        g_tx_sample_valid = 1U;
        SlaveRuntime_WriteU16(&g_tx_frame[12], (uint16_t)sample.temperature_x10);
        SlaveRuntime_WriteU16(&g_tx_frame[14], sample.humidity_x10);
        SlaveRuntime_WriteU32(&g_tx_frame[16], sample.pressure_pa);
    }

    g_tx_acoustic_valid = SlaveAcoustic_GetLatest(g_current_tick,
                                                &g_tx_acoustic);
    if (g_tx_acoustic_valid != 0U)
    {
        SlaveRuntime_WriteU32(&g_tx_frame[29], g_tx_acoustic.rms_left);
        SlaveRuntime_WriteU32(&g_tx_frame[33], g_tx_acoustic.rms_right);
    }

    g_tx_mq2_valid = SlaveMq2_GetSample(g_current_tick, &mq2);
    if (g_tx_mq2_valid != 0U)
    {
        g_tx_mq2_tick = mq2.tick;
        g_tx_mq2_errors = SlaveMq2Diag.error_count;
        SlaveRuntime_WriteU16(&g_tx_frame[37], mq2.raw);
        SlaveRuntime_WriteU16(&g_tx_frame[39], (uint16_t)mq2.pa7_mv);
        SlaveRuntime_WriteU16(&g_tx_frame[41], (uint16_t)mq2.ao_mv);
        SlaveRuntime_WriteU16(&g_tx_frame[43], (uint16_t)(g_current_tick - mq2.tick));
    }
    g_tx_ultrasonic_valid = SlaveHcsr04_GetSample(g_current_tick, &ultrasonic);
    if (g_tx_ultrasonic_valid != 0U)
    {
        g_tx_ultrasonic_tick = ultrasonic.tick;
        g_tx_ultrasonic_errors = SlaveHcsr04Diag.error_count;
        SlaveRuntime_WriteU16(&g_tx_frame[45], ultrasonic.distance_mm);
        SlaveRuntime_WriteU16(&g_tx_frame[47], ultrasonic.raw_mm);
        SlaveRuntime_WriteU16(&g_tx_frame[49], ultrasonic.pulse_us);
        SlaveRuntime_WriteU16(&g_tx_frame[51], (uint16_t)(g_current_tick - ultrasonic.tick));
    }
    g_tx_frame_length = (uint16_t)(FRAME_MIN_SIZE + payload_length);
    SlaveRuntime_UpdateTxCrc();

    memcpy(g_last_temp_frame, g_tx_frame, g_tx_frame_length);
    g_last_temp_frame_length = g_tx_frame_length;
    g_last_temp_flow_id = flow_id;
    g_last_temp_valid = 1U;
    g_last_temp_tick = g_current_tick;
    g_last_force = request_type;
    g_last_sample_valid = g_tx_sample_valid;
    g_last_sample_tick = g_tx_sample_tick;
    g_last_acoustic = g_tx_acoustic;
    g_last_acoustic_valid = g_tx_acoustic_valid;
    g_last_mq2_valid = g_tx_mq2_valid;
    g_last_mq2_tick = g_tx_mq2_tick;
    g_last_mq2_errors = g_tx_mq2_errors;
    g_last_ultrasonic_valid = g_tx_ultrasonic_valid;
    g_last_ultrasonic_tick = g_tx_ultrasonic_tick;
    g_last_ultrasonic_errors = g_tx_ultrasonic_errors;

    g_tx_attempt_count = 0U;
    g_tx_not_before_tick = g_current_tick + SLAVE_REPLY_DELAY_MS;
    g_tx_pending = 1U;
}

static uint8_t SlaveRuntime_QueueDuplicate(const SlaveMessage *message)
{
    const uint8_t *last_frame = NULL;
    uint16_t last_length = 0U;

    if ((message->type == FRAME_TYPE_READ_TELEMETRY) &&
        (g_last_temp_valid != 0U) &&
        ((uint32_t)(g_current_tick - g_last_temp_tick) < 2000U) &&
        (message->payload[0] == g_last_force) &&
        (message->flow_id == g_last_temp_flow_id))
    {
        last_frame = g_last_temp_frame;
        last_length = g_last_temp_frame_length;
    }
    if (last_frame == NULL)
    {
        return 0U;
    }

    SlaveRuntimeDiag.duplicate_request_count++;
    if (g_tx_pending == 0U)
    {
        memcpy(g_tx_frame, last_frame, last_length);
        g_tx_frame_length = last_length;
        g_tx_attempt_count = 0U;
        g_tx_not_before_tick = g_current_tick + SLAVE_REPLY_DELAY_MS;
        g_tx_pending = 1U;
        g_tx_sample_valid = g_last_sample_valid;
        g_tx_sample_tick = g_last_sample_tick;
        g_tx_acoustic = g_last_acoustic;
        g_tx_acoustic_valid = g_last_acoustic_valid;
        g_tx_mq2_valid = g_last_mq2_valid;
        g_tx_mq2_tick = g_last_mq2_tick;
        g_tx_mq2_errors = g_last_mq2_errors;
        g_tx_ultrasonic_valid = g_last_ultrasonic_valid;
        g_tx_ultrasonic_tick = g_last_ultrasonic_tick;
        g_tx_ultrasonic_errors = g_last_ultrasonic_errors;
    }
    return 1U;
}

static void SlaveRuntime_HandleWindow(const SlaveMessage *message)
{
    uint8_t status;
    uint16_t crc;

    if ((message->payload_length != 2U) || (message->payload[0] < 1U) || (message->payload[0] > SLAVE_SERVO_COUNT) ||
        (message->payload[1] > 1U))
    {
        SlaveRuntimeDiag.ignored_message_count++;
        return;
    }
    if (g_window_ack_pending != 0U)
    {
        if ((message->flow_id == g_window_flow_id) &&
            (message->payload[0] == g_window_servo_id) &&
            (message->payload[1] == g_window_action))
        {
            SlaveRuntimeDiag.duplicate_request_count++;
        }
        else
        {
            SlaveRuntimeDiag.ignored_message_count++;
        }
        return;
    }

    /* Reserve first: an accepted action can never lose its response slot. */
    g_window_ack_pending = 1U;
    g_window_flow_id = message->flow_id;
    g_window_servo_id = message->payload[0];
    g_window_action = message->payload[1];
    g_window_ack_attempt_count = 0U;
    SlaveRuntimeDiag.request_count++;
    SlaveRuntimeDiag.last_flow_id = message->flow_id;
    status = (SlaveServoTest_SetWindowChannel(g_window_servo_id, g_window_action, HAL_GetTick()) != 0U) ?
             0U : 1U;
    g_window_ack_not_before_tick = HAL_GetTick() +
                                 ((status == 0U) ? SG90_TEST_PERIOD_MS : 0U);
    g_window_ack_frame[0] = FRAME_HEAD_1;
    g_window_ack_frame[1] = FRAME_HEAD_2;
    g_window_ack_frame[2] = FRAME_VERSION;
    g_window_ack_frame[3] = FRAME_TYPE_ACK;
    g_window_ack_frame[4] = FRAME_ROLE_SLAVE;
    g_window_ack_frame[5] = g_local_group;
    g_window_ack_frame[6] = FRAME_ROLE_MASTER;
    g_window_ack_frame[7] = g_local_group;
    SlaveRuntime_WriteU16(&g_window_ack_frame[8], message->flow_id);
    g_window_ack_frame[10] = 2U;
    g_window_ack_frame[11] = FRAME_TYPE_SET_WINDOW;
    g_window_ack_frame[12] = status;
    crc = SlaveRuntime_Crc16(&g_window_ack_frame[2], 11U);
    SlaveRuntime_WriteU16(&g_window_ack_frame[13], crc);
}

static void SlaveRuntime_HandleMessage(const SlaveMessage *message)
{
    if ((message->source_role != FRAME_ROLE_MASTER) ||
        (message->destination_role != FRAME_ROLE_SLAVE) ||
        (message->source_group != g_local_group) ||
        (message->destination_group != g_local_group))
    {
        SlaveRuntimeDiag.ignored_message_count++;
        return;
    }
    if(message->type==WEB_RESULT){SlaveWebControl_Accept(message->flow_id,message->payload,message->payload_length,g_current_tick);return;}
    if (message->type == FRAME_TYPE_SET_WINDOW)
    {
        SlaveRuntime_HandleWindow(message);
        return;
    }
    if (message->type == MONITOR_STATUS_TYPE)
    {
        (void)SlaveMasterStatus_Accept(message->flow_id, message->payload,
                                     message->payload_length, g_current_tick);
        return;
    }
    if ((message->type != FRAME_TYPE_READ_TELEMETRY) ||
        (message->payload_length != 1U) || (message->payload[0] > 1U))
    {
        SlaveRuntimeDiag.ignored_message_count++;
        return;
    }

    if (g_force_pending != 0U)
    {
        if (message->flow_id == g_force_flow && message->payload[0] == 1U)
        { SlaveRuntimeDiag.duplicate_request_count++; }
        else { SlaveRuntimeDiag.ignored_message_count++; }
        return;
    }
    if (SlaveRuntime_QueueDuplicate(message) != 0U)
    {
        return;
    }

    if ((g_tx_pending != 0U) || (g_force_pending != 0U))
    {
        SlaveRuntimeDiag.ignored_message_count++;
        return;
    }
    SlaveRuntimeDiag.request_count++;
    SlaveRuntimeDiag.last_flow_id = message->flow_id;
    if (message->payload[0] != 0U)
    {
        g_force_pending = 1U;
        g_force_flow = message->flow_id;
        g_force_generation = SlaveBme280Diag.completed_count;
        SlaveBme280_RequestSample(g_current_tick);
    }
    else { SlaveRuntime_BuildResponse(0U, message->flow_id); }
}

static void SlaveRuntime_PushByte(uint8_t byte)
{
    SlaveMessage message;

    if (g_frame_length == 0U)
    {
        if (byte == FRAME_HEAD_1)
        {
            g_frame[0] = byte;
            g_frame_length = 1U;
        }
        return;
    }
    if (g_frame_length == 1U)
    {
        if (byte == FRAME_HEAD_2)
        {
            g_frame[1] = byte;
            g_frame_length = 2U;
        }
        else if (byte == FRAME_HEAD_1)
        {
            g_frame[0] = byte;
        }
        else
        {
            SlaveRuntime_ResetParser();
        }
        return;
    }
    if (g_frame_length >= FRAME_MAX_SIZE)
    {
        SlaveRuntime_ResetParser();
        return;
    }
    g_frame[g_frame_length++] = byte;
    if (g_frame_length == 11U)
    {
        if (g_frame[10] > FRAME_MAX_PAYLOAD)
        {
            SlaveRuntime_ResetParser();
            return;
        }
        g_expected_length = (uint16_t)(FRAME_MIN_SIZE + g_frame[10]);
    }
    if ((g_expected_length != 0U) && (g_frame_length == g_expected_length))
    {
        if (SlaveRuntime_Decode(g_frame, g_frame_length, &message) != 0U)
        {
            SlaveRuntime_HandleMessage(&message);
        }
        else
        {
            SlaveRuntimeDiag.invalid_frame_count++;
        }
        SlaveRuntime_ResetParser();
    }
}

void SlaveRuntime_Init(uint8_t local_group)
{
    SlaveMasterStatus_Init();
    SlaveWebControl_Init();
    (void)memset(&g_rx_ring, 0, sizeof(g_rx_ring));
    SlaveRuntime_ResetParser();
    g_local_group = local_group;
    g_application_mode = (local_group == 1U) ? 1U : 0U;
    g_tx_pending = 0U;
    g_tx_frame_length = 0U;
    g_tx_attempt_count = 0U;
    g_tx_not_before_tick = 0U;
    g_window_ack_pending = 0U;
    g_window_ack_attempt_count = 0U;
    g_window_action = 0U;
    g_window_servo_id = 0U;
    g_window_flow_id = 0U;
    g_window_ack_not_before_tick = 0U;
    memset(g_window_ack_frame, 0, sizeof(g_window_ack_frame));
    g_current_tick = 0U;
    g_last_temp_frame_length = 0U;
    g_last_temp_flow_id = 0U;
    g_last_temp_valid = 0U;
    g_force_pending = 0U;
    g_last_temp_tick = g_last_rx_tick = HAL_GetTick();
    g_seen_overflow = 0U;
    g_tx_sample_valid = g_last_sample_valid = 0U;
    g_tx_acoustic_valid = g_last_acoustic_valid = 0U;
    g_tx_mq2_valid = g_last_mq2_valid = 0U;
    g_tx_ultrasonic_valid = g_last_ultrasonic_valid = 0U;
    memset(&g_tx_acoustic, 0, sizeof(g_tx_acoustic));
    memset(&g_last_acoustic, 0, sizeof(g_last_acoustic));
    SlaveBme280_Init(HAL_GetTick());
    (void)memset(&SlaveRuntimeDiag, 0, sizeof(SlaveRuntimeDiag));
}

uint8_t SlaveRuntime_IsApplicationMode(void)
{
    return g_application_mode;
}

void SlaveRuntime_PushRxByteFromIsr(uint8_t byte)
{
    uint16_t head;
    uint16_t next_head;

    if (g_application_mode == 0U)
    {
        return;
    }
    head = g_rx_ring.head;
    g_last_rx_tick = HAL_GetTick();
    next_head = (uint16_t)((head + 1U) & RX_RING_MASK);
    if (next_head == g_rx_ring.tail)
    {
        g_rx_ring.overflow_count++;
        SlaveRuntimeDiag.rx_overflow_count++;
        return;
    }
    g_rx_ring.data[head] = byte;
    g_rx_ring.head = next_head;
}

void SlaveRuntime_Process(uint32_t now_ms)
{
    uint8_t byte;
    uint16_t count = 0U;

    SlaveWebControl_Process(now_ms);
    SlaveBme280_Process(now_ms);
    now_ms = HAL_GetTick();
    g_current_tick = now_ms;
    if ((g_seen_overflow != g_rx_ring.overflow_count) ||
        ((g_frame_length != 0U) && ((uint32_t)(now_ms - g_last_rx_tick) >= 200U)))
    {
        SlaveRuntime_ResetParser();
        g_seen_overflow = g_rx_ring.overflow_count;
    }
    while ((count < 128U) && (SlaveRuntime_PopRx(&byte) != 0U))
    {
        SlaveRuntime_PushByte(byte);
        count++;
    }
    if ((g_force_pending != 0U) &&
        (SlaveBme280Diag.completed_count != g_force_generation))
    {
        SlaveRuntime_BuildResponse(1U, g_force_flow);
        g_force_pending = 0U;
    }
    if (g_window_ack_pending != 0U)
    {
        now_ms = HAL_GetTick();
        if (((uint32_t)(now_ms - g_last_rx_tick) >= SLAVE_REPLY_DELAY_MS) &&
            (SlaveRuntime_DeadlineReached(now_ms,
                                         g_window_ack_not_before_tick) != 0U))
        {
            if (LORA_SendData(g_window_ack_frame, FRAME_WINDOW_ACK_SIZE) != 0U)
            {
                g_window_ack_pending = 0U;
                g_window_ack_attempt_count = 0U;
                SlaveRuntimeDiag.reply_count++;
            }
            else
            {
                g_window_ack_attempt_count++;
                g_window_ack_not_before_tick = HAL_GetTick() + 10U;
                if (g_window_ack_attempt_count >= TX_RETRY_COUNT)
                {
                    g_window_ack_pending = 0U;
                    g_window_ack_attempt_count = 0U;
                    SlaveRuntimeDiag.tx_failure_count++;
                }
            }
            /* Allow the ACK's radio transmission to finish before telemetry. */
            g_tx_not_before_tick = HAL_GetTick() + SLAVE_REPLY_DELAY_MS;
        }
        return; /* ACK has priority; never send both frames in one iteration. */
    }
    if ((g_tx_pending != 0U) &&
        ((uint32_t)(now_ms - g_last_rx_tick) >= SLAVE_REPLY_DELAY_MS) &&
        (SlaveRuntime_DeadlineReached(now_ms, g_tx_not_before_tick) != 0U))
    {
        uint8_t changed = 0U;
        if ((g_tx_sample_valid != 0U) &&
            ((SlaveBme280Diag.sample_valid == 0U) ||
             ((uint32_t)(now_ms - g_tx_sample_tick) >= 2000U)))
        {
            SlaveRuntime_WriteU16(&g_tx_frame[12], 0x8000U);
            SlaveRuntime_WriteU16(&g_tx_frame[14], 0xFFFFU);
            SlaveRuntime_WriteU32(&g_tx_frame[16], 0xFFFFFFFFUL);
            g_tx_sample_valid = g_last_sample_valid = 0U;
            changed = 1U;
        }
        if ((g_tx_acoustic_valid != 0U) &&
            (SlaveAcoustic_IsSnapshotValid(now_ms, &g_tx_acoustic) == 0U))
        {
            SlaveRuntime_WriteU32(&g_tx_frame[29], SLAVE_SOUND_RMS_INVALID);
            SlaveRuntime_WriteU32(&g_tx_frame[33], SLAVE_SOUND_RMS_INVALID);
            g_tx_acoustic_valid = g_last_acoustic_valid = 0U;
            changed = 1U;
        }
        if (changed != 0U)
        {
            SlaveRuntime_UpdateTxCrc();
            memcpy(g_last_temp_frame, g_tx_frame, g_tx_frame_length);
        }
        if (g_tx_mq2_valid != 0U)
        {
            uint32_t age = now_ms - g_tx_mq2_tick;
            if ((age >= SLAVE_MQ2_MAX_AGE_MS) || (SlaveMq2Diag.valid == 0U) ||
                (SlaveMq2Diag.error_count != g_tx_mq2_errors))
            {
                memset(&g_tx_frame[37], 0xFF, 8U);
                g_tx_mq2_valid = g_last_mq2_valid = 0U;
            }
            else { SlaveRuntime_WriteU16(&g_tx_frame[43], (uint16_t)age); }
            SlaveRuntime_UpdateTxCrc();
            memcpy(g_last_temp_frame, g_tx_frame, g_tx_frame_length);
        }
        if (g_tx_ultrasonic_valid != 0U)
        {
            uint32_t age = now_ms - g_tx_ultrasonic_tick;
            if ((age >= SLAVE_HCSR04_MAX_AGE_MS) || (SlaveHcsr04Diag.valid == 0U) ||
                (SlaveHcsr04Diag.error_count != g_tx_ultrasonic_errors))
            {
                memset(&g_tx_frame[45], 0xFF, 8U);
                g_tx_ultrasonic_valid = g_last_ultrasonic_valid = 0U;
            }
            else { SlaveRuntime_WriteU16(&g_tx_frame[51], (uint16_t)age); }
            SlaveRuntime_UpdateTxCrc();
            memcpy(g_last_temp_frame, g_tx_frame, g_tx_frame_length);
        }
        if (LORA_SendData(g_tx_frame, g_tx_frame_length) != 0U)
        {
            g_tx_pending = 0U;
            g_tx_attempt_count = 0U;
            SlaveRuntimeDiag.reply_count++;
        }
        else
        {
            g_tx_attempt_count++;
            g_tx_not_before_tick = now_ms + 10U;
            if (g_tx_attempt_count >= TX_RETRY_COUNT)
            {
                g_tx_pending = 0U;
                g_tx_attempt_count = 0U;
                SlaveRuntimeDiag.tx_failure_count++;
            }
        }
    }
    else if(!g_force_pending && !g_frame_length && g_rx_ring.head==g_rx_ring.tail &&
        (uint32_t)(now_ms-g_last_rx_tick)>=SLAVE_REPLY_DELAY_MS && SlaveRuntime_DeadlineReached(now_ms,g_tx_not_before_tick))
    {
        uint8_t p[11], frame[24];uint16_t flow,crc;
        if(SlaveWebControl_Prepare(p,&flow,now_ms))
        {
            frame[0]=0xAAU;frame[1]=0x55U;frame[2]=4U;frame[3]=WEB_REQUEST;frame[4]=FRAME_ROLE_SLAVE;frame[5]=g_local_group;
            frame[6]=FRAME_ROLE_MASTER;frame[7]=g_local_group;SlaveRuntime_WriteU16(frame+8,flow);frame[10]=11U;memcpy(frame+11,p,11U);
            crc=SlaveRuntime_Crc16(frame+2,20U);SlaveRuntime_WriteU16(frame+22,crc);
            SlaveWebControl_Sent(LORA_SendData(frame,24U),HAL_GetTick());g_tx_not_before_tick=HAL_GetTick()+SLAVE_REPLY_DELAY_MS;
        }
    }

}
