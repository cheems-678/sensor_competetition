#include "slave_acoustic.h"

#include "acoustic_processing.h"
#include "sph0645.h"

#include <string.h>

#define SLAVE_ACOUSTIC_WARMUP_MS 100U
#define SLAVE_ACOUSTIC_RETRY_MS 1000U
#define SLAVE_ACOUSTIC_STALE_MS 300U

volatile SlaveAcousticDiagnostics SlaveAcousticDiag;
static uint16_t block_words[SPH0645_BLOCK_WORDS];
static AcousticAccumulator accumulator;
static uint32_t started_at, retry_at, previous_sequence, start_sequence;
static uint32_t validity_epoch, seen_dropped_blocks;

static void SlaveAcoustic_Invalidate(void)
{
    validity_epoch++;
    SlaveAcousticDiag.window_valid = 0U;
    SlaveAcousticDiag.validity_epoch = validity_epoch;
}

static void SlaveAcoustic_Expire(uint32_t now_ms)
{
    /* ISR faults can arrive during BME/LoRa work before the next Process call.
       Getters observe these flags without restarting or touching clocks. */
    if (seen_dropped_blocks != Sph0645Diag.dropped_blocks)
    {
        seen_dropped_blocks = Sph0645Diag.dropped_blocks;
        SlaveAcoustic_Invalidate();
        Acoustic_Reset(&accumulator);
    }
    if ((SlaveAcousticDiag.window_valid != 0U) &&
        ((Sph0645Diag.running == 0U) ||
         (Sph0645Diag.active_error != SPH0645_ERROR_NONE) ||
         ((int32_t)(now_ms - SlaveAcousticDiag.window_tick) >=
          (int32_t)SLAVE_ACOUSTIC_STALE_MS)))
    {
        SlaveAcoustic_Invalidate();
        Acoustic_Reset(&accumulator);
    }
}

static void SlaveAcoustic_Start(uint32_t now_ms)
{
    Acoustic_Reset(&accumulator);
    SlaveAcoustic_Invalidate();
    seen_dropped_blocks = Sph0645Diag.dropped_blocks;
    previous_sequence = Sph0645Diag.completed_blocks;
    start_sequence = previous_sequence;
    started_at = now_ms;
    if (Sph0645_Start(now_ms) != 0U)
    {
        SlaveAcousticDiag.state = SLAVE_ACOUSTIC_WARMUP;
    }
    else
    {
        SlaveAcousticDiag.last_error = Sph0645Diag.active_error;
        SlaveAcousticDiag.state = SLAVE_ACOUSTIC_RETRY;
        retry_at = now_ms;
    }
}

void SlaveAcoustic_Init(uint32_t now_ms)
{
    memset((void *)&SlaveAcousticDiag, 0, sizeof(SlaveAcousticDiag));
    SlaveAcoustic_Start(now_ms);
}

void SlaveAcoustic_Process(uint32_t now_ms)
{
    uint32_t sequence, block_tick, index, error;
    uint8_t block_available;
    AcousticResult result;

    SlaveAcoustic_Expire(now_ms);
    if (SlaveAcousticDiag.state == SLAVE_ACOUSTIC_RETRY)
    {
        if ((uint32_t)(now_ms - retry_at) >= SLAVE_ACOUSTIC_RETRY_MS)
        {
            SlaveAcousticDiag.restart_count++;
            SlaveAcoustic_Start(now_ms);
        }
        return;
    }
    error = Sph0645_Check(now_ms);
    if (error != SPH0645_ERROR_NONE)
    {
        Sph0645_Stop();
        Acoustic_Reset(&accumulator);
        SlaveAcoustic_Invalidate();
        SlaveAcousticDiag.last_error = error;
        SlaveAcousticDiag.state = SLAVE_ACOUSTIC_RETRY;
        retry_at = now_ms;
        return;
    }
    block_available = Sph0645_ReadBlock(block_words, &sequence, &block_tick);
    /* ReadBlock can detect a copy race and publish a drop while returning 0.
       Invalidate the latest window before a later sequence gap is observed. */
    SlaveAcoustic_Expire(now_ms);
    if (block_available == 0U)
    {
        return;
    }
    SlaveAcousticDiag.processed_blocks++;
    for (index = 0U; index < 16U; index++)
    {
        SlaveAcousticDiag.raw_words[index] = block_words[index];
    }
    if ((uint32_t)(sequence - previous_sequence) != 1U)
    {
        Acoustic_Reset(&accumulator);
        SlaveAcousticDiag.discontinuities++;
        SlaveAcoustic_Invalidate();
    }
    previous_sequence = sequence;
    /* Block #8 starts at 111.5 ms; count prevents late ISR timestamps admitting
       earlier blocks. Also require elapsed time so an unintended fast clock fails warmup. */
    if (((uint32_t)(sequence - start_sequence) < 8U) ||
        ((uint32_t)(block_tick - started_at) < (SLAVE_ACOUSTIC_WARMUP_MS + 17U)))
    {
        SlaveAcousticDiag.warmup_blocks++;
        return;
    }
    SlaveAcousticDiag.state = SLAVE_ACOUSTIC_CAPTURING;
    for (index = 0U; index < SPH0645_BLOCK_WORDS; index += 4U)
    {
        SlaveAcousticDiag.processed_frames++;
        if (Acoustic_PushFrame(&accumulator, &block_words[index], &result) != 0U)
        {
            SlaveAcousticDiag.rms_left = result.rms[0];
            SlaveAcousticDiag.rms_right = result.rms[1];
            SlaveAcousticDiag.peak_left = result.peak[0];
            SlaveAcousticDiag.peak_right = result.peak[1];
            SlaveAcousticDiag.mean_left = result.mean[0];
            SlaveAcousticDiag.mean_right = result.mean[1];
            SlaveAcousticDiag.last_left = result.last_sample[0];
            SlaveAcousticDiag.last_right = result.last_sample[1];
            SlaveAcousticDiag.nonzero_left = result.nonzero[0];
            SlaveAcousticDiag.nonzero_right = result.nonzero[1];
            SlaveAcousticDiag.padding_errors_left = result.padding_errors[0];
            SlaveAcousticDiag.padding_errors_right = result.padding_errors[1];
            SlaveAcousticDiag.total_padding_errors_left += result.padding_errors[0];
            SlaveAcousticDiag.total_padding_errors_right += result.padding_errors[1];
            SlaveAcousticDiag.window_tick = block_tick;
            SlaveAcousticDiag.window_count++;
            /* A valid PCM window is NOT proof of sensor presence or calibrated SPL. */
            SlaveAcousticDiag.window_valid =
                ((result.padding_errors[0] == 0U) && (result.padding_errors[1] == 0U)) ? 1U : 0U;
            if (SlaveAcousticDiag.window_valid == 0U)
            {
                SlaveAcoustic_Invalidate();
            }
        }
    }
}

uint8_t SlaveAcoustic_GetLatest(uint32_t now_ms, SlaveAcousticSnapshot *snapshot)
{
    if (snapshot == NULL) { return 0U; }
    memset(snapshot, 0, sizeof(*snapshot));
    SlaveAcoustic_Expire(now_ms);
    if ((SlaveAcousticDiag.state != SLAVE_ACOUSTIC_CAPTURING) ||
        (SlaveAcousticDiag.window_valid == 0U))
    {
        return 0U;
    }
    snapshot->rms_left = SlaveAcousticDiag.rms_left;
    snapshot->rms_right = SlaveAcousticDiag.rms_right;
    snapshot->window_tick = SlaveAcousticDiag.window_tick;
    snapshot->validity_epoch = validity_epoch;
    return 1U;
}

uint8_t SlaveAcoustic_IsSnapshotValid(uint32_t now_ms,
                                    const SlaveAcousticSnapshot *snapshot)
{
    if (snapshot == NULL) { return 0U; }
    SlaveAcoustic_Expire(now_ms);
    return ((SlaveAcousticDiag.state == SLAVE_ACOUSTIC_CAPTURING) &&
            (SlaveAcousticDiag.window_valid != 0U) &&
            (snapshot->validity_epoch == validity_epoch) &&
            ((int32_t)(now_ms - snapshot->window_tick) <
             (int32_t)SLAVE_ACOUSTIC_STALE_MS)) ? 1U : 0U;
}
