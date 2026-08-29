#include "signal_source.h"

#include <string.h>

#include <tm/tmonitor.h>

#include "ipc_objects.h"
#include "main.h"

/* Capture buffer, two halves. The DMA writes one half while the pipeline reads
   the other, so the handoff itself costs no copy. Sized for the largest window
   the controller may select, the transfer size is what actually changes. */
static int16_t capture[T1_CAPTURE_SAMPLES] __attribute__((aligned(4)));

static uint32_t window_samples = T1_WINDOW_DEFAULT;
static volatile uint32_t completed_half;   /* half that filled most recently */
static volatile uint32_t block_count;
static volatile uint32_t overruns;
static volatile uint32_t pending;          /* set by ISR, cleared by consumer */

const int16_t *signal_source_capture_buffer(void) { return capture; }

uint32_t signal_source_block_count(void) { return block_count; }
uint32_t signal_source_overruns(void) { return overruns; }

void signal_source_completed_block(UINT pattern, const int16_t **block,
                                   uint32_t *count)
{
    (void)pattern;
    uint32_t half = completed_half;
    *block = &capture[half * window_samples];
    *count = window_samples;
}

void signal_source_release_block(void)
{
    /* Cleared only once the consumer has copied the block out. Clearing it at
       hand out would hide the case where the next interrupt arrives before the
       consumer is finished, which is exactly the overrun worth counting. */
    pending = 0;
}

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_REPLAY

#include "replay_data.h"

/* Timer paced GPDMA replay.

   TIM6 raises an update event at the sample rate and GPDMA1 channel 1 takes it
   as a hardware request, moving one sample per event out of the flash resident
   corpus into the capture buffer. Direction is peripheral to memory with an
   incrementing source, because the memory to memory setting means a software
   request and would run free rather than at the sample rate.

   Two linked list nodes ping pong between the halves of the capture buffer.
   When a node completes its source address is advanced to the next chunk of
   the corpus, which is a single register write, so the stream keeps moving
   without rebuilding the list. Channel 0 is left alone, it belongs to the I2S
   path. See docs/signal_source.md. */

static DMA_HandleTypeDef hdma_replay;
static DMA_NodeTypeDef   replay_node[2];
static DMA_QListTypeDef  replay_queue;

static volatile uint32_t replay_offset;      /* next corpus index to stage   */
static volatile uint32_t node_offset[2];     /* corpus index staged per node */
static volatile uint32_t completed_corpus;   /* corpus index of the last block */
static volatile uint32_t node_turn;          /* node that completes next     */

#define NODE_SOURCE_REGISTER 3u            /* CSAR, see hal_dma_ex.h     */

static uint32_t advance_offset(uint32_t samples)
{
    uint32_t next = replay_offset + samples;
    if (next + samples > REPLAY_TOTAL_SAMPLES) {
        next = 0;
    }
    return next;
}

static void replay_block_complete(DMA_HandleTypeDef *hdma)
{
    (void)hdma;

    uint32_t finished = node_turn;
    completed_half = finished;
    completed_corpus = node_offset[finished];
    node_turn ^= 1u;
    block_count++;

    if (pending) {
        overruns++;
    }
    pending = 1;

    /* Stage the next chunk into the node that just finished, it will not run
       again until the other node has completed. */
    replay_offset = advance_offset(window_samples);
    node_offset[finished] = replay_offset;
    replay_node[finished].LinkRegisters[NODE_SOURCE_REGISTER] =
        (uint32_t)&replay_samples[replay_offset];

    tk_set_flg(flgid_capture, finished == 0 ? FLG_HALF_READY : FLG_FULL_READY);
}

static ER build_queue(uint32_t samples, uint32_t base)
{
    memset(&replay_queue, 0, sizeof(replay_queue));

    DMA_NodeConfTypeDef node = {0};
    node.NodeType = DMA_GPDMA_LINEAR_NODE;
    node.Init = hdma_replay.Init;
    node.Init.Mode = DMA_NORMAL;
    node.DataSize = samples * sizeof(int16_t);

    for (uint32_t i = 0; i < 2; i++) {
        uint32_t offset = base + i * samples;
        if (offset + samples > REPLAY_TOTAL_SAMPLES) {
            offset = i * samples;
        }
        node_offset[i] = offset;
        node.SrcAddress = (uint32_t)&replay_samples[offset];
        node.DstAddress = (uint32_t)&capture[i * samples];
        if (HAL_DMAEx_List_BuildNode(&node, &replay_node[i]) != HAL_OK) {
            return E_SYS;
        }
        if (HAL_DMAEx_List_InsertNode_Tail(&replay_queue, &replay_node[i]) != HAL_OK) {
            return E_SYS;
        }
    }
    if (HAL_DMAEx_List_SetCircularMode(&replay_queue) != HAL_OK) {
        return E_SYS;
    }
    replay_offset = node_offset[1];
    node_turn = 0;
    return E_OK;
}

static void timer_start(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = (SYSTEM_CLOCK_HZ / SAMPLE_RATE_HZ) - 1u;
    TIM6->EGR = TIM_EGR_UG;          /* latch prescaler and reload      */
    TIM6->SR  = 0;
    TIM6->DIER = TIM_DIER_UDE;       /* update event raises a DMA request */
    TIM6->CR1 = TIM_CR1_CEN;
}

static void timer_stop(void)
{
    TIM6->CR1 = 0;
    TIM6->DIER = 0;
}

ER signal_source_init(void)
{
    __HAL_RCC_GPDMA1_CLK_ENABLE();

    hdma_replay.Instance                   = GPDMA1_Channel1;
    hdma_replay.Init.Request               = GPDMA1_REQUEST_TIM6_UP;
    hdma_replay.Init.BlkHWRequest          = DMA_BREQ_SINGLE_BURST;
    hdma_replay.Init.Direction             = DMA_PERIPH_TO_MEMORY;
    hdma_replay.Init.SrcInc                = DMA_SINC_INCREMENTED;
    hdma_replay.Init.DestInc               = DMA_DINC_INCREMENTED;
    hdma_replay.Init.SrcDataWidth          = DMA_SRC_DATAWIDTH_HALFWORD;
    hdma_replay.Init.DestDataWidth         = DMA_DEST_DATAWIDTH_HALFWORD;
    hdma_replay.Init.SrcBurstLength        = 1;
    hdma_replay.Init.DestBurstLength       = 1;
    hdma_replay.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 |
                                             DMA_DEST_ALLOCATED_PORT0;
    hdma_replay.Init.TransferEventMode     = DMA_TCEM_BLOCK_TRANSFER;
    hdma_replay.Init.Mode                  = DMA_LINKEDLIST_CIRCULAR;

    hdma_replay.InitLinkedList.Priority          = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    hdma_replay.InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
    hdma_replay.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    hdma_replay.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hdma_replay.InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;

    if (HAL_DMAEx_List_Init(&hdma_replay) != HAL_OK) {
        return E_SYS;
    }
    if (HAL_DMA_RegisterCallback(&hdma_replay, HAL_DMA_XFER_CPLT_CB_ID,
                                 replay_block_complete) != HAL_OK) {
        return E_SYS;
    }

    HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);
    return E_OK;
}

static ER start_at(uint32_t samples, uint32_t base)
{
    window_samples = samples;
    memset(capture, 0, sizeof(capture));
    pending = 0;
    completed_half = 0;
    completed_corpus = base;

    ER err = build_queue(samples, base);
    if (err != E_OK) {
        return err;
    }
    if (HAL_DMAEx_List_LinkQ(&hdma_replay, &replay_queue) != HAL_OK) {
        return E_SYS;
    }
    if (HAL_DMAEx_List_Start_IT(&hdma_replay) != HAL_OK) {
        return E_SYS;
    }
    timer_start();
    return E_OK;
}

ER signal_source_start(uint32_t samples)
{
    return start_at(samples, 0);
}

ER signal_source_set_window(uint32_t samples)
{
    if (samples == window_samples) {
        return E_OK;
    }
    if (samples < T1_WINDOW_MIN || samples > T1_WINDOW_MAX) {
        return E_PAR;
    }

    /* Resume where playback had reached. Restarting from the beginning would
       make the corpus never advance once the controller resizes regularly, and
       would silently invalidate every ground truth label. */
    uint32_t resume = replay_offset;
    timer_stop();
    HAL_DMA_Abort(&hdma_replay);
    return start_at(samples, resume);
}

uint32_t signal_source_completed_corpus(void)
{
    return completed_corpus;
}

int signal_source_label_for_span(uint32_t end_offset, uint32_t span)
{
    /* The grid spans very nearly a whole clip, so demanding that it sit
       entirely inside one clip means it essentially never does and nothing is
       ever scored. What is defensible is to score against the clip that
       dominates the span, and to refuse when no clip dominates, because a span
       split evenly across two clips has no single correct answer and guessing
       would quietly corrupt the reported accuracy. */
    if (span == 0u || end_offset < span) {
        return -1;
    }

    uint32_t start_offset = end_offset - span;
    uint32_t first = start_offset / REPLAY_CLIP_SAMPLES;
    uint32_t last  = (end_offset - 1u) / REPLAY_CLIP_SAMPLES;
    if (last >= REPLAY_CLIP_COUNT) {
        return -1;
    }
    if (first == last) {
        return (int)replay_clip_label[last];
    }
    if (last - first > 1u) {
        return -1;      /* spans three or more clips, nothing dominates */
    }

    /* Exactly two clips: score only if one of them holds a clear majority. */
    uint32_t boundary = last * REPLAY_CLIP_SAMPLES;
    uint32_t in_first = boundary - start_offset;
    uint32_t in_last  = end_offset - boundary;
    uint32_t majority = (span * REPLAY_SCORE_MAJORITY_PCT) / 100u;

    if (in_first >= majority) {
        return (int)replay_clip_label[first];
    }
    if (in_last >= majority) {
        return (int)replay_clip_label[last];
    }
    return -1;
}

void GPDMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_replay);
}

#else  /* KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S */

/* Live INMP441 capture. The microphone delivers 24 bit samples in 32 bit I2S
   slots, so the raw buffer is twice the width of the capture buffer and the
   completed block is narrowed before the pipeline sees it. Needs hardware
   verification, see docs/mic_verification.md. */

extern I2S_HandleTypeDef hi2s2;

static int32_t i2s_raw[T1_CAPTURE_SAMPLES * 2] __attribute__((aligned(4)));

const int32_t *signal_source_i2s_buffer(void) { return i2s_raw; }
uint32_t signal_source_i2s_words(void)
{
    return (uint32_t)(sizeof(i2s_raw) / sizeof(i2s_raw[0]));
}

ER signal_source_init(void) { return E_OK; }

ER signal_source_start(uint32_t samples)
{
    window_samples = samples;
    if (HAL_I2S_Receive_DMA(&hi2s2, (uint16_t *)i2s_raw,
                            (uint16_t)(samples * 4)) != HAL_OK) {
        return E_SYS;
    }
    return E_OK;
}

ER signal_source_set_window(uint32_t samples)
{
    HAL_I2S_DMAStop(&hi2s2);
    return signal_source_start(samples);
}

/* A live microphone carries no ground truth, so nothing can be scored from it. */
uint32_t signal_source_completed_corpus(void) { return 0; }
int signal_source_label_for_span(uint32_t end_offset, uint32_t span)
{
    (void)end_offset; (void)span;
    return -1;
}

static void narrow(uint32_t half)
{
    /* One channel of the stereo frame carries signal, take the upper 16 bits
       of the 24 bit sample so the pipeline always sees int16. */
    const int32_t *src = &i2s_raw[half * window_samples * 2];
    int16_t *dst = &capture[half * window_samples];
    for (uint32_t i = 0; i < window_samples; i++) {
        dst[i] = (int16_t)(src[i * 2] >> 8);
    }
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) {
        completed_half = 0;
        narrow(0);
        block_count++;
        if (pending) { overruns++; }
        pending = 1;
        tk_set_flg(flgid_capture, FLG_HALF_READY);
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2) {
        completed_half = 1;
        narrow(1);
        block_count++;
        if (pending) { overruns++; }
        pending = 1;
        tk_set_flg(flgid_capture, FLG_FULL_READY);
    }
}

#endif
