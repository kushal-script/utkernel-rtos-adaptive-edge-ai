#include "signal_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_source.h"
#include "ipc_objects.h"
#include "replay_data.h"
#include "uthread.h"
#include "wav.h"

/* Host capture front end, a producer thread standing in for the timer paced DMA, see desktop/README.md. */

static int16_t capture[T1_CAPTURE_SAMPLES];

static uint32_t window_samples = T1_WINDOW_DEFAULT;
static volatile uint32_t completed_half;
static volatile uint32_t block_count;
static volatile uint32_t overruns;
static volatile uint32_t pending;

static volatile uint32_t replay_offset;
static volatile uint32_t node_offset[2];
static volatile uint32_t completed_corpus;
static volatile uint32_t node_turn;
/* Width of the block actually published, a resize can land while one is in flight. */
static volatile uint32_t filled_samples = T1_WINDOW_DEFAULT;
/* Bumped by every restage, the producer drops a block whose parameters changed. */
static volatile uint32_t config_gen;

static const int16_t *corpus       = replay_samples;
static uint32_t       corpus_total = REPLAY_TOTAL_SAMPLES;
static uint32_t       clip_samples = REPLAY_CLIP_SAMPLES;
static uint32_t       clip_count   = REPLAY_CLIP_COUNT;
static const uint8_t *clip_label   = replay_clip_label;
static int16_t       *wav_samples;
static char           description[256] = "built in replay corpus";

static double        speed = 1.0;
static uthread_t     producer;
static volatile int  producer_run;
/* Held until signal_source_start. */
static volatile int  producer_paused = 1;
static int           producer_started;

const int16_t *signal_source_capture_buffer(void) { return capture; }
uint32_t signal_source_block_count(void) { return block_count; }
uint32_t signal_source_overruns(void) { return overruns; }

void signal_source_completed_block(UINT pattern, const int16_t **block,
                                   uint32_t *count)
{
    (void)pattern;
    uint32_t half  = completed_half;
    uint32_t width = filled_samples;
    *block = &capture[half * width];
    *count = width;
}

void signal_source_release_block(void)
{
    pending = 0;
}

uint32_t signal_source_completed_corpus(void)
{
    return completed_corpus;
}

/* Ported unchanged from the device source, it defines what the accuracy figure means. */
int signal_source_label_for_span(uint32_t end_offset, uint32_t span)
{
    if (clip_count == 0u) {
        return -1;
    }
    if (span == 0u || end_offset < span) {
        return -1;
    }

    uint32_t start_offset = end_offset - span;
    uint32_t first = start_offset / clip_samples;
    uint32_t last  = (end_offset - 1u) / clip_samples;
    if (last >= clip_count) {
        return -1;
    }
    if (first == last) {
        return (int)clip_label[last];
    }
    if (last - first > 1u) {
        return -1;
    }

    uint32_t boundary = last * clip_samples;
    uint32_t in_first = boundary - start_offset;
    uint32_t in_last  = end_offset - boundary;
    uint32_t majority = (span * REPLAY_SCORE_MAJORITY_PCT) / 100u;

    if (in_first >= majority) {
        return (int)clip_label[first];
    }
    if (in_last >= majority) {
        return (int)clip_label[last];
    }
    return -1;
}

static uint32_t advance_offset(uint32_t samples)
{
    uint32_t next = replay_offset + samples;
    if (next + samples > corpus_total) {
        next = 0;
    }
    return next;
}

/* Host stand in for the transfer complete interrupt, same steps in the same order. */
static void block_complete(uint32_t width)
{
    uint32_t finished = node_turn;
    completed_half   = finished;
    completed_corpus = node_offset[finished];
    filled_samples   = width;
    node_turn ^= 1u;
    block_count++;

    if (pending) {
        overruns++;
    }
    pending = 1;

    replay_offset = advance_offset(window_samples);
    node_offset[finished] = replay_offset;

    tk_set_flg(flgid_capture, finished == 0 ? FLG_HALF_READY : FLG_FULL_READY);
}

static void producer_entry(void *arg)
{
    (void)arg;
    uint64_t next_us = umonotonic_us();

    while (producer_run) {
        if (producer_paused) {
            usleep_us(1000);
            next_us = umonotonic_us();
            continue;
        }

        uint32_t gen  = config_gen;
        uint32_t w    = window_samples;
        uint32_t half = node_turn;
        uint32_t from = node_offset[half];

        if (from + w <= corpus_total) {
            memcpy(&capture[half * w], &corpus[from], (size_t)w * sizeof(int16_t));
        } else {
            memset(&capture[half * w], 0, (size_t)w * sizeof(int16_t));
        }

        uint64_t period_us = (uint64_t)((double)w * 1000000.0
                                        / (double)SAMPLE_RATE_HZ / speed);
        next_us += period_us;
        uint64_t now = umonotonic_us();
        if (next_us > now) {
            usleep_us(next_us - now);
        } else {
            next_us = now;
        }

        if (!producer_run) {
            break;
        }
        /* A restage mid flight invalidates the block, drop it. */
        if (gen != config_gen) {
            next_us = umonotonic_us();
            continue;
        }
        block_complete(w);
    }
}

ER signal_source_init(void)
{
    memset(capture, 0, sizeof(capture));
    block_count      = 0;
    overruns         = 0;
    pending          = 0;
    completed_half   = 0;
    completed_corpus = 0;
    node_turn        = 0;
    return E_OK;
}

static ER start_at(uint32_t samples, uint32_t base)
{
    if (samples < T1_WINDOW_MIN || samples > T1_WINDOW_MAX) {
        return E_PAR;
    }
    window_samples = samples;
    node_turn      = 0;
    node_offset[0] = base;
    node_offset[1] = (base + samples + samples <= corpus_total) ? base + samples : 0;
    replay_offset  = node_offset[1];
    filled_samples = samples;
    pending        = 0;
    config_gen++;
    return E_OK;
}

ER signal_source_start(uint32_t samples)
{
    ER err = start_at(samples, 0);
    if (err == E_OK) {
        producer_paused = 0;
    }
    return err;
}

void signal_source_pause(void)
{
    producer_paused = 1;
}

void signal_source_resume(void)
{
    pending = 0;
    producer_paused = 0;
}

ER signal_source_set_window(uint32_t samples)
{
    if (samples == window_samples) {
        return E_OK;
    }
    if (samples < T1_WINDOW_MIN || samples > T1_WINDOW_MAX) {
        return E_PAR;
    }
    /* Resume where playback reached, as on the board. */
    uint32_t resume = replay_offset;
    return start_at(samples, resume);
}

void host_source_use_corpus(void)
{
    if (wav_samples != NULL) {
        wav_free(wav_samples);
        wav_samples = NULL;
    }
    corpus       = replay_samples;
    corpus_total = REPLAY_TOTAL_SAMPLES;
    clip_samples = REPLAY_CLIP_SAMPLES;
    clip_count   = REPLAY_CLIP_COUNT;
    clip_label   = replay_clip_label;
    snprintf(description, sizeof(description),
             "built in replay corpus, %u clips of %u samples",
             (unsigned)REPLAY_CLIP_COUNT, (unsigned)REPLAY_CLIP_SAMPLES);
}

int host_source_use_wav(const char *path, char *err, size_t errlen)
{
    int16_t *samples = NULL;
    uint32_t count = 0;
    uint32_t rate  = 0;

    if (wav_read_mono16(path, &samples, &count, &rate, err, errlen) != 0) {
        return -1;
    }
    if (rate != SAMPLE_RATE_HZ) {
        if (err != NULL && errlen > 0) {
            snprintf(err, errlen,
                     "file is %u Hz, the pipeline needs %u Hz, resample it first",
                     (unsigned)rate, (unsigned)SAMPLE_RATE_HZ);
        }
        wav_free(samples);
        return -1;
    }
    if (count < T1_WINDOW_MAX * 2u) {
        if (err != NULL && errlen > 0) {
            snprintf(err, errlen,
                     "file holds %u samples, at least %u are needed to fill both "
                     "capture halves", (unsigned)count, (unsigned)(T1_WINDOW_MAX * 2u));
        }
        wav_free(samples);
        return -1;
    }

    if (wav_samples != NULL) {
        wav_free(wav_samples);
    }
    wav_samples  = samples;
    corpus       = samples;
    corpus_total = count;
    clip_samples = 0;
    clip_count   = 0;
    clip_label   = NULL;
    snprintf(description, sizeof(description),
             "%s, %u samples at %u Hz, unlabelled so nothing is scored",
             path, (unsigned)count, (unsigned)rate);
    return 0;
}

void host_source_set_speed(double s)
{
    if (s > 0.0) {
        speed = s;
    }
}

void host_source_start_producer(void)
{
    if (producer_started) {
        return;
    }
    producer_run = 1;
    if (uthread_start(&producer, producer_entry, NULL) == 0) {
        producer_started = 1;
    }
}

void host_source_stop_producer(void)
{
    if (!producer_started) {
        return;
    }
    producer_run = 0;
    uthread_join(producer);
    producer_started = 0;
    if (wav_samples != NULL) {
        wav_free(wav_samples);
        wav_samples = NULL;
    }
}

uint32_t host_source_total_samples(void) { return corpus_total; }
uint32_t host_source_clip_count(void)    { return clip_count; }
int      host_source_is_labelled(void)   { return clip_count > 0u; }
const char *host_source_description(void) { return description; }

const char *host_source_clip_name(uint32_t index)
{
    if (clip_count == 0u || index >= clip_count) {
        return "unlabelled";
    }
    return replay_clip_name[index];
}
