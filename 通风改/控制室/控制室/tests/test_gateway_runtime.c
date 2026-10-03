#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_runtime.h"

typedef struct
{
    GatewayOutputPort port;
    uint8_t bytes[141];
    uint16_t length;
} SentFrame;

static SentFrame sent[32];
static unsigned send_count;

static uint16_t crc16(const uint8_t *bytes, unsigned length)
{
    uint16_t crc = 0xFFFFU;
    unsigned i, bit;
    for (i = 0U; i < length; i++)
    {
        crc ^= bytes[i];
        for (bit = 0U; bit < 8U; bit++)
        { crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0xA001U : 0U)); }
    }
    return crc;
}

static uint8_t send_frame(GatewayOutputPort port, const uint8_t *bytes,
                          uint16_t length, void *context)
{
    uint16_t crc;
    (void)context;
    assert(send_count < sizeof(sent) / sizeof(sent[0]));
    assert(length == 13U + bytes[10]);
    crc = crc16(&bytes[2], 9U + bytes[10]);
    assert(bytes[length - 2U] == (uint8_t)crc && bytes[length - 1U] == (uint8_t)(crc >> 8U));
    sent[send_count].port = port;
    sent[send_count].length = length;
    memcpy(sent[send_count].bytes, bytes, length);
    send_count++;
    return 1U;
}

static void reset(void)
{
    send_count = 0U;
    memset(sent, 0, sizeof(sent));
    GatewayRuntime_Init(send_frame, NULL);
}

static unsigned make_frame(uint8_t *bytes, uint8_t type, uint8_t source_role,
                            uint16_t flow, uint8_t payload_length, uint8_t flags)
{
    unsigned length = 13U + payload_length;
    uint16_t crc;
    memset(bytes, 0xFF, length);
    bytes[0] = 0xAAU; bytes[1] = 0x55U; bytes[2] = 4U; bytes[3] = type;
    bytes[4] = source_role; bytes[5] = source_role == 1U ? 0U : 1U;
    bytes[6] = source_role == 1U ? 2U : 1U;
    bytes[7] = source_role == 1U ? 1U : 0U;
    bytes[8] = (uint8_t)flow; bytes[9] = (uint8_t)(flow >> 8U);
    bytes[10] = payload_length; bytes[11] = flags;
    crc = crc16(&bytes[2], 9U + payload_length);
    bytes[length - 2U] = (uint8_t)crc; bytes[length - 1U] = (uint8_t)(crc >> 8U);
    return length;
}

static void refresh_crc(uint8_t *frame, unsigned length)
{
    uint16_t crc = crc16(&frame[2], 9U + frame[10]);
    frame[length - 2U] = (uint8_t)crc; frame[length - 1U] = (uint8_t)(crc >> 8U);
}

static void push(uint8_t pc, const uint8_t *bytes, unsigned length)
{
    unsigned i;
    for (i = 0U; i < length; i++)
    {
        if (pc) { GatewayRuntime_PushPcByteFromIsr(bytes[i]); }
        else { GatewayRuntime_PushLoRaByteFromIsr(bytes[i]); }
    }
}

static void request(uint16_t flow, uint32_t now)
{
    uint8_t frame[141];
    unsigned length = make_frame(frame, 1U, 1U, flow, 1U, 0U);
    push(1U, frame, length); GatewayRuntime_Process(now);
    assert(sent[send_count - 1U].port == GATEWAY_OUTPUT_LORA);
    assert(memcmp(sent[send_count - 1U].bytes, frame, length) == 0);
}

static void test_all_telemetry_flags(void)
{
    unsigned flags;
    uint8_t length;
    uint8_t frame[141];
    for (length = 18U; length <= 26U; length += 8U)
    {
        for (flags = 0U; flags < 256U; flags++)
        {
            unsigned full_length;
            unsigned accepted = length == 18U ?
                (flags == 0U || flags == 1U || flags == 3U || flags == 7U) :
                (flags == 0x0BU || flags == 0x0FU);
            reset(); request(100U, 0U);
            full_length = make_frame(frame, 2U, 2U, 100U, length, (uint8_t)flags);
            if (length == 26U)
            {
                /* Zero and >65535 PCM preserved exactly through gateway. */
                memset(&frame[29], 0U, 4U);
                frame[33] = 0xFFU; frame[34] = 0xFFU; frame[35] = 1U; frame[36] = 0U;
                refresh_crc(frame, full_length);
            }
            push(0U, frame, 17U); GatewayRuntime_Process(1U);
            assert(send_count == 1U);
            push(0U, &frame[17], full_length - 17U); GatewayRuntime_Process(2U);
            assert(send_count == 1U + accepted);
            if (accepted)
            {
                assert(sent[1].port == GATEWAY_OUTPUT_PC && sent[1].length == full_length);
                assert(memcmp(sent[1].bytes, frame, full_length) == 0);
                GatewayRuntime_Process(5000U); assert(send_count == 2U);
            }
            else
            {
                GatewayRuntime_Process(4000U);
                assert(send_count == 2U && sent[1].bytes[3] == 0x7EU && sent[1].bytes[11] == 10U);
            }
        }
    }
}

static void test_malformed_address_flow_and_sticky_frames(void)
{
    uint8_t frame[141], good[141];
    unsigned length, good_length;
    reset(); request(200U, 0U);
    length = make_frame(frame, 2U, 2U, 200U, 26U, 0x0FU);
    frame[20] ^= 1U;
    push(0U, frame, length); GatewayRuntime_Process(1U);
    assert(send_count == 1U);
    length = make_frame(frame, 2U, 3U, 200U, 26U, 8U);
    push(0U, frame, length); GatewayRuntime_Process(2U);
    assert(send_count == 1U);
    length = make_frame(frame, 2U, 2U, 200U, 25U, 0x0FU);
    push(0U, frame, length); GatewayRuntime_Process(3U);
    assert(send_count == 1U);
    length = make_frame(frame, 2U, 2U, 200U, 26U, 0x0FU);
    frame[5] = 2U; refresh_crc(frame, length);
    push(0U, frame, length); GatewayRuntime_Process(4U); assert(send_count == 1U);
    frame[5] = 1U; frame[7] = 1U; refresh_crc(frame, length);
    push(0U, frame, length); GatewayRuntime_Process(5U); assert(send_count == 1U);
    length = make_frame(frame, 2U, 2U, 201U, 26U, 0x0FU);
    push(0U, frame, length); GatewayRuntime_Process(6U);
    assert(send_count == 2U); /* May log late/unmatched frames; pending remains. */
    good_length = make_frame(good, 2U, 2U, 200U, 26U, 0x0BU);
    push(0U, frame, length); push(0U, good, good_length); GatewayRuntime_Process(7U);
    assert(send_count == 4U);
    assert(memcmp(sent[3].bytes, good, good_length) == 0);
    GatewayRuntime_Process(5000U); assert(send_count == 4U);

    /* A wrong-flow response must not end the pending request. */
    reset(); request(200U, 0U);
    length = make_frame(frame, 2U, 2U, 201U, 26U, 0x0FU);
    push(0U, frame, length); GatewayRuntime_Process(1U);
    assert(send_count == 2U);
    GatewayRuntime_Process(4000U);
    assert(send_count == 3U && sent[2].bytes[3] == 0x7EU);
    assert(sent[2].bytes[8] == 200U && sent[2].bytes[11] == 10U);
}

static void test_fan_ack_pending_and_timeout_wrap(void)
{
    uint8_t frame[141];
    unsigned length;
    reset();
    GatewayRuntime_Process(0U); GatewayRuntime_Process(1000U);
    assert(send_count == 0U); /* Auto polling remains disabled. */
    request(300U, 1000U);
    length = make_frame(frame, 0x10U, 1U, 301U, 2U, 2U);
    frame[12] = 0U; refresh_crc(frame, length);
    push(1U, frame, length); GatewayRuntime_Process(1001U);
    assert(send_count == 1U); /* Existing half-duplex pending policy retained. */
    length = make_frame(frame, 2U, 2U, 300U, 26U, 0x0FU);
    push(0U, frame, length); GatewayRuntime_Process(1002U);
    assert(send_count == 3U && sent[2].port == GATEWAY_OUTPUT_LORA && sent[2].bytes[3] == 0x10U);
    length = make_frame(frame, 0x20U, 2U, 301U, 2U, 0x01U); /* Wrong ACK command. */
    frame[12] = 0U; refresh_crc(frame, length);
    push(0U, frame, length); GatewayRuntime_Process(1003U);
    assert(send_count == 4U);
    frame[11] = 0x10U; refresh_crc(frame, length);
    push(0U, frame, length); GatewayRuntime_Process(1004U);
    assert(send_count == 5U && memcmp(sent[4].bytes, frame, length) == 0);
    GatewayRuntime_Process(7002U); assert(send_count == 5U);
    reset(); request(400U, 0xFFFFFF00U);
    GatewayRuntime_Process(3743U); assert(send_count == 1U); /* elapsed3999 */
    GatewayRuntime_Process(3744U); assert(send_count == 2U);
    assert(sent[1].bytes[3] == 0x7EU && sent[1].bytes[11] == 10U);
}

static unsigned make_window(uint8_t *frame, uint16_t flow,
                            uint8_t channel, uint8_t action)
{
    unsigned length = make_frame(frame, 0x11U, 1U, flow, 2U, channel);
    frame[12] = action;
    refresh_crc(frame, length);
    return length;
}

static unsigned make_ack(uint8_t *frame, uint16_t flow,
                         uint8_t command, uint8_t status)
{
    unsigned length = make_frame(frame, 0x20U, 2U, flow, 2U, command);
    frame[12] = status;
    refresh_crc(frame, length);
    return length;
}

static void test_window_shape_and_forwarding(void)
{
    uint8_t frame[141], command[141];
    unsigned action, channel, length, command_length, status;

    for (action = 0U; action < 256U; action++)
    {
        reset();
        length = make_window(frame, 500U, 1U, (uint8_t)action);
        push(1U, frame, 7U); GatewayRuntime_Process(0U);
        assert(send_count == 0U);
        push(1U, &frame[7], length - 7U); GatewayRuntime_Process(1U);
        assert(send_count == (action <= 1U ? 1U : 0U));
        if (action <= 1U)
        {
            assert(sent[0].port == GATEWAY_OUTPUT_LORA && sent[0].length == 15U);
            assert(memcmp(sent[0].bytes, frame, length) == 0);
        }
    }
    for (channel = 0U; channel < 256U; channel++)
    {
        reset(); length = make_window(frame, 500U, (uint8_t)channel, 1U);
        push(1U, frame, length); GatewayRuntime_Process(0U);
        assert(send_count == (channel == 1U ? 1U : 0U));
    }
    for (length = 0U; length <= 3U; length++)
    {
        unsigned full_length;
        if (length == 2U) { continue; }
        reset(); full_length = make_frame(frame, 0x11U, 1U, 500U, (uint8_t)length, 1U);
        push(1U, frame, full_length); GatewayRuntime_Process(0U);
        assert(send_count == 0U);
    }
    reset(); length = make_window(frame, 500U, 1U, 1U);
    frame[12] ^= 1U; /* Valid action, invalid CRC. */
    push(1U, frame, length); GatewayRuntime_Process(0U); assert(send_count == 0U);
    refresh_crc(frame, length); frame[4] = 2U; refresh_crc(frame, length);
    push(1U, frame, length); GatewayRuntime_Process(1U); assert(send_count == 0U);
    frame[4] = 1U; frame[5] = 1U; refresh_crc(frame, length);
    push(1U, frame, length); GatewayRuntime_Process(2U); assert(send_count == 0U);
    frame[5] = 0U; frame[6] = 3U; refresh_crc(frame, length);
    push(1U, frame, length); GatewayRuntime_Process(3U); assert(send_count == 0U);
    frame[6] = 2U; frame[7] = 2U; refresh_crc(frame, length);
    push(1U, frame, length); GatewayRuntime_Process(4U);
    assert(send_count == 1U && sent[0].port == GATEWAY_OUTPUT_PC);
    assert(sent[0].bytes[3] == 0x7EU && sent[0].bytes[11] == 2U);

    for (action = 0U; action <= 1U; action++)
    {
        for (status = 0U; status <= 3U; status++)
        {
            reset(); command_length = make_window(command, 501U, 1U, (uint8_t)action);
            push(1U, command, command_length); GatewayRuntime_Process(0U);
            assert(send_count == 1U && memcmp(sent[0].bytes, command, command_length) == 0);
            length = make_ack(frame, 501U, 0x11U, (uint8_t)status);
            push(0U, frame, length); GatewayRuntime_Process(50U);
            assert(send_count == 2U && sent[1].port == GATEWAY_OUTPUT_PC);
            assert(memcmp(sent[1].bytes, frame, length) == 0);
            GatewayRuntime_Process(6000U); assert(send_count == 2U);
        }
    }
}

static void test_window_ack_matching_queue_and_timeout(void)
{
    uint8_t frame[141], queued[141];
    unsigned length, queued_length;
    reset(); request(600U, 0U);
    queued_length = make_window(queued, 601U, 1U, 1U);
    push(1U, queued, queued_length); GatewayRuntime_Process(1U);
    assert(send_count == 1U); /* Do not interrupt an existing telemetry request. */
    length = make_frame(frame, 2U, 2U, 600U, 26U, 0x0FU);
    push(0U, frame, length); GatewayRuntime_Process(2U);
    assert(send_count == 3U && sent[2].port == GATEWAY_OUTPUT_LORA);
    assert(memcmp(sent[2].bytes, queued, queued_length) == 0);
    queued_length = make_window(queued, 602U, 1U, 0U);
    push(1U, queued, queued_length); GatewayRuntime_Process(3U);
    assert(send_count == 3U);
    length = make_ack(frame, 601U, 0x11U, 4U);
    push(0U, frame, length); GatewayRuntime_Process(4U); assert(send_count == 3U);
    length = make_ack(frame, 601U, 0x11U, 0U);
    frame[4] = 3U; refresh_crc(frame, length);
    push(0U, frame, length); GatewayRuntime_Process(5U); assert(send_count == 3U);
    length = make_ack(frame, 603U, 0x11U, 0U);
    push(0U, frame, length); GatewayRuntime_Process(6U); assert(send_count == 4U);
    length = make_ack(frame, 601U, 0x10U, 0U);
    push(0U, frame, length); GatewayRuntime_Process(7U); assert(send_count == 5U);
    length = make_frame(frame, 2U, 2U, 601U, 26U, 0x0BU);
    push(0U, frame, length); GatewayRuntime_Process(8U); assert(send_count == 6U);
    length = make_ack(frame, 601U, 0x11U, 0U);
    push(0U, frame, length); GatewayRuntime_Process(9U);
    assert(send_count == 8U && sent[7].port == GATEWAY_OUTPUT_LORA);
    assert(memcmp(sent[7].bytes, queued, queued_length) == 0);
    length = make_ack(frame, 602U, 0x11U, 1U);
    push(0U, frame, length); GatewayRuntime_Process(10U);
    assert(send_count == 9U && memcmp(sent[8].bytes, frame, length) == 0);
    GatewayRuntime_Process(6010U); assert(send_count == 9U);

    reset(); length = make_window(frame, 700U, 1U, 0U);
    push(1U, frame, length); GatewayRuntime_Process(0xFFFFFF00U);
    assert(send_count == 1U);
    GatewayRuntime_Process(5743U); assert(send_count == 1U); /* 5999 ms */
    GatewayRuntime_Process(5744U); assert(send_count == 2U); /* 6000 ms */
    assert(sent[1].port == GATEWAY_OUTPUT_PC && sent[1].bytes[3] == 0x7EU);
    assert(sent[1].bytes[8] == (uint8_t)700U && sent[1].bytes[11] == 10U);
}

int main(void)
{
    test_all_telemetry_flags();
    test_malformed_address_flow_and_sticky_frames();
    test_fan_ack_pending_and_timeout_wrap();
    test_window_shape_and_forwarding();
    test_window_ack_matching_queue_and_timeout();
    puts("5 real gateway groups passed (telemetry/fan/window, shape/CRC/stream/ACK/queue/wrap)");
    return 0;
}
