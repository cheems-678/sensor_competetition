#include "slave_acoustic.h"
#include "sph0645.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

volatile Sph0645Diagnostics Sph0645Diag;

static uint32_t checks;
static uint32_t failures;
static uint32_t start_calls;
static uint32_t stop_calls;
static uint32_t check_calls;
static uint32_t read_calls;
static uint32_t latest_start_tick;
static uint32_t check_error;
static uint8_t start_success;
static uint8_t block_ready;
static uint8_t copy_race;
static uint16_t fake_words[SPH0645_BLOCK_WORDS];
static uint32_t fake_sequence;
static uint32_t fake_block_tick;
static int32_t amplitude_left = 1000L, amplitude_right = 2000L;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        checks++;                                                            \
        if (!(condition))                                                    \
        {                                                                    \
            failures++;                                                      \
            if (failures <= 20U)                                             \
            {                                                                \
                printf("FAIL line %u: %s\n", (unsigned int)__LINE__,           \
                       #condition);                                          \
            }                                                                \
        }                                                                    \
    } while (0)

uint8_t Sph0645_Start(uint32_t now_ms)
{
    start_calls++;
    Sph0645Diag.start_count++;
    latest_start_tick = now_ms;
    block_ready = 0U;
    if (start_success == 0U)
    {
        Sph0645Diag.running = 0U;
        Sph0645Diag.active_error = SPH0645_ERROR_CLOCK_CONFIG;
        Sph0645Diag.last_error = SPH0645_ERROR_CLOCK_CONFIG;
        Sph0645Diag.failure_count++;
        return 0U;
    }
    Sph0645Diag.running = 1U;
    Sph0645Diag.active_error = SPH0645_ERROR_NONE;
    check_error = SPH0645_ERROR_NONE;
    return 1U;
}

void Sph0645_Stop(void)
{
    stop_calls++;
    Sph0645Diag.running = 0U;
    block_ready = 0U;
}

uint32_t Sph0645_Check(uint32_t now_ms)
{
    (void)now_ms;
    check_calls++;
    Sph0645Diag.active_error = check_error;
    return check_error;
}

uint8_t Sph0645_ReadBlock(uint16_t words[SPH0645_BLOCK_WORDS],
                         uint32_t *sequence, uint32_t *block_tick)
{
    read_calls++;
    if (block_ready == 0U)
    {
        return 0U;
    }
    memcpy(words, fake_words, sizeof(fake_words));
    *sequence = fake_sequence;
    *block_tick = fake_block_tick;
    block_ready = 0U;
    if (copy_race != 0U)
    {
        copy_race = 0U;
        Sph0645Diag.copy_races++;
        Sph0645Diag.dropped_blocks++;
        return 0U;
    }
    Sph0645Diag.copied_blocks++;
    return 1U;
}

void Sph0645_DmaIRQ(void)
{
    /* Interrupt timing belongs to the separately reviewed hardware driver. */
}

static void reset_fakes(void)
{
    memset((void *)&Sph0645Diag, 0, sizeof(Sph0645Diag));
    start_calls = 0U;
    stop_calls = 0U;
    check_calls = 0U;
    read_calls = 0U;
    latest_start_tick = 0U;
    check_error = SPH0645_ERROR_NONE;
    start_success = 1U;
    block_ready = 0U;
    copy_race = 0U;
    fake_sequence = 0U;
    fake_block_tick = 0U;
    amplitude_left = 1000L;
    amplitude_right = 2000L;
}

static void encode_sample(int32_t sample, uint8_t tail, uint16_t words[2])
{
    uint32_t raw = (((uint32_t)sample & 0x3FFFFU) << 14) | tail;
    words[0] = (uint16_t)(raw >> 16);
    words[1] = (uint16_t)raw;
}

static void queue_block(uint32_t sequence, uint32_t block_tick,
                        uint16_t left_padding, uint16_t right_padding)
{
    uint32_t frame;
    CHECK(block_ready == 0U);
    for (frame = 0U; frame < SPH0645_BLOCK_FRAMES; frame++)
    {
        int32_t sign = ((frame & 1U) == 0U) ? 1L : -1L;
        encode_sample(sign * amplitude_left, (uint8_t)frame, &fake_words[frame * 4U]);
        encode_sample(-sign * amplitude_right, (uint8_t)(frame * 73U + 29U),
                      &fake_words[frame * 4U + 2U]);
        fake_words[frame * 4U + 1U] |= left_padding;
        fake_words[frame * 4U + 3U] |= right_padding;
    }
    fake_sequence = sequence;
    fake_block_tick = block_tick;
    block_ready = 1U;
    Sph0645Diag.completed_blocks = sequence;
    Sph0645Diag.last_block_tick = block_tick;
}

static void feed_block(uint32_t sequence, uint32_t block_tick,
                       uint16_t left_padding, uint16_t right_padding)
{
    queue_block(sequence, block_tick, left_padding, right_padding);
    SlaveAcoustic_Process(block_tick);
    CHECK(block_ready == 0U);
}

static void check_stereo_window(uint32_t count, uint32_t tick)
{
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_CAPTURING);
    CHECK(SlaveAcousticDiag.window_count == count);
    CHECK(SlaveAcousticDiag.window_tick == tick);
    CHECK(SlaveAcousticDiag.rms_left == 1000U);
    CHECK(SlaveAcousticDiag.rms_right == 2000U);
    CHECK(SlaveAcousticDiag.peak_left == 1000U);
    CHECK(SlaveAcousticDiag.peak_right == 2000U);
    CHECK(SlaveAcousticDiag.mean_left == 0L);
    CHECK(SlaveAcousticDiag.mean_right == 0L);
    CHECK(SlaveAcousticDiag.last_left == -1000L);
    CHECK(SlaveAcousticDiag.last_right == 2000L);
    CHECK(SlaveAcousticDiag.nonzero_left == 2048U);
    CHECK(SlaveAcousticDiag.nonzero_right == 2048U);
}

static void feed_warmup_blocks(uint32_t started_at, uint32_t start_sequence)
{
    uint32_t block;
    for (block = 1U; block <= 7U; block++)
    {
        feed_block(start_sequence + block, started_at + block * 16U, 0U, 0U);
    }
}

static void begin_with_clean_window(uint32_t started_at)
{
    uint32_t block;
    reset_fakes();
    SlaveAcoustic_Init(started_at);
    feed_warmup_blocks(started_at, 0U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 8U, started_at + 117U + block * 16U, 0U, 0U);
    }
    check_stereo_window(1U, started_at + 165U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
}

static void test_whole_block_warmup_and_raw_diagnostics(void)
{
    uint32_t index;
    uint32_t block;
    reset_fakes();
    SlaveAcoustic_Init(1000U);
    CHECK(start_calls == 1U && latest_start_tick == 1000U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    SlaveAcoustic_Process(1001U);
    CHECK(SlaveAcousticDiag.processed_blocks == 0U);
    CHECK(check_calls == 1U && read_calls == 1U);

    for (block = 1U; block <= 6U; block++)
    {
        feed_block(block, 1000U + block * 16U, 0x3F00U, 0x3F00U);
    }
    /* An ISR timestamp may be late: elapsed time alone cannot admit block #7. */
    feed_block(7U, 1117U, 0x3F00U, 0x3F00U);
    CHECK(SlaveAcousticDiag.warmup_blocks == 7U);
    CHECK(SlaveAcousticDiag.processed_frames == 0U);
    CHECK(SlaveAcousticDiag.window_count == 0U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    CHECK(SlaveAcousticDiag.discontinuities == 0U);

    feed_block(8U, 1117U, 0U, 0U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_CAPTURING);
    CHECK(SlaveAcousticDiag.processed_frames == SPH0645_BLOCK_FRAMES);
    for (index = 0U; index < 16U; index++)
    {
        CHECK(SlaveAcousticDiag.raw_words[index] == fake_words[index]);
    }
    feed_block(9U, 1133U, 0U, 0U);
    feed_block(10U, 1149U, 0U, 0U);
    CHECK(SlaveAcousticDiag.window_count == 0U);
    feed_block(11U, 1165U, 0U, 0U);
    check_stereo_window(1U, 1165U);
    CHECK(SlaveAcousticDiag.processed_blocks == 11U);
    CHECK(SlaveAcousticDiag.processed_frames == 2048U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcousticDiag.total_padding_errors_left == 0U);
    CHECK(SlaveAcousticDiag.total_padding_errors_right == 0U);
}

static void test_warmup_sequence_does_not_bypass_elapsed_time(void)
{
    uint32_t block;
    reset_fakes();
    SlaveAcoustic_Init(1000U);
    for (block = 1U; block <= 7U; block++)
    {
        feed_block(block, 1000U + block * 14U, 0U, 0U);
    }
    /* Even block #8 is rejected when its timestamp is only 116 ms after start. */
    feed_block(8U, 1116U, 0U, 0U);
    CHECK(SlaveAcousticDiag.warmup_blocks == 8U);
    CHECK(SlaveAcousticDiag.processed_frames == 0U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    feed_block(9U, 1117U, 0U, 0U);
    CHECK(SlaveAcousticDiag.processed_frames == SPH0645_BLOCK_FRAMES);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_CAPTURING);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(SlaveAcousticDiag.discontinuities == 0U);
}

static void test_sequence_gap_discards_partial_window(void)
{
    begin_with_clean_window(0U);
    feed_block(12U, 181U, 0U, 0U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    feed_block(14U, 197U, 0U, 0U);
    CHECK(SlaveAcousticDiag.discontinuities == 1U);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(SlaveAcousticDiag.window_count == 1U);
    feed_block(15U, 213U, 0U, 0U);
    feed_block(16U, 229U, 0U, 0U);
    CHECK(SlaveAcousticDiag.window_count == 1U);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    feed_block(17U, 245U, 0U, 0U);
    check_stereo_window(2U, 245U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcousticDiag.discontinuities == 1U);
    CHECK(SlaveAcousticDiag.processed_frames == 9U * SPH0645_BLOCK_FRAMES);
}

static void test_padding_invalidates_window_and_counters_are_cumulative(void)
{
    uint32_t block;
    reset_fakes();
    SlaveAcoustic_Init(0U);
    feed_warmup_blocks(0U, 0U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 8U, 117U + block * 16U,
                   (block == 1U) ? 0x0100U : 0U,
                   (block == 3U) ? 0x2000U : 0U);
    }
    check_stereo_window(1U, 165U);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(SlaveAcousticDiag.padding_errors_left == SPH0645_BLOCK_FRAMES);
    CHECK(SlaveAcousticDiag.padding_errors_right == SPH0645_BLOCK_FRAMES);
    CHECK(SlaveAcousticDiag.total_padding_errors_left == SPH0645_BLOCK_FRAMES);
    CHECK(SlaveAcousticDiag.total_padding_errors_right == SPH0645_BLOCK_FRAMES);

    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 12U, 181U + block * 16U, 0U, 0U);
    }
    check_stereo_window(2U, 229U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcousticDiag.padding_errors_left == 0U);
    CHECK(SlaveAcousticDiag.padding_errors_right == 0U);
    CHECK(SlaveAcousticDiag.total_padding_errors_left == SPH0645_BLOCK_FRAMES);
    CHECK(SlaveAcousticDiag.total_padding_errors_right == SPH0645_BLOCK_FRAMES);
}

static void test_stale_boundary_and_fresh_window_recovery(void)
{
    uint32_t block;
    begin_with_clean_window(0U);
    SlaveAcoustic_Process(164U); /* A captured timestamp may lead now_ms by 1 ms. */
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    SlaveAcoustic_Process(464U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcousticDiag.window_count == 1U);
    SlaveAcoustic_Process(465U);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_CAPTURING);
    CHECK(SlaveAcousticDiag.rms_left == 1000U); /* History stays visible. */

    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 12U, 466U + block * 16U, 0U, 0U);
        CHECK(SlaveAcousticDiag.window_valid == (uint32_t)(block == 3U));
    }
    check_stereo_window(2U, 514U);
    CHECK(start_calls == 1U && stop_calls == 0U);
}

static void test_fault_stops_and_retries_once_per_second(void)
{
    uint32_t checks_before;
    uint32_t reads_before;
    uint32_t block;
    begin_with_clean_window(0U);
    feed_block(12U, 181U, 0U, 0U); /* Partial window must not survive a fault. */
    check_error = SPH0645_ERROR_SPI_OVERRUN;
    reads_before = read_calls;
    SlaveAcoustic_Process(182U);
    CHECK(stop_calls == 1U && Sph0645Diag.running == 0U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_RETRY);
    CHECK(SlaveAcousticDiag.last_error == SPH0645_ERROR_SPI_OVERRUN);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(read_calls == reads_before);
    checks_before = check_calls;

    SlaveAcoustic_Process(183U);
    SlaveAcoustic_Process(1181U);
    CHECK(start_calls == 1U && SlaveAcousticDiag.restart_count == 0U);
    CHECK(check_calls == checks_before && read_calls == reads_before);
    SlaveAcoustic_Process(1182U);
    CHECK(start_calls == 2U && SlaveAcousticDiag.restart_count == 1U);
    CHECK(latest_start_tick == 1182U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    CHECK(check_calls == checks_before && read_calls == reads_before);

    feed_warmup_blocks(1182U, 12U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 20U, 1299U + block * 16U, 0U, 0U);
        CHECK(SlaveAcousticDiag.window_count == ((block == 3U) ? 2U : 1U));
    }
    check_stereo_window(2U, 1347U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcousticDiag.discontinuities == 0U);
    CHECK(stop_calls == 1U && start_calls == 2U);
}

static void test_initial_start_failure_and_bounded_repeat_failure(void)
{
    uint32_t block;
    reset_fakes();
    start_success = 0U;
    SlaveAcoustic_Init(500U);
    CHECK(start_calls == 1U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_RETRY);
    CHECK(SlaveAcousticDiag.last_error == SPH0645_ERROR_CLOCK_CONFIG);
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    SlaveAcoustic_Process(501U);
    SlaveAcoustic_Process(1499U);
    CHECK(start_calls == 1U && SlaveAcousticDiag.restart_count == 0U);
    SlaveAcoustic_Process(1500U);
    CHECK(start_calls == 2U && SlaveAcousticDiag.restart_count == 1U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_RETRY);
    SlaveAcoustic_Process(1501U);
    SlaveAcoustic_Process(2499U);
    CHECK(start_calls == 2U);
    start_success = 1U;
    SlaveAcoustic_Process(2500U);
    CHECK(start_calls == 3U && SlaveAcousticDiag.restart_count == 2U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    CHECK(check_calls == 0U && read_calls == 0U);
    feed_warmup_blocks(2500U, 0U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 8U, 2617U + block * 16U, 0U, 0U);
    }
    check_stereo_window(1U, 2665U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
}

static void test_tick_and_sequence_wrap(void)
{
    uint32_t started_at;
    uint32_t block;

    reset_fakes();
    started_at = UINT32_MAX - 50U;
    SlaveAcoustic_Init(started_at);
    feed_warmup_blocks(started_at, 0U);
    CHECK(SlaveAcousticDiag.warmup_blocks == 7U);
    CHECK(SlaveAcousticDiag.processed_frames == 0U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block(block + 8U, started_at + 117U + block * 16U, 0U, 0U);
    }
    check_stereo_window(1U, started_at + 165U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);

    /* Window timestamp precedes rollover; the stale check follows it. */
    started_at = UINT32_MAX - 400U;
    begin_with_clean_window(started_at);
    SlaveAcoustic_Process(started_at + 165U + 299U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    SlaveAcoustic_Process(started_at + 165U + 300U);
    CHECK(SlaveAcousticDiag.window_valid == 0U);

    reset_fakes();
    started_at = UINT32_MAX - 200U;
    start_success = 0U;
    SlaveAcoustic_Init(started_at);
    SlaveAcoustic_Process(started_at + 999U);
    CHECK(start_calls == 1U);
    start_success = 1U;
    SlaveAcoustic_Process(started_at + 1000U);
    CHECK(start_calls == 2U && SlaveAcousticDiag.restart_count == 1U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);

    reset_fakes();
    Sph0645Diag.completed_blocks = UINT32_MAX - 2U;
    SlaveAcoustic_Init(0U);
    feed_warmup_blocks(0U, UINT32_MAX - 2U);
    for (block = 0U; block < 4U; block++)
    {
        feed_block((UINT32_MAX - 2U) + 8U + block,
                   117U + block * 16U, 0U, 0U);
    }
    check_stereo_window(1U, 165U);
    CHECK(SlaveAcousticDiag.discontinuities == 0U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
}

static void feed_window(uint32_t *sequence, uint32_t *tick,
                        int32_t left, int32_t right)
{
    uint32_t block;
    amplitude_left = left;
    amplitude_right = right;
    for (block = 0U; block < 4U; block++)
    {
        (*sequence)++;
        *tick += 16U;
        feed_block(*sequence, *tick, 0U, 0U);
    }
}

static void test_recent_max_history_zero_and_independent_channels(void)
{
    SlaveAcousticSnapshot snapshot, again;
    uint32_t sequence = 11U, tick = 165U, index;
    begin_with_clean_window(0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, NULL) == 0U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, NULL) == 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(snapshot.rms_left == 1000U && snapshot.rms_right == 2000U);
    feed_window(&sequence, &tick, 131071L, 12L);
    feed_window(&sequence, &tick, 13L, 90000L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(snapshot.rms_left == 131071U && snapshot.rms_right == 90000U);
    CHECK(snapshot.window_tick == tick);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &again) == 1U);
    CHECK(memcmp(&snapshot, &again, sizeof(snapshot)) == 0);
    CHECK(SlaveAcousticDiag.history_count == 3U);
    /* Keep producing silence: the one-window transient expires after 1000 ms. */
    for (index = 0U; index < 15U; index++)
    {
        feed_window(&sequence, &tick, 0L, 0L);
    }
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(snapshot.rms_left == 13U && snapshot.rms_right == 90000U);
    /* Right's transient completed one window later, so drops one window later. */
    feed_window(&sequence, &tick, 0L, 0L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(snapshot.rms_left == 0U && snapshot.rms_right == 0U);
    for (index = 0U; index < 40U; index++)
    {
        feed_window(&sequence, &tick, 17L, 23L);
    }
    CHECK(SlaveAcousticDiag.history_count == 32U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(snapshot.rms_left == 17U && snapshot.rms_right == 23U);
}

static void test_snapshot_stale_and_epoch_cannot_revive(void)
{
    SlaveAcousticSnapshot snapshot, fresh;
    uint32_t sequence = 11U, tick = 165U, epoch;
    begin_with_clean_window(0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick - 1U, &snapshot) == 1U);
    feed_window(&sequence, &tick, 7L, 11L);
    CHECK(SlaveAcoustic_IsSnapshotValid(464U, &snapshot) == 1U);
    /* Current capture remains fresh; original snapshot has crossed 300 ms. */
    CHECK(SlaveAcoustic_IsSnapshotValid(465U, &snapshot) == 0U);
    CHECK(SlaveAcousticDiag.window_valid == 1U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &snapshot) == 1U);
    epoch = snapshot.validity_epoch;
    feed_block(sequence + 2U, tick + 16U, 0U, 0U);
    sequence += 2U;
    tick += 16U;
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcousticDiag.validity_epoch != epoch);
    /* The gap block starts a new accumulator. Three more make a clean window. */
    feed_block(++sequence, tick += 16U, 0U, 0U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &fresh) == 1U);
    CHECK(fresh.rms_left == 7U && fresh.rms_right == 11U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &snapshot) == 0U);
    snapshot = fresh;
    epoch = snapshot.validity_epoch;
    /* One bad channel invalidates BOTH, dropping earlier maxima. */
    feed_block(++sequence, tick += 16U, 0U, 0x0100U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &fresh) == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcousticDiag.validity_epoch != epoch);
    feed_window(&sequence, &tick, 0L, 0L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &fresh) == 1U);
    CHECK(fresh.rms_left == 0U && fresh.rms_right == 0U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &snapshot) == 0U);
    snapshot = fresh;
    epoch = snapshot.validity_epoch;
    CHECK(SlaveAcoustic_GetRecentMax(tick + 299U, &fresh) == 1U);
    CHECK(SlaveAcoustic_GetRecentMax(tick + 300U, &fresh) == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcousticDiag.validity_epoch != epoch);
    epoch = SlaveAcousticDiag.validity_epoch;
    SlaveAcoustic_Process(tick + 301U);
    CHECK(SlaveAcousticDiag.validity_epoch == epoch); /* No per-loop epoch churn. */
    tick += 301U;
    feed_window(&sequence, &tick, 1L, 2L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &fresh) == 1U);
    CHECK(fresh.rms_left == 1U && fresh.rms_right == 2U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &snapshot) == 0U);
}

static void test_recent_max_wrap_and_fault_restart(void)
{
    SlaveAcousticSnapshot old, fresh;
    uint32_t started_at = UINT32_MAX - 200U;
    uint32_t sequence = 11U, tick = started_at + 165U, index;
    begin_with_clean_window(started_at);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &old) == 1U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick + 299U, &old) == 1U);
    for (index = 0U; index < 16U; index++)
    {
        feed_window(&sequence, &tick, 10L, 20L);
    }
    CHECK(SlaveAcoustic_GetRecentMax(tick, &fresh) == 1U);
    CHECK(fresh.rms_left == 10U && fresh.rms_right == 20U);
    old = fresh;
    check_error = SPH0645_ERROR_DMA;
    SlaveAcoustic_Process(tick + 1U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick + 1U, &fresh) == 0U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick + 1U, &old) == 0U);
    SlaveAcoustic_Process(tick + 1001U);
    CHECK(SlaveAcousticDiag.state == SLAVE_ACOUSTIC_WARMUP);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick + 1001U, &fresh) == 0U);
}

static void test_isr_fault_invalidates_before_process(void)
{
    SlaveAcousticSnapshot old, current;
    begin_with_clean_window(0U);
    CHECK(SlaveAcoustic_GetRecentMax(165U, &old) == 1U);
    Sph0645Diag.running = 0U;
    Sph0645Diag.active_error = SPH0645_ERROR_DMA;
    /* No Process: the TX validity check must observe ISR-published failure. */
    CHECK(SlaveAcoustic_IsSnapshotValid(166U, &old) == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcousticDiag.validity_epoch != old.validity_epoch);
    CHECK(SlaveAcoustic_GetRecentMax(166U, &current) == 0U);
    CHECK(stop_calls == 0U && start_calls == 1U); /* Getter cannot touch clocks. */

    begin_with_clean_window(0U);
    CHECK(SlaveAcoustic_GetRecentMax(165U, &old) == 1U);
    Sph0645Diag.active_error = SPH0645_ERROR_IRQ_LATE;
    CHECK(SlaveAcoustic_GetRecentMax(166U, &current) == 0U);
    CHECK(SlaveAcoustic_IsSnapshotValid(166U, &old) == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
}

static void test_exact_one_second_boundary_with_fresh_windows(void)
{
    static const uint32_t starts[] = {0U, UINT32_MAX - 400U};
    uint32_t scenario, index;
    SlaveAcousticSnapshot snapshot;
    for (scenario = 0U; scenario < 2U; scenario++)
    {
        uint32_t sequence = 11U, tick = starts[scenario] + 165U;
        uint32_t peak_tick = tick;
        begin_with_clean_window(starts[scenario]);
        for (index = 0U; index < 15U; index++)
        {
            feed_window(&sequence, &tick, 0L, 0L);
        }
        CHECK(SlaveAcoustic_GetRecentMax(peak_tick + 999U, &snapshot) == 1U);
        CHECK(snapshot.rms_left == 1000U && snapshot.rms_right == 2000U);
        CHECK(SlaveAcoustic_GetRecentMax(peak_tick + 1000U, &snapshot) == 1U);
        CHECK(snapshot.rms_left == 0U && snapshot.rms_right == 0U);
        CHECK(SlaveAcoustic_IsSnapshotValid(peak_tick + 1000U, &snapshot) == 1U);
    }
}

static void test_copy_drop_clears_peak_and_partial_accumulator(void)
{
    SlaveAcousticSnapshot old, current;
    uint32_t sequence = 11U, tick = 165U;
    begin_with_clean_window(0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &old) == 1U);
    feed_block(++sequence, tick += 16U, 0U, 0U); /* Partial old loud window. */
    Sph0645Diag.dropped_blocks++;
    /* ReadBlock can fail without a fatal active_error. Getter must see the drop. */
    CHECK(Sph0645Diag.active_error == SPH0645_ERROR_NONE);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &old) == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &current) == 0U);
    feed_window(&sequence, &tick, 0L, 0L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &current) == 1U);
    CHECK(current.rms_left == 0U && current.rms_right == 0U);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &old) == 0U);

    begin_with_clean_window(0U);
    sequence = 11U; tick = 165U;
    CHECK(SlaveAcoustic_GetRecentMax(tick, &old) == 1U);
    feed_block(++sequence, tick += 16U, 0U, 0U);
    queue_block(++sequence, tick += 16U, 0U, 0U);
    copy_race = 1U;
    SlaveAcoustic_Process(tick); /* ReadBlock publishes drop, then returns 0. */
    CHECK(SlaveAcousticDiag.window_valid == 0U);
    CHECK(SlaveAcousticDiag.history_count == 0U);
    CHECK(Sph0645Diag.copy_races == 1U);
    CHECK(SlaveAcousticDiag.validity_epoch != old.validity_epoch);
    CHECK(SlaveAcoustic_IsSnapshotValid(tick, &old) == 0U);
    feed_window(&sequence, &tick, 0L, 0L);
    CHECK(SlaveAcoustic_GetRecentMax(tick, &current) == 1U);
    CHECK(current.rms_left == 0U && current.rms_right == 0U);
}

int main(void)
{
    CHECK(SPH0645_BLOCK_FRAMES == 512U);
    test_whole_block_warmup_and_raw_diagnostics();
    test_warmup_sequence_does_not_bypass_elapsed_time();
    test_sequence_gap_discards_partial_window();
    test_padding_invalidates_window_and_counters_are_cumulative();
    test_stale_boundary_and_fresh_window_recovery();
    test_fault_stops_and_retries_once_per_second();
    test_initial_start_failure_and_bounded_repeat_failure();
    test_tick_and_sequence_wrap();
    test_recent_max_history_zero_and_independent_channels();
    test_snapshot_stale_and_epoch_cannot_revive();
    test_recent_max_wrap_and_fault_restart();
    test_isr_fault_invalidates_before_process();
    test_exact_one_second_boundary_with_fresh_windows();
    test_copy_drop_clears_peak_and_partial_accumulator();
    printf("Slave acoustic service: %lu checks, %lu failures\n",
           (unsigned long)checks, (unsigned long)failures);
    return (failures == 0U) ? 0 : 1;
}
