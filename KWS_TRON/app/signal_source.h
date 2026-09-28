#pragma once

#include <stdint.h>

#include <tk/tkernel.h>

#include "app_config.h"

/* Sample source behind one interface, replay or I2S, see docs/signal_source.md. */

ER signal_source_init(void);

/* Begins capture with the given block size in samples. */
ER signal_source_start(uint32_t window_samples);

/* Re stages capture at a new block size, from a task only; E_OK once in effect. */
ER signal_source_set_window(uint32_t window_samples);

/* The block that just filled, read in place from the half the DMA is not writing. */
void signal_source_completed_block(UINT pattern, const int16_t **block,
                                   uint32_t *count);

/* Blocks captured, and blocks the consumer failed to service in time. */
uint32_t signal_source_block_count(void);

/* Pause the producer with the consumers, or unserviced blocks read as overruns. */
void signal_source_pause(void);
void signal_source_resume(void);
uint32_t signal_source_overruns(void);

/* Marks the handed out block consumed; call after the copy, the overrun count depends on it. */
void signal_source_release_block(void);

/* Corpus index of the most recently filled block. */
uint32_t signal_source_completed_corpus(void);

/* Label for the span ending at end_offset, -1 when it crosses a clip boundary or the wrap. */
int signal_source_label_for_span(uint32_t end_offset, uint32_t span);

/* Raw capture buffer, exposed for the DMA setup in the MSP. */
const int16_t *signal_source_capture_buffer(void);

#if KWS_SIGNAL_SOURCE == KWS_SOURCE_I2S
const int32_t *signal_source_i2s_buffer(void);
uint32_t signal_source_i2s_words(void);
#endif
